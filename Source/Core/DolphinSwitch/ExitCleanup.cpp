// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Switch 2 (kaiHEN): when Dolphin exits, the homebrew loader runs the next
// program (hbmenu) in this same process. Memory Dolphin leaves mapped keeps
// heap pages locked, and hbmenu then starts without the memory its GPU needs:
// a black screen until kaiHEN is started again. The libnx runtime only cleans
// up what it created itself, so this releases the rest once everything else
// has shut down:
//
// - Threads: libnx mirrors a thread's stack (svcMapMemory) until the thread is
//   closed by a join. A thread nobody joined keeps its stack mapped after it
//   ended; a thread still running would also keep executing Dolphin's code
//   under the next program. Every pthread (std::thread) is tracked through
//   -Wl,--wrap=__syscall_thread_create to find them at exit.
// - Everything else is reported: the diagnostics build lists the regions still
//   mapped so a leak shows up in one run.
//
// Service lookups: kaiHEN's SM proxy forwards every program's lookups on one
// shared SM session. SM answers a lookup of a service that is not registered
// only once it is, so such a lookup blocks that session, and every later
// program's lookups (hbmenu's included), until kaiHEN restarts. The diagnostics
// build logs each lookup and, at exit, the ones that never got an answer, with
// every thread's entry point, creator and stack so the caller can be found
// with tools/symbolize.sh.

#include "DolphinSwitch/ExitCleanup.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <switch.h>

#include "Common/SwitchDiagnostics.h"

struct __pthread_t;

extern "C" int __real___syscall_thread_create(__pthread_t** thread, void* (*func)(void*),
                                              void* arg, void* stack_addr, size_t stack_size);
#ifdef DOLPHIN_SWITCH_DIAGNOSTICS
extern "C" Result __real_threadCreate(Thread* t, ThreadFunc entry, void* arg, void* stack_mem,
                                      size_t stack_sz, int prio, int cpuid);
extern "C" Result __real_smGetServiceWrapper(Service* service_out, SmServiceName name);
extern "C" Result __real_smGetServiceOriginal(Handle* handle_out, SmServiceName name);
#endif

