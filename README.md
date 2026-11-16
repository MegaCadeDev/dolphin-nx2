# Dolphin for Switch 2 (kaiHEN)

An unofficial port of [Dolphin](https://dolphin-emu.org/), the GameCube and Wii emulator, to
Nintendo Switch 2 homebrew. It is dolphin-nx 1.2.2 (NaGaa95/dolphin-nx, f13fdd6) rebuilt
with kaiHEN and ounce-devkit (libounce 9382b1e, OunceVK f5afcd2, kaiHEN eb81e19). It is a
work in progress: expect bugs, and report them here, not to the Dolphin or dolphin-nx projects.

All port changes are in the commits on top of f13fdd6 and are gated on
`DOLPHIN_SWITCH2` / `__SWITCH2__`. Dolphin's own readme is in
[Readme-Dolphin.md](Readme-Dolphin.md).

## Installing

1. Run kaiHEN on your Switch 2.
2. Copy `dolphin.nro` to `sdmc:/switch/`.
3. Start it from hbmenu.

Games can be on the SD card or on an SMB network share. USB drives are not supported on
Switch 2 (see below).

## Playing

- **In-game menu:** press **L + R + Plus**. It has save states, disc changes, cheats,
  controller modes, screen layout, Show FPS, VBI Skip and **Write-back cache (slow)**. A few
  games need the write-back cache. It is applied at once and saved for that game only.
- **Wii pointer:** point the controller at the middle of the screen and press **Y** to
  recenter it. You can change the button under Controller settings → Pointer recenter.
- **Motion:** Joy-Con and Pro Controller motion works for the Wii Remote and Nunchuk, with the
  Joy-Con attached or detached.
- **Button layout:** the default layout can be changed in the launcher under Settings →
  Controller / Input. Interactive mapping, Wii Remote mapping and Extension mapping are
  press-to-bind screens. A game's own settings can give it its own layout, and Reset Switch
  defaults restores the default.
- **Remembered per game:** the controller mode and the disc you last chose in the in-game menu.

## Known issues

- USB drives are not supported yet: they broke hbmenu after Dolphin exited.
- Fastmem is not available under kaiHEN, so CPU-heavy scenes can slow down (see Performance).
- NVK is not a conformant Vulkan driver, so some games may show graphics problems.

## Building

The build scripts are in [Switch2/tools](Switch2/tools). They run in the ounce-devkit
container and expect a project folder with this repository at `external/dolphin-nx`, next to
`external/ounce-devkit`, `external/kaihen` and a copy of `Switch2/tools` at `tools/`. From that
project folder:

```sh
tools/build-dolphin.sh            # Release, with the SD-card diagnostics logs
DIAGNOSTICS=OFF tools/build-dolphin.sh
```

The output is `external/dolphin-nx/build-switch2/Binaries/dolphin.nro`.

## Credits

This port is a small layer on other people's work. Thanks to everyone below.

- **[Dolphin](https://github.com/dolphin-emu/dolphin)**, the GameCube and Wii emulator, by the
  Dolphin Emulator Project and its contributors (GPLv2+).
- **[dolphin-nx](https://github.com/NaGaa95/dolphin-nx)** by **NaGaa95**: the Switch port of
  Dolphin that this builds on. That includes the Switch frontend and launcher, the Switch
  memory arena and JIT support, and the forwarder.
- **[kaiHEN](https://github.com/ouncebrew/kaihen)** by the **kaiHEN contributors (ouncebrew)**,
  with thanks to its private beta testers and the r/switch2hacks Discord: the Switch 2 homebrew
  environment, with RO's JIT publishing that Dolphin's JIT uses.
- **[ounce-devkit](https://github.com/ouncebrew/ounce-devkit)**,
  **[libounce](https://github.com/ouncebrew/libounce)** and
  **[OunceVK](https://github.com/ouncebrew/ouncevk)** by **ouncebrew**: the Switch 2 toolchain,
  libnx for Switch 2, and Vulkan/OpenGL (built on Mesa's NVK and Zink).
- **[ounce-hbloader](https://github.com/ouncebrew/ounce-hbloader)** and
  **[ounce-hbmenu](https://github.com/ouncebrew/ounce-hbmenu)**: ouncebrew's Switch 2 builds of
  switchbrew's nx-hbloader and nx-hbmenu.
- **[switchbrew](https://github.com/switchbrew)**, for libnx (which libounce is based on),
  nx-hbloader, nx-hbmenu and nxlink.
- **[devkitPro](https://devkitpro.org/)**, for devkitA64, the tools and the Switch portlibs
  (SDL 2, curl and others).
- **[Mesa](https://mesa3d.org/)**, for NVK and Zink, which OunceVK is built on.
- **[libusbhsfs](https://github.com/DarkMatterCore/libusbhsfs)** by **DarkMatterCore** (used
  through [ITotalJustice's fork](https://github.com/ITotalJustice/libusbhsfs)), and
  **[libsmb2](https://github.com/sahlberg/libsmb2)** by **Ronnie Sahlberg** (through
  [ITotalJustice's fork](https://github.com/ITotalJustice/libsmb2)): dolphin-nx's USB and SMB
  storage.
- **[Kaizhar](https://github.com/wowilookgood/kaizhar)** by **wowilookgood**, the Switch 2 port
  of [Azahar](https://github.com/azahar-emu/azahar). `ExitCleanup.cpp` uses Kaizhar's approach to
  exiting cleanly under kaiHEN: track threads with `--wrap=__syscall_thread_create`, then
  pause leftover threads and unmap their stacks at exit.
- **[retroarch-s2](https://github.com/NerdsMcGee/retroarch-s2)** by **NerdsMcGee** (a port of
  [RetroArch](https://github.com/libretro/RetroArch) by libretro) and
  **[mGBA](https://github.com/wowilookgood/mgba)** for Switch 2 by **wowilookgood** (a port of
  [mGBA](https://github.com/mgba-emu/mgba) by endrift): their exit and platform code was
  studied for this port.
- [Porpoise](https://github.com/PalindromicBreadLoaf/nezumiiruka) by nezumiiruka: their port of Dolphin to the Nintendo Switch was used as the base for this project to be recompiled to Switch 2.

Each project keeps its own license. Dolphin and dolphin-nx are GPLv2+, kaiHEN is
GPL-2.0-only (libkaihen is ISC), and the other licenses are in each project's repository.
This port is unofficial: report problems here, not to the Dolphin or dolphin-nx projects.

## Fixed: hbmenu was black after Dolphin exits (USB host storage)

After Dolphin exited, by HOME → Close or by its own Exit option, the next hbmenu
launch showed a black screen until kaiHEN was injected again. vulkan-clear did not
cause it.

**Cause: Dolphin's USB drive support** (libusbhsfs, which opens `usb:hs` and claims
mass-storage interfaces). It was narrowed down on hardware, with only the launcher
opened and then closed:

| USB drives | Network (bsd:u, ssl, csrng, nifm, sfdnsres) | Frontend audio (aud:u) | hbmenu afterwards |
|---|---|---|---|
| on  | on  | on  | black |
| off | off | off | works |
| on  | off | off | black |
| off | on  | on  | works |

Dolphin's own exit is clean in every case. The diagnostics build logs every SM
lookup and every thread (`ExitCleanup.cpp`): every lookup was answered, no thread
was left running, and the exit reached `appletUnlockExit`. HOME → Close, which ends
the process, breaks hbmenu as well. So the state that breaks the next program
outlives Dolphin's process. It is in the system's USB host service or in kaiHEN,
which doesn't seem to reset it when the process ends.

For the kaiHEN side: HEN itself survives. Holding R still starts hbloader, and only
hbmenu stays black.

Workaround in this port: `Storage::InitializeUsb` returns false on `__SWITCH2__`
(`Source/Core/DolphinSwitch/Storage.cpp`), so libusbhsfs is no longer started.
None of the other Switch 2 homebrew checked (RetroArch, Kaizhar, mGBA) use USB host
either.

Diagnostics test switches (empty files in `sdmc:/switch/dolphin/diag/`):
`no-network`, `no-audio`, `no-fastmem` and `cpu-boost` each leave that part off.

## Performance and motion (2026-11-16)

Measured in Wii Sports Resort table tennis with the diagnostics build, whose
`boot.log` gets a `perf:` line (speed, max speed, FPS, VPS) and a `jit:` line
(publish requests to kaiHEN) every 10 seconds.

- **JIT publishing is batched.** Every code change used to be its own request to
  kaiHEN's RO. Now the changes made in one write scope (a compiled block with its
  links, a backpatch, the blocks one invalidation destroys) go out as one request
  when the scope ends (`Source/Core/Common/MemoryUtil.cpp`). In normal play, waiting on kaiHEN
  is now under 1% of the time.
- **CPU boost stays on for the whole game** on Switch 2, not just while it boots.
  The worst dips went from 60-78% speed to 80-89%. The test switch `cpu-boost`
  turns it off.
- **Fastmem is not available under kaiHEN.** The process is not granted
  svcSetProcessMemoryPermission (0x73), svcMapProcessMemory (0x74),
  svcUnmapProcessMemory (0x75), svcMapProcessCodeMemory (0x77) or
  svcUnmapProcessCodeMemory (0x78), so the JIT uses its page table lookup
  instead. The remaining dips are CPU-bound (speed equals max speed). Fastmem
  needs kaiHEN to grant those svcs, or to offer a service that maps the
  emulated memory at its fastmem addresses.
- **Wireless motion is steadier** (`Source/Core/InputCommon/ControllerInterface/Switch/Switch.cpp`).
  A missed motion sample used to stop the sensor and read as free fall for a
  second. Now a gap keeps the last reading (the gyroscope reads still after
  100 ms), the sensor only restarts after 500 ms without samples, and all samples
  since the last read are averaged.

## Files in Switch2/

- [`PORTING_PLAN.md`](Switch2/PORTING_PLAN.md): port notes and status.
- [`diag-latest/`](Switch2/diag-latest): `sdmc:/switch/dolphin/diag/` copied from the console
  after the test runs.
- [`tools/`](Switch2/tools): the build scripts used. `nxlink-run.sh` sends the NRO through
  hbmenu's netloader and streams the diagnostics log live.
- [`sd/switch/dolphin/diag/`](Switch2/sd/switch/dolphin/diag): the test switch files.
