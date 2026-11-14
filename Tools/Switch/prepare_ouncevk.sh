#!/usr/bin/env bash
# Localize OunceVK's Vulkan entry points for Dolphin's PFN loader (Switch 2).
#   prepare_ouncevk.sh <output-dir>
set -euo pipefail

OUT="${1:?usage: $0 <output-dir>}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
BIN="${DEVKITPRO}/devkitA64/bin"
NM="${BIN}/aarch64-none-elf-nm"
OBJCOPY="${BIN}/aarch64-none-elf-objcopy"

# libouncevk-gl.a is one object with NVK, Zink and EGL, a superset of
# libouncevk.a. Linking it once gives Vulkan and EGL one shared driver.
SOURCE="${DEVKITPRO}/portlibs/switch2/lib/libouncevk-gl.a"
if [[ ! -f "${SOURCE}" ]]; then
  echo "OunceVK is missing: ${SOURCE}. Install ounce-devkit." >&2
  exit 1
fi

mkdir -p "${OUT}"
LOCAL_ARCHIVE="${OUT}/libouncevk_local.a"
PUBLIC_VK_SYMBOLS="${OUT}/ouncevk_public_symbols.txt"

# Dolphin's Vulkan loader stores entry points in global PFN variables named
# like Vulkan functions, so OunceVK's public vk* trampolines must go. Its ICD
# entry points are file-local; export them in their place, as the Switch 1
# Mesa SDK's localized NVK object does.
"${NM}" -g --defined-only "${SOURCE}" | awk '$NF ~ /^vk[A-Z]/ { print $NF }' | \
  LC_ALL=C sort -u > "${PUBLIC_VK_SYMBOLS}"
if [[ ! -s "${PUBLIC_VK_SYMBOLS}" ]]; then
  echo "OunceVK has no public entry points to localize" >&2
  exit 1
fi
"${OBJCOPY}" \
  --localize-symbols="${PUBLIC_VK_SYMBOLS}" \
  --globalize-symbol=vk_icdGetInstanceProcAddr \
  --globalize-symbol=vk_icdNegotiateLoaderICDInterfaceVersion \
  "${SOURCE}" "${LOCAL_ARCHIVE}.tmp"

for symbol in vk_icdGetInstanceProcAddr vk_icdNegotiateLoaderICDInterfaceVersion \
  eglGetDisplay; do
  if ! "${NM}" -g --defined-only "${LOCAL_ARCHIVE}.tmp" | \
    awk -v expected="${symbol}" '$NF == expected { found = 1 } END { exit !found }'; then
    echo "Localized OunceVK archive does not export ${symbol}" >&2
    exit 1
  fi
done
if "${NM}" -g --defined-only "${LOCAL_ARCHIVE}.tmp" | awk '$NF ~ /^vk[A-Z]/ { found = 1 } END { exit !found }'; then
  echo "Localized OunceVK archive still exports a public Vulkan entry point" >&2
  exit 1
fi

mv "${LOCAL_ARCHIVE}.tmp" "${LOCAL_ARCHIVE}"
echo "Localized OunceVK archive: ${LOCAL_ARCHIVE}"
