#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Host build only; no ADB, Fastboot or device operations.
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
edk2="$root/Common/edk2"
mkdir -p "$root/Local/Build/Native"
export WORKSPACE="$edk2" PACKAGES_PATH="$root/Platform/Xiaomi"
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
export SOURCE_DATE_EPOCH="$(git -C "$edk2" show -s --format=%ct HEAD)"
export MAKEFLAGS=-j2
extra=()
mode=integration
case "${MIEL_PROBE:-integration}" in
  integration) ;;
  entry) mode=entry; extra=(-D MIEL_ENTRY_PROBE=TRUE) ;;
  mmu) mode=mmu; extra=(-D MIEL_MMU_PROBE=TRUE) ;;
  *) echo 'MIEL_PROBE must be integration, entry or mmu' >&2; exit 1 ;;
esac
cd "$edk2"
set +u
source edksetup.sh BaseTools > /dev/null
set -u
log="$root/Local/Build/Native/$mode.log"
if ! build -n 4 -a AARCH64 -t GCC -b RELEASE -p mielPkg/MielNative.dsc \
    -D "MIEL_BUILD_ROOT=$root/Local/Build/MielNative" "${extra[@]}" > "$log" 2>&1; then
  tail -n 25 "$log"
  exit 1
fi
grep -q '^- Done -' "$log"
mkdir -p "$root/Local/Build/Native/$mode"
cp -- "$root/Local/Build/MielNative/RELEASE_GCC/FV/MIELNATIVE.fd" "$root/Local/Build/Native/$mode/MIELNATIVE.fd"
echo "Miel native $mode compiled on the host; device execution remains unverified."
