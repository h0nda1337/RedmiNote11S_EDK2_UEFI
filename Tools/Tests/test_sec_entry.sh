#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# System emulation of the original SEC entry only, not an MIEL or UEFI boot.
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"
edk2="$root/Common/edk2"
mkdir -p Local/Build/SecEntry
cat > Local/Build/SecEntry/test.ld <<'EOF'
ENTRY(TestImageStart)
SECTIONS {
  . = 0x40080000;
  .text : { KEEP(*(.test_header)) *(.text.MielSecEntry) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  . = ALIGN(4096);
  TestImageEnd = .;
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
EOF
common=(-Wall -Wextra -Werror -O2 -ffreestanding -fno-builtin -fno-stack-protector -fshort-wchar
  -fno-pic -fno-pie -ffixed-x18 -mgeneral-regs-only -mstrict-align
  -I"$edk2/MdePkg/Include" -I"$edk2/MdePkg/Include/AArch64" -IPlatform/Xiaomi/mielPkg/Include)
aarch64-linux-gnu-gcc "${common[@]}" -c Platform/Xiaomi/mielPkg/Sec/AArch64/Entry.S -o Local/Build/SecEntry/entry.o
aarch64-linux-gnu-gcc "${common[@]}" -c Tools/Tests/test_sec_header.S -o Local/Build/SecEntry/header.o
for el in 1 2; do
  aarch64-linux-gnu-gcc "${common[@]}" -DTEST_EXPECTED_EL="$el" \
    -c Tools/Tests/test_sec_entry.c -o "Local/Build/SecEntry/harness-el$el.o"
  aarch64-linux-gnu-ld -T Local/Build/SecEntry/test.ld Local/Build/SecEntry/header.o \
    Local/Build/SecEntry/entry.o "Local/Build/SecEntry/harness-el$el.o" -o "Local/Build/SecEntry/test-el$el.elf"
  aarch64-linux-gnu-objcopy -O binary "Local/Build/SecEntry/test-el$el.elf" "Local/Build/SecEntry/test-el$el.img"
  virtualization=off
  if [[ "$el" == 2 ]]; then virtualization=on; fi
  timeout 15 qemu-system-aarch64 -machine "virt,virtualization=$virtualization" \
    -cpu cortex-a57 -m 512M -display none -serial none -monitor none -no-reboot \
    -semihosting-config enable=on,target=native -kernel "Local/Build/SecEntry/test-el$el.img" \
    > "Local/Build/SecEntry/qemu-el$el.txt" 2>&1
  grep -q '^PASS original SEC entry' "Local/Build/SecEntry/qemu-el$el.txt"
  cat "Local/Build/SecEntry/qemu-el$el.txt"
done
printf '%s\n' 'QEMU system EL1 and EL2 entry fixtures passed. No DXE/Shell or phone execution demonstrated.'
