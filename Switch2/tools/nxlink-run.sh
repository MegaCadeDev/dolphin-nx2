#!/bin/sh
# Sends dolphin.nro to hbmenu's netloader and prints Dolphin's diagnostics log
# live (also saved to diag/live.log). On the console: open hbmenu, press Y.
# Usage: tools/nxlink-run.sh [console-ip]   (without an IP nxlink searches the LAN)
# Stop with Ctrl-C after Dolphin has exited.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NRO=external/dolphin-nx/build-switch2/Binaries/dolphin.nro
mkdir -p "$ROOT/diag"
docker run --rm -it --network=host -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT:/work" -w /work \
  "${DEVKIT_IMAGE:-ounce-devkit}" \
  nxlink ${1:+-a "$1"} -p switch/dolphin.nro -s "$NRO" | tee "$ROOT/diag/live.log"
