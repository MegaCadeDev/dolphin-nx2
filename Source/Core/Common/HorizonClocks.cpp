// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/HorizonClocks.h"

#include <mutex>

#include <switch.h>

#include "Common/SwitchDiagnostics.h"

namespace Common::HorizonClocks
{
namespace
{
std::mutex s_mutex;
unsigned int s_boost_count = 0;
bool s_boost_active = false;
}

void AcquireCpuBoost()
{
  if (appletGetAppletType() != AppletType_Application)
    return;

  std::lock_guard lock(s_mutex);
  if (s_boost_count++ != 0)
    return;

  const Result result = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
  s_boost_active = R_SUCCEEDED(result);
  SWITCH_DIAG_MARK("clocks: cpu boost on: 0x%x", result);
}

void ReleaseCpuBoost()
{
  if (appletGetAppletType() != AppletType_Application)
    return;

  std::lock_guard lock(s_mutex);
  if (s_boost_count == 0 || --s_boost_count != 0 || !s_boost_active)
    return;

  const Result result = appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  (void)result;
  SWITCH_DIAG_MARK("clocks: cpu boost off: 0x%x", result);
  s_boost_active = false;
}
}
