# Dolphin for Switch 2 (kaiHEN): work in progress

This is dolphin-nx 1.2.2 (NaGaa95/dolphin-nx, f13fdd6) ported to Switch 2 homebrew with
kaiHEN and ounce-devkit (libounce 9382b1e, OunceVK f5afcd2, kaiHEN eb81e19).
All port changes are in the commit on top of f13fdd6 and are gated on `DOLPHIN_SWITCH2` / `__SWITCH2__`.

## Open problem: hbmenu is black after Dolphin exits

Dolphin starts, runs games and shuts down cleanly. After it exits, the next hbmenu launch shows a
black screen, and kaiHEN has to be injected again. This happens with both ways of leaving:

- HOME → Close Software. The process ends at `appletUnlockExit`, as for any application.
- Dolphin's own Exit option, which returns from `main` to hbloader and then hbmenu.

vulkan-clear (a small OunceVK sample) exits and returns to hbmenu without this problem.

What the exit already does (see `Source/Core/DolphinSwitch/Main.cpp` and `ExitCleanup.cpp`):

- All of Dolphin's shutdown steps complete. The JIT regions are closed, and the fastmem views and
  code alias are unmapped (`Source/Core/Common/MemArenaSwitch.cpp`).
- `ExitCleanup.cpp` wraps `__syscall_thread_create` to track every pthread. At exit it waits for
  threads that are still running, pauses any that remain, and unmaps the stack mirrors of
  unjoined threads. This follows Kaizhar's approach.
- The diagnostics build logs the process memory map before the exit lock is released. The last
  HOME-close run (`diag-latest/boot.log`) shows the NRO image, the hbloader backing heap
  (type 0x05, attr 0x1) and one 64 KiB stack mirror (type 0x0B) that was not yet cleaned up at
  that point.

Not yet known: whether anything outside the process (kaiHEN's GameArm re-arm, the code mount, or
GPU state left by OunceVK/NVK) is what hbmenu is missing.

## Files here

- `PORTING_PLAN.md`: port notes and status.
- `diag-latest/`: the latest `sdmc:/switch/dolphin/diag/` copied from the console.
- `tools/`: the build scripts used (they expect the project layout `tools/` next to
  `external/dolphin-nx`, `external/ounce-devkit` and `external/kaihen`).
  `nxlink-run.sh` sends the NRO through hbmenu's netloader and streams the diagnostics log live.
- `sd/switch/dolphin/diag/no-fastmem`: copy this to the SD card to turn fastmem off for a test run.
