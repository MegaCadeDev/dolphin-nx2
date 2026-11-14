#!/bin/sh
# Turns the dolphin.elf+0x... offsets in a crash.log into functions and source lines.
#   tools/symbolize.sh path/to/crash.log
# Uses the dolphin.elf from the same build as the dolphin.nro that crashed.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOG=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
case "$LOG" in "$ROOT"/*) ;; *) cp "$LOG" "$ROOT/crash.log"; LOG="$ROOT/crash.log" ;; esac
REL_LOG=${LOG#"$ROOT/"}
cd "$ROOT"
exec tools/devkit.sh sh -c "
  grep -o 'dolphin.elf+0x[0-9a-f]*' '$REL_LOG' | while read -r ref; do
    printf '%-26s ' \"\$ref\"
    /opt/devkitpro/devkitA64/bin/aarch64-none-elf-addr2line -f -C -i -p \
      -e external/dolphin-nx/build-switch2/Binaries/dolphin.elf \"\${ref#dolphin.elf+}\" | head -3 | tr '\n' ' '
    echo
  done
"
