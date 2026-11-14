// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Bring-up diagnostics for the Switch frontend, written to sdmc:/switch/dolphin/diag/:
//   boot.log    startup breadcrumbs and system information, synced after every line
//   stderr.log  stderr (Mesa and Dolphin's console log listener) and stdout
//   crash.log   registers, image offsets and a stack scan from the exception handler
// Without DOLPHIN_SWITCH_DIAGNOSTICS every function is an empty inline.

#if defined(__SWITCH__) && defined(DOLPHIN_SWITCH_DIAGNOSTICS)

#include <switch.h>

namespace Common::SwitchDiagnostics
{
// Appends a printf-style line to boot.log and syncs it to the SD card.
void Mark(const char* format, ...) __attribute__((format(printf, 1, 2)));

// Turns on Dolphin's console log listener for the startup-relevant log types.
void EnableDolphinLogging();

// Marks every mapped region that is not plain heap, plus totals, so memory a
// teardown left behind (code aliases, process-memory views) shows up.
void LogMemoryMap(const char* label);

// When Dolphin was started through hbmenu's netloader (nxlink -s), also sends
// every mark to the nxlink host so the log can be read live. Call once sockets
// are initialized.
void ConnectNxlink();

// True while marks go to nxlink: the socket service then stays up until
// FinishExitReport, so the exit cleanup's report still reaches the host.
bool KeepSocketsForNxlink();

// The last diagnostics step at exit: closes the nxlink stream and the socket
// service it kept alive.
void FinishExitReport();

// True when sdmc:/switch/dolphin/diag/no-fastmem exists: fastmem is then
// reported unsupported, to isolate its mappings from other exit problems.
bool IsFastmemDisabledByFile();

// Writes crash.log. Safe to call from the libnx exception handler: no allocation.
void WriteCrashReport(const ThreadExceptionDump* context);
}  // namespace Common::SwitchDiagnostics

#define SWITCH_DIAG_MARK(...) ::Common::SwitchDiagnostics::Mark(__VA_ARGS__)

#else

#define SWITCH_DIAG_MARK(...) ((void)0)

#endif
