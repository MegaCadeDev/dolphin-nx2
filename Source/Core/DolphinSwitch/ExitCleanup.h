// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace DolphinSwitch::ExitCleanup
{
// Hands the HOME exit lock to the exit cleanup, which releases it as its last
// step. After an exit request the system ends the process as soon as the lock
// is released, so releasing it any earlier would skip the cleanup.
void ReleaseExitLockLast();
}  // namespace DolphinSwitch::ExitCleanup