namespace DolphinSwitch::ExitCleanup
{
namespace
{
struct ThreadStart
{
  void* (*func)(void*);
  void* arg;
};

struct TrackedThread
{
  Thread* thread = nullptr;
  void* mirror = nullptr;
  void* memory = nullptr;
  void* (*func)(void*) = nullptr;
  // The first word of the thread's argument: for a std::thread, the vtable of
  // its callable, which names the function that started it.
  void* hint = nullptr;
  bool live = false;
};

std::atomic<bool> s_release_exit_lock{false};

constexpr size_t MAX_TRACKED = 512;
TrackedThread s_threads[MAX_TRACKED];
Mutex s_lock;

int TrackStart(Thread* self, void* (*func)(void*), void* hint)
{
  mutexLock(&s_lock);
  int free_slot = -1;
  for (size_t i = 0; i < MAX_TRACKED; ++i)
  {
    TrackedThread& entry = s_threads[i];
    // A finished thread whose mirror address was reused has been closed: forget it.
    if (!entry.live && entry.mirror == self->stack_mirror)
      entry = {};
    if (free_slot < 0 && !entry.live && !entry.mirror)
      free_slot = static_cast<int>(i);
  }
  if (free_slot >= 0)
    s_threads[free_slot] = {self, self->stack_mirror, self->stack_mem, func, hint, true};
  mutexUnlock(&s_lock);
  return free_slot;
}

void TrackFinish(int slot)
{
  if (slot < 0)
    return;
  mutexLock(&s_lock);
  s_threads[slot].live = false;
  s_threads[slot].thread = nullptr;
  mutexUnlock(&s_lock);
}

void* Trampoline(void* argument)
{
  const ThreadStart start = *static_cast<ThreadStart*>(argument);
  std::free(argument);
  Thread* const self = threadGetSelf();
  void* const hint = start.arg ? *static_cast<void**>(start.arg) : nullptr;
  const int slot = self ? TrackStart(self, start.func, hint) : -1;
  void* const result = start.func(start.arg);
  TrackFinish(slot);
  return result;
}

unsigned CountLive()
{
  unsigned live = 0;
  mutexLock(&s_lock);
  for (const TrackedThread& entry : s_threads)
    live += entry.live;
  mutexUnlock(&s_lock);
  return live;
}

#ifdef DOLPHIN_SWITCH_DIAGNOSTICS
struct PendingLookup
{
  u64 name = 0;
  Handle thread = INVALID_HANDLE;
  u64 start_tick = 0;
};

constexpr size_t MAX_PENDING = 32;
PendingLookup s_pending[MAX_PENDING];
Mutex s_pending_lock;

const char* NameOf(const SmServiceName& name, char (&out)[9])
{
  std::memcpy(out, name.name, 8);
  out[8] = '\0';
  return out;
}

int BeginLookup(SmServiceName name)
{
  char text[9];
  SWITCH_DIAG_MARK("sm: looking up '%s' (thread handle 0x%x)", NameOf(name, text),
                   threadGetCurHandle());
  int slot = -1;
  mutexLock(&s_pending_lock);
  for (size_t i = 0; i < MAX_PENDING; ++i)
  {
    if (s_pending[i].name == 0)
    {
      u64 raw = 0;
      std::memcpy(&raw, name.name, sizeof(raw));
      s_pending[i] = {raw ? raw : 1, threadGetCurHandle(), armGetSystemTick()};
      slot = static_cast<int>(i);
      break;
    }
  }
  mutexUnlock(&s_pending_lock);
  return slot;
}

void EndLookup(int slot, SmServiceName name, Result rc)
{
  u64 elapsed_ms = 0;
  mutexLock(&s_pending_lock);
  if (slot >= 0)
  {
    elapsed_ms = armTicksToNs(armGetSystemTick() - s_pending[slot].start_tick) / 1'000'000;
    s_pending[slot] = {};
  }
  mutexUnlock(&s_pending_lock);
  char text[9];
  SWITCH_DIAG_MARK("sm: '%s' -> 0x%x (%lu ms)", NameOf(name, text), rc,
                   static_cast<unsigned long>(elapsed_ms));
}

void ReportPendingLookups()
{
  unsigned pending = 0;
  mutexLock(&s_pending_lock);
  for (const PendingLookup& lookup : s_pending)
  {
    if (lookup.name == 0)
      continue;
    char text[9] = {};
    std::memcpy(text, &lookup.name, 8);
    SWITCH_DIAG_MARK("sm: LOOKUP OF '%s' NEVER ANSWERED (thread handle 0x%x, %lu ms): it blocks "
                     "kaiHEN's SM session for every later program",
                     text, lookup.thread,
                     static_cast<unsigned long>(
                         armTicksToNs(armGetSystemTick() - lookup.start_tick) / 1'000'000));
    ++pending;
  }
  mutexUnlock(&s_pending_lock);
  SWITCH_DIAG_MARK("sm: %u lookup(s) still waiting at exit", pending);
}
#endif

bool IsStackMirror(void* mirror, u64* size)
{
  MemoryInfo info{};
  u32 page_info = 0;
  if (R_FAILED(svcQueryMemory(&info, &page_info, reinterpret_cast<u64>(mirror))))
    return false;
  *size = info.size;
  return (info.type & MemState_Type) == MemType_MappedMemory &&
         info.addr == reinterpret_cast<u64>(mirror);
}

void Run()
{
  SWITCH_DIAG_MARK("exit cleanup: start, %u tracked thread(s) running", CountLive());

  // Threads still running get a second to end by themselves.
  for (int waited = 0; waited < 1000 && CountLive() != 0; waited += 10)
    svcSleepThread(10'000'000);

  unsigned paused = 0;
  unsigned released = 0;
  unsigned failed = 0;
  u64 released_bytes = 0;
  const Handle current = threadGetCurHandle();
  mutexLock(&s_lock);
  for (TrackedThread& entry : s_threads)
  {
    if (entry.live && entry.thread && entry.thread->handle != current)
    {
      // Never resumed: it must not run Dolphin's code under the next program.
      SWITCH_DIAG_MARK("exit cleanup: thread %p (handle 0x%x, stack %p, start dolphin.elf+0x%lx, "
                       "callable dolphin.elf+0x%lx) still running, pausing it",
                       entry.thread, entry.thread->handle, entry.mirror,
                       static_cast<unsigned long>(Common::SwitchDiagnostics::ElfOffset(
                           reinterpret_cast<void*>(entry.func))),
                       static_cast<unsigned long>(Common::SwitchDiagnostics::ElfOffset(entry.hint)));
      svcSetThreadActivity(entry.thread->handle, ThreadActivity_Paused);
      entry.live = false;
      ++paused;
    }
    if (entry.live || !entry.mirror)
      continue;
    u64 size = 0;
    if (IsStackMirror(entry.mirror, &size))
    {
      // The mirror covers the stack plus libnx's TLS and reent page after it.
      const Result rc = svcUnmapMemory(entry.mirror, entry.memory, size);
      if (R_SUCCEEDED(rc))
      {
        ++released;
        released_bytes += size;
      }
      else
      {
        ++failed;
        SWITCH_DIAG_MARK("exit cleanup: unmapping stack %p (start %p) failed: 0x%x",
                         entry.mirror, reinterpret_cast<void*>(entry.func), rc);
      }
      SWITCH_DIAG_MARK("exit cleanup: released unjoined stack %p (callable dolphin.elf+0x%lx)",
                       entry.mirror,
                       static_cast<unsigned long>(Common::SwitchDiagnostics::ElfOffset(entry.hint)));
    }
    entry = {};
  }
  mutexUnlock(&s_lock);
  SWITCH_DIAG_MARK("exit cleanup: %u thread(s) paused, %u unjoined stack(s) released "
                   "(%lu KiB), %u failed",
                   paused, released, static_cast<unsigned long>(released_bytes >> 10), failed);

#ifdef DOLPHIN_SWITCH_DIAGNOSTICS
  ReportPendingLookups();
  Common::SwitchDiagnostics::LogMemoryMap("after exit cleanup");
#endif

  if (s_release_exit_lock.exchange(false))
  {
    SWITCH_DIAG_MARK("exit cleanup: releasing the exit lock");
    const Result rc = appletUnlockExit();
    SWITCH_DIAG_MARK("exit cleanup: exit lock released: 0x%x", rc);
  }

#ifdef DOLPHIN_SWITCH_DIAGNOSTICS
  Common::SwitchDiagnostics::FinishExitReport();
#endif
}

__attribute__((constructor(102))) void Register()
{
  mutexInit(&s_lock);
#ifdef DOLPHIN_SWITCH_DIAGNOSTICS
  mutexInit(&s_pending_lock);
#endif
  // Registered before any C++ static object is constructed, so it runs after
  // their destructors: the last of Dolphin's code before libnx's own teardown.
  std::atexit(Run);
}
}  // namespace

void ReleaseExitLockLast()
{
  s_release_exit_lock.store(true);
}
}  // namespace DolphinSwitch::ExitCleanup

