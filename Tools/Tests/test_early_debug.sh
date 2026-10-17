#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"
pkg=Platform/Xiaomi/mielPkg
edk2=Common/edk2
out=Local/Build/Tests
mkdir -p "$out"
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$edk2/MdePkg/Include" -I"$edk2/MdePkg/Include/X64" -I"$pkg/Include" \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  "$pkg/Library/MielDebugLib/MielDebugLib.c" \
  "$pkg/Library/MielFrameBufferLib/MielFrameBufferLib.c" \
  Tools/Tests/test_early_debug.c -o "$out/test-early-debug"
"$out/test-early-debug"
