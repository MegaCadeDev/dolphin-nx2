// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/SwitchDiagnostics.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <switch/runtime/nxlink.h>

#include "Common/Logging/Log.h"
#include "Common/Logging/LogManager.h"

#ifdef __SWITCH2__
#include <kaihen.h>
#endif

// libounce's linker script defines these as absolute symbols, so they are not
// relocated: __start__ is 0 and __end__ is the image size. The load address
// comes from the memory region holding this code instead.
extern "C" char __start__[];
extern "C" char __end__[];

namespace Common::SwitchDiagnostics
{
namespace
{
constexpr const char* DIAG_DIRECTORY = "sdmc:/switch/dolphin/diag";
constexpr const char* BOOT_LOG = "sdmc:/switch/dolphin/diag/boot.log";
constexpr const char* BOOT_LOG_PREVIOUS = "sdmc:/switch/dolphin/diag/boot.prev.log";
constexpr const char* STDERR_LOG = "sdmc:/switch/dolphin/diag/stderr.log";
constexpr const char* STDERR_LOG_PREVIOUS = "sdmc:/switch/dolphin/diag/stderr.prev.log";
// Logs are appended across runs (Dolphin relaunches itself through hbloader)
// and rotated once they pass this size.
constexpr off_t ROTATE_SIZE = 1024 * 1024;
constexpr const char* CRASH_LOG = "sdmc:/switch/dolphin/diag/crash.log";

int s_boot_fd = -1;
// The nxlink host stream (hbmenu netloader with nxlink -s), or -1.
int s_nxlink_fd = -1;
bool s_sockets_deferred = false;
u64 s_image_base = 0;
u64 s_image_end = 0;
std::mutex s_mark_mutex;
// The most recent mark, copied into crash.log.
char s_last_mark[256] = "(none)";

u64 ElapsedMs()
{
  return armTicksToNs(armGetSystemTick()) / 1000000;
}

void WriteAll(int fd, const char* text, size_t length)
{
  while (length > 0)
  {
    const ssize_t written = write(fd, text, length);
    if (written <= 0)
      return;
    text += written;
    length -= static_cast<size_t>(written);
  }
}

// snprintf into a stack buffer and write it; usable from the exception handler.
void WriteFormat(int fd, const char* format, ...) __attribute__((format(printf, 2, 3)));
void WriteFormat(int fd, const char* format, ...)
{
  char line[512];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0)
    WriteAll(fd, line, std::min(static_cast<size_t>(length), sizeof(line) - 1));
}

void FindImage()
{
  // .text comes first in the image, so its region starts at the load address.
  MemoryInfo info{};
  u32 page_info = 0;
  if (R_SUCCEEDED(svcQueryMemory(&info, &page_info, reinterpret_cast<u64>(&FindImage))))
  {
    s_image_base = info.addr;
    s_image_end = info.addr + static_cast<u64>(__end__ - __start__);
  }
}

bool InImage(u64 address)
{
  return address >= s_image_base && address < s_image_end;
}

u64 ImageOffset(u64 address)
{
  return address - s_image_base;
}

void LogSystemInfo()
{
  Mark("image base 0x%lx end 0x%lx (size 0x%lx)", s_image_base, s_image_end,
       static_cast<unsigned long>(s_image_end - s_image_base));
  Mark("applet type %d", static_cast<int>(appletGetAppletType()));

  u64 value = 0;
  if (R_SUCCEEDED(svcGetInfo(&value, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
    Mark("process memory total 0x%lx (%lu MiB)", value, value >> 20);
  if (R_SUCCEEDED(svcGetInfo(&value, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)))
    Mark("process memory used 0x%lx (%lu MiB)", value, value >> 20);
  if (R_SUCCEEDED(svcGetInfo(&value, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
    Mark("core mask 0x%lx", value);
  if (R_SUCCEEDED(svcGetInfo(&value, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0)))
    Mark("alias region size 0x%lx", value);
  if (R_SUCCEEDED(svcGetInfo(&value, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0)))
    Mark("heap region size 0x%lx", value);
  if (R_SUCCEEDED(svcGetInfo(&value, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0)))
    Mark("aslr region size 0x%lx", value);

#ifdef __SWITCH2__
  Service ro;
  const Result ro_result = kaihenGetRoSession(&ro);
  Mark("kaiHEN ldr:ro session: %s (0x%x)", R_SUCCEEDED(ro_result) ? "present" : "MISSING",
       ro_result);
#endif
}

// Rotates path to previous once it is large. FAT's rename does not replace an
// existing file, so the old previous is deleted first.
void RotateIfLarge(const char* path, const char* previous)
{
  struct stat info{};
  if (stat(path, &info) != 0 || info.st_size < ROTATE_SIZE)
    return;
  unlink(previous);
  rename(path, previous);
}

// Copies Dolphin's own log lines into boot.log.
class BootLogListener final : public Common::Log::LogListener
{
public:
  void Log(Common::Log::LogLevel, const char* text) override
  {
    std::string_view line(text);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.remove_suffix(1);
    Mark("log: %.*s", static_cast<int>(line.size()), line.data());
  }
};

void AtExit()
{
  Mark("process exiting (atexit)");
}

[[noreturn]] void TerminateHandler()
{
  Mark("std::terminate called (uncaught exception or noexcept violation)");
  abort();
}

// Runs from __libc_init_array, after libnx has mounted the SD card and before
// main() and other C++ static constructors.
__attribute__((constructor(101))) void Initialize()
{
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/dolphin", 0777);
  mkdir(DIAG_DIRECTORY, 0777);
  RotateIfLarge(BOOT_LOG, BOOT_LOG_PREVIOUS);
  RotateIfLarge(STDERR_LOG, STDERR_LOG_PREVIOUS);
  s_boot_fd = open(BOOT_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (s_boot_fd >= 0)
  {
    constexpr std::string_view separator = "\n========== new run ==========\n";
    WriteAll(s_boot_fd, separator.data(), separator.size());
  }

  if (freopen(STDERR_LOG, "a", stderr))
  {
    setvbuf(stderr, nullptr, _IONBF, 0);
    // stdout shares the file; line-buffered so it interleaves with stderr.
    if (freopen(STDERR_LOG, "a", stdout))
      setvbuf(stdout, nullptr, _IOLBF, 0);
  }

  FindImage();
  std::set_terminate(TerminateHandler);
  atexit(AtExit);
  Mark("Dolphin diagnostics build started");
  LogSystemInfo();
}
}  // namespace

u64 ElfOffset(const void* address)
{
  const u64 value = reinterpret_cast<u64>(address);
  return InImage(value) ? ImageOffset(value) : 0;
}

void Mark(const char* format, ...)
{
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  std::lock_guard lock{s_mark_mutex};
  std::memcpy(s_last_mark, message, sizeof(s_last_mark));
  if (s_boot_fd >= 0)
  {
    WriteFormat(s_boot_fd, "[%8lu ms] %s\n", ElapsedMs(), message);
    fsync(s_boot_fd);
  }
  if (s_nxlink_fd >= 0)
    WriteFormat(s_nxlink_fd, "[%8lu ms] %s\n", ElapsedMs(), message);
}

void LogMemoryMap(const char* label)
{
  Mark("memory map (%s):", label);
  u64 address = 0;
  u64 heap_bytes = 0;
  unsigned listed = 0;
  unsigned skipped = 0;
  unsigned suspect = 0;
  for (;;)
  {
    MemoryInfo info{};
    u32 page_info = 0;
    if (R_FAILED(svcQueryMemory(&info, &page_info, address)))
      break;
    const bool plain_heap = info.type == MemType_Heap && info.attr == 0;
    if (plain_heap)
      heap_bytes += info.size;
    if (info.type != MemType_Unmapped && !plain_heap)
    {
      // What a teardown can leave behind: memory mirrors, process-memory and
      // code aliases, transfer memory, and heap pages that are lent out.
      switch (info.type & MemState_Type)
      {
      case MemType_Heap:
      case MemType_WeirdMappedMem:
      case MemType_MappedMemory:
      case MemType_ProcessMem:
      case MemType_TransferMemIsolated:
      case MemType_TransferMem:
      case MemType_CodeReadOnly:
      case MemType_CodeWritable:
        ++suspect;
        break;
      case MemType_ModuleCodeStatic:
      case MemType_ModuleCodeMutable:
        suspect += !InImage(info.addr);
        break;
      default:
        break;
      }
      if (listed < 200)
      {
        Mark("  0x%010lx size 0x%09lx type 0x%02x attr 0x%x perm 0x%x ipc %u dev %u%s", info.addr,
             info.size, info.type, info.attr, info.perm, info.ipc_refcount,
             info.device_refcount, InImage(info.addr) ? " (image)" : "");
        ++listed;
      }
      else
      {
        ++skipped;
      }
    }
    const u64 next = info.addr + info.size;
    if (next <= address)
      break;
    address = next;
  }
  u64 used = 0;
  svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  Mark("memory map end: %u regions listed, %u skipped, plain heap 0x%lx, used 0x%lx", listed,
       skipped, heap_bytes, used);
  Mark("memory map verdict (%s): %u region(s) that can lock memory for the next program", label,
       suspect);
}

void ConnectNxlink()
{
  if (__nxlink_host.s_addr == 0)
    return;
  s_nxlink_fd = nxlinkConnectToHost(false, false);
  Mark("nxlink: streaming this log to the host: %s", s_nxlink_fd >= 0 ? "yes" : "failed");
}

bool KeepSocketsForNxlink()
{
  s_sockets_deferred = s_nxlink_fd >= 0;
  return s_sockets_deferred;
}

void FinishExitReport()
{
  Mark("exit report done");
  if (s_nxlink_fd >= 0)
  {
    close(s_nxlink_fd);
    s_nxlink_fd = -1;
  }
  if (s_sockets_deferred)
  {
    socketExit();
    s_sockets_deferred = false;
  }
}

bool IsFastmemDisabledByFile()
{
  struct stat info{};
  return stat("sdmc:/switch/dolphin/diag/no-fastmem", &info) == 0;
}

bool IsDisabledByFile(const char* name)
{
  char path[96];
  std::snprintf(path, sizeof(path), "%s/%s", DIAG_DIRECTORY, name);
  struct stat info{};
  const bool disabled = stat(path, &info) == 0;
  if (disabled)
    Mark("test switch: %s is set, leaving it off", name);
  return disabled;
}

bool IsSetByFile(const char* name)
{
  char path[96];
  std::snprintf(path, sizeof(path), "%s/%s", DIAG_DIRECTORY, name);
  struct stat info{};
  const bool set = stat(path, &info) == 0;
  if (set)
    Mark("test switch: %s is set", name);
  return set;
}

void EnableDolphinLogging()
{
  auto* const manager = Common::Log::LogManager::GetInstance();
  if (!manager)
    return;
  using Common::Log::LogType;
  for (const LogType type : {LogType::BOOT, LogType::COMMON, LogType::CORE, LogType::DYNA_REC,
                             LogType::HOST_GPU, LogType::MASTER_LOG, LogType::MEMMAP,
                             LogType::VIDEO})
  {
    manager->SetEnable(type, true);
  }
  manager->SetConfigLogLevel(Common::Log::LogLevel::LINFO);
  manager->EnableListener(Common::Log::LogListener::CONSOLE_LISTENER, true);
  // The Switch frontend has no log window; reuse its slot for boot.log.
  manager->RegisterListener(Common::Log::LogListener::LOG_WINDOW_LISTENER,
                            std::make_unique<BootLogListener>());
  manager->EnableListener(Common::Log::LogListener::LOG_WINDOW_LISTENER, true);
  Mark("Dolphin console logging enabled (effective level %d)",
       static_cast<int>(manager->GetEffectiveLogLevel()));
}

void WriteCrashReport(const ThreadExceptionDump* context)
{
  const int fd = open(CRASH_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    return;

  WriteFormat(fd, "Dolphin crashed %lu ms after boot\n", ElapsedMs());
  WriteFormat(fd, "last mark: %s\n", s_last_mark);
  WriteFormat(fd, "error_desc 0x%x  esr 0x%x  afsr0 0x%x  afsr1 0x%x  pstate 0x%x\n",
              context->error_desc, context->esr, context->afsr0, context->afsr1,
              context->pstate);
  WriteFormat(fd, "image base 0x%lx  end 0x%lx\n", s_image_base, s_image_end);

  const auto describe = [fd](const char* name, u64 value) {
    if (InImage(value))
      WriteFormat(fd, "%-4s 0x%016lx  (dolphin.elf+0x%lx)\n", name, value, ImageOffset(value));
    else
      WriteFormat(fd, "%-4s 0x%016lx\n", name, value);
  };
  describe("pc", context->pc.x);
  describe("lr", context->lr.x);
  describe("sp", context->sp.x);
  describe("fp", context->fp.x);
  describe("far", context->far.x);
  for (int i = 0; i < 29; ++i)
  {
    char name[8];
    snprintf(name, sizeof(name), "x%d", i);
    describe(name, context->cpu_gprs[i].x);
  }

  // -fomit-frame-pointer makes frame walking unreliable; list image addresses
  // found on the stack instead. Most are return addresses.
  WriteFormat(fd, "\nstack scan (image addresses in the top 16 KiB):\n");
  const u64* stack = reinterpret_cast<const u64*>(context->sp.x & ~u64{7});
  MemoryInfo info{};
  u32 page_info = 0;
  size_t words = 0x4000 / sizeof(u64);
  if (R_SUCCEEDED(svcQueryMemory(&info, &page_info, reinterpret_cast<u64>(stack))) &&
      (info.perm & Perm_R))
  {
    const u64 readable_end = info.addr + info.size;
    words = std::min(words, static_cast<size_t>((readable_end - reinterpret_cast<u64>(stack)) /
                                                sizeof(u64)));
    for (size_t i = 0; i < words; ++i)
    {
      if (InImage(stack[i]))
        WriteFormat(fd, "  [sp+0x%04lx] dolphin.elf+0x%lx\n", i * sizeof(u64),
                    ImageOffset(stack[i]));
    }
  }
  else
  {
    WriteFormat(fd, "  stack not readable\n");
  }

  fsync(fd);
  close(fd);

  if (s_boot_fd >= 0)
  {
    WriteFormat(s_boot_fd, "[%8lu ms] CRASH: pc 0x%lx far 0x%lx, see crash.log\n", ElapsedMs(),
                context->pc.x, context->far.x);
    fsync(s_boot_fd);
  }
}
}  // namespace Common::SwitchDiagnostics

// libnx calls this at the start of __appExit, before services and the GPU
// driver are torn down. The last line before a hang on exit shows how far it got.
extern "C" void userAppExit(void)
{
  Common::SwitchDiagnostics::Mark("userAppExit: libnx teardown starting");
}
