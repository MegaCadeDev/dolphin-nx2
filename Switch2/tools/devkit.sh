#!/bin/sh
# Runs a command inside the ounce-devkit Docker image, with the Dolphin-NS2
# project mounted at /work and the current directory preserved.
#   tools/devkit.sh make -j16
#   tools/devkit.sh sh -c 'source $DEVKITPRO/switch2vars.sh && aarch64-none-elf-cmake ...'
# Build the image first: docker build --network=host -t ounce-devkit external/ounce-devkit
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REL=${PWD#"$ROOT"}
exec docker run --rm --network=host \
    -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$ROOT:/work" -w "/work$REL" \
    "${DEVKIT_IMAGE:-ounce-devkit}" "$@"
