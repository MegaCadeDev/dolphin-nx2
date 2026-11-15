# Dolphin for Switch 2 (kaiHEN): work in progress

This is dolphin-nx 1.2.2 (NaGaa95/dolphin-nx, f13fdd6) ported to Switch 2 homebrew with
kaiHEN and ounce-devkit (libounce 9382b1e, OunceVK f5afcd2, kaiHEN eb81e19).
All port changes are in the commits on top of f13fdd6 and are gated on `DOLPHIN_SWITCH2` / `__SWITCH2__`.

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
`no-network`, `no-audio` and `no-fastmem` each leave that part off.

## Files here

- `PORTING_PLAN.md`: port notes and status.
- `diag-latest/`: `sdmc:/switch/dolphin/diag/` copied from the console after the test runs.
- `tools/`: the build scripts used (they expect the project layout `tools/` next to
  `external/dolphin-nx`, `external/ounce-devkit` and `external/kaihen`).
  `nxlink-run.sh` sends the NRO through hbmenu's netloader and streams the diagnostics log live.
- `sd/switch/dolphin/diag/`: the test switch files.