extern "C" int __wrap___syscall_thread_create(__pthread_t** thread, void* (*func)(void*),
                                              void* arg, void* stack_addr, size_t stack_size);
extern "C" int __wrap___syscall_thread_create(__pthread_t** thread, void* (*func)(void*),
                                              void* arg, void* stack_addr, size_t stack_size)
{
  using DolphinSwitch::ExitCleanup::ThreadStart;
  auto* const start = static_cast<ThreadStart*>(std::malloc(sizeof(ThreadStart)));
  if (!start)
    return ENOMEM;
  *start = {func, arg};
  const int rc = __real___syscall_thread_create(
      thread, DolphinSwitch::ExitCleanup::Trampoline, start, stack_addr, stack_size);
  if (rc != 0)
    std::free(start);
  return rc;
}

#ifdef DOLPHIN_SWITCH_DIAGNOSTICS
// Every libnx thread, pthreads included: the stack address matches the type
// 0x0B regions of the exit memory map, the entry point and creator name it.
extern "C" Result __wrap_threadCreate(Thread* t, ThreadFunc entry, void* arg, void* stack_mem,
                                      size_t stack_sz, int prio, int cpuid);
extern "C" Result __wrap_threadCreate(Thread* t, ThreadFunc entry, void* arg, void* stack_mem,
                                      size_t stack_sz, int prio, int cpuid)
{
  void* const caller = __builtin_return_address(0);
  const Result rc = __real_threadCreate(t, entry, arg, stack_mem, stack_sz, prio, cpuid);
  using Common::SwitchDiagnostics::ElfOffset;
  SWITCH_DIAG_MARK("thread: created 0x%x, stack %p size 0x%lx, entry dolphin.elf+0x%lx, "
                   "creator dolphin.elf+0x%lx",
                   rc, R_SUCCEEDED(rc) ? t->stack_mirror : nullptr,
                   static_cast<unsigned long>(stack_sz),
                   static_cast<unsigned long>(ElfOffset(reinterpret_cast<void*>(entry))),
                   static_cast<unsigned long>(ElfOffset(caller)));
  return rc;
}

extern "C" Result __wrap_smGetServiceWrapper(Service* service_out, SmServiceName name);
extern "C" Result __wrap_smGetServiceWrapper(Service* service_out, SmServiceName name)
{
  const int slot = DolphinSwitch::ExitCleanup::BeginLookup(name);
  const Result rc = __real_smGetServiceWrapper(service_out, name);
  DolphinSwitch::ExitCleanup::EndLookup(slot, name, rc);
  return rc;
}

extern "C" Result __wrap_smGetServiceOriginal(Handle* handle_out, SmServiceName name);
extern "C" Result __wrap_smGetServiceOriginal(Handle* handle_out, SmServiceName name)
{
  const int slot = DolphinSwitch::ExitCleanup::BeginLookup(name);
  const Result rc = __real_smGetServiceOriginal(handle_out, name);
  DolphinSwitch::ExitCleanup::EndLookup(slot, name, rc);
  return rc;
}
#endif
