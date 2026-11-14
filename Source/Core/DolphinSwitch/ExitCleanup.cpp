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

#include "DolphinSwitch/ExitCleanup.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>

#include <switch.h>

#include "Common/SwitchDiagnostics.h"

struct __pthread_t;

extern "C" int __real___syscall_thread_create(__pthread_t** thread, void* (*func)(void*),
                                              void* arg, void* stack_addr, size_t stack_size);

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
  bool live = false;
};

std::atomic<bool> s_release_exit_lock{false};

constexpr size_t MAX_TRACKED = 512;
TrackedThread s_threads[MAX_TRACKED];
Mutex s_lock;

int TrackStart(Thread* self, void* (*func)(void*))
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
    s_threads[free_slot] = {self, self->stack_mirror, self->stack_mem, func, true};
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
  const int slot = self ? TrackStart(self, start.func) : -1;
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
      SWITCH_DIAG_MARK("exit cleanup: thread %p (start %p) still running, pausing it",
                       entry.thread, reinterpret_cast<void*>(entry.func));
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
    }
    entry = {};
  }
  mutexUnlock(&s_lock);
  SWITCH_DIAG_MARK("exit cleanup: %u thread(s) paused, %u unjoined stack(s) released "
                   "(%lu KiB), %u failed",
                   paused, released, static_cast<unsigned long>(released_bytes >> 10), failed);

#ifdef DOLPHIN_SWITCH_DIAGNOSTICS
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
