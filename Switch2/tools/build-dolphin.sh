#!/bin/sh
# Configures and builds dolphin-nx for Switch 2 inside the ounce-devkit image.
#   tools/build-dolphin.sh [Release|Debug]     (DIAGNOSTICS=OFF to drop the SD-card logs)
# Output: external/dolphin-nx/build-switch2/Binaries/dolphin.nro
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TYPE=${1:-Release}
JOBS=${JOBS:-$(nproc)}
cd "$ROOT/external/dolphin-nx"
exec "$ROOT/tools/devkit.sh" bash -c "
  set -e
  export PATH=\$DEVKITPRO/tools/bin:\$PATH
  cmake -S . -B build-switch2 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=CMake/Toolchain-Switch2.cmake \
    -DCMAKE_BUILD_TYPE=$TYPE \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DENABLE_LTO=OFF \
    -DDOLPHIN_SWITCH_DIAGNOSTICS=${DIAGNOSTICS:-ON}
  cmake --build build-switch2 --target dolphin_nro --parallel $JOBS
"
