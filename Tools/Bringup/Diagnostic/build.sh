#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"
mkdir -p Local/Build/Diagnostic
aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror -c \
  Tools/Bringup/Diagnostic/abi_check.c -o Local/Build/Diagnostic/abi-check.o
aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
  -nostdlib -static -no-pie -Wl,-e,_start -Wl,--build-id=none \
  Tools/Bringup/Diagnostic/start.S Tools/Bringup/Diagnostic/abi_probe.c \
  -o Local/Build/Diagnostic/abi-probe
aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -mno-outline-atomics \
  -nostdlib -static -no-pie -Wl,-e,_start -Wl,--build-id=none \
  Tools/Bringup/Diagnostic/start.S Tools/Bringup/Diagnostic/init.c \
  -o Local/Build/Diagnostic/init
aarch64-linux-gnu-readelf -h -l Local/Build/Diagnostic/init > Local/Build/Diagnostic/elf.txt
python3 Tools/Bringup/init_elf.py Local/Build/Diagnostic/init > Local/Build/Diagnostic/elf-validation.json
if aarch64-linux-gnu-readelf -l Local/Build/Diagnostic/init | grep -q INTERP; then
  echo 'Unexpected interpreter' >&2; exit 1
fi
if test -n "$(aarch64-linux-gnu-nm -u Local/Build/Diagnostic/init)"; then
  echo 'Unresolved external dependency' >&2; exit 1
fi
set +e
qemu-aarch64 Local/Build/Diagnostic/init
status=$?
set -e
if test "$status" != 2; then echo 'PID 1 guard failed' >&2; exit 1; fi
echo 'Freestanding Linux readout built; non-PID-1 guard passed in QEMU user mode.'
