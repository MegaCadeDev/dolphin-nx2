# Dolphin for Switch 2 (kaiHEN): work in progress

This is dolphin-nx 1.2.2 (NaGaa95/dolphin-nx, f13fdd6) ported to Switch 2 homebrew with
kaiHEN and ounce-devkit (libounce 9382b1e, OunceVK f5afcd2, kaiHEN eb81e19).
All port changes are in the commit on top of f13fdd6 and are gated on `DOLPHIN_SWITCH2` / `__SWITCH2__`.

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
