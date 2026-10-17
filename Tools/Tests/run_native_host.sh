#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Host-only checks of the owner's libraries; no device operations.
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"
edk2="$root/Common/edk2"
pkg=Platform/Xiaomi/mielPkg
out=Local/Build/Tests
mkdir -p "$out"
bash Tools/Tests/build_handoff_lib.sh
flags=(-std=c11 -Wall -Wextra -Werror -fshort-wchar -O1 -g
  -fsanitize=address,undefined -fno-omit-frame-pointer
  -I"$edk2/MdePkg/Include" -I"$edk2/MdePkg/Include/X64" -I"$pkg/Include")
gcc "${flags[@]}" "$pkg/Library/MielMemoryPlanLib/MielMemoryPlanLib.c" \
  "$pkg/Library/MielHandoffLib/MielHandoffLib.c" \
  "$pkg/Library/MielFdtLib/MielFdtLib.c" \
  "$pkg/Library/MielPlatformLib/MielPlatformLib.c" \
  Tools/Tests/test_memory_plan.c -o "$out/test-memory-plan"
"$out/test-memory-plan"
gcc "${flags[@]}" "$pkg/Library/MielFrameBufferLib/MielFrameBufferLib.c" \
  Tools/Tests/test_framebuffer.c -o "$out/test-framebuffer"
"$out/test-framebuffer"

bash Tools/Tests/test_early_debug.sh
