#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"
edk2="$root/Common/edk2"
mkdir -p Local/Build/Tests/handoff
cat > Local/Build/Tests/handoff/synthetic.dts <<'EOF'
/dts-v1/;
/memreserve/ 0x48000000 0x1000;
/ {
  compatible = "mediatek,MT6781";
  #address-cells = <2>;
  #size-cells = <2>;
  memory@40000000 {
    device_type = "memory";
    status = "okay";
    reg = <0 0x40000000 1 0x80000000>;
  };
  chosen {
    atag,videolfb = [00 00 00 90 00 00 00 00 01 00 00 00 70 17 00 00 00 00 00 02];
    atag,videolfb-fb_base_h = <0>;
    atag,videolfb-fb_base_l = <0xa0000000>;
    atag,videolfb-vramSize = <0x1000000>;
    linux,initrd-start = <0 0x51c00000>;
    linux,initrd-end = <0 0x51d00000>;
  };
  reserved-memory {
    #address-cells = <2>;
    #size-cells = <2>;
    ranges;
    framebuffer@90000000 { reg = <0 0x90000000 0 0x2000000>; no-map; };
    secure@78800000 { reg = <0 0x78800000 0 0x5800000>; no-map; };
    // LK's observed SSPM status is four bytes, not a terminated DTS string.
    reserve-memory-sspm_share {
      status = [6f 6b 61 79];
      reg = <0 0x57400000 0 0x100000>;
      no-map;
    };
    dynamic { size = <0 0x100000>; no-map; };
    unresolved { no-map; };
    placeholder { };
  };
  psci { compatible = "arm,psci-1.0"; method = "smc"; };
};
EOF
dtc -I dts -O dtb Local/Build/Tests/handoff/synthetic.dts -o Local/Build/Tests/handoff/synthetic.dtb
source=Platform/Xiaomi/mielPkg/Library/MielHandoffLib/MielHandoffLib.c
common=(-std=c11 -Wall -Wextra -Werror -fshort-wchar
  -I"$edk2/MdePkg/Include" -IPlatform/Xiaomi/mielPkg/Include)
gcc "${common[@]}" -I"$edk2/MdePkg/Include/X64" -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer "$source" \
  Platform/Xiaomi/mielPkg/Library/MielFdtLib/MielFdtLib.c \
  Platform/Xiaomi/mielPkg/Library/MielPlatformLib/MielPlatformLib.c \
  Tools/Tests/test_handoff.c -o Local/Build/Tests/handoff/test-handoff
args=(Local/Build/Tests/handoff/synthetic.dtb)
if [[ "$#" == 1 ]]; then args+=("$1"); fi
Local/Build/Tests/handoff/test-handoff "${args[@]}"
aarch64-linux-gnu-gcc "${common[@]}" -I"$edk2/MdePkg/Include/AArch64" \
  -O2 -ffreestanding -fno-stack-protector -fno-builtin -mstrict-align \
  -mgeneral-regs-only -c "$source" -o Local/Build/Tests/handoff/MielHandoffLib.o
printf '%s\n' 'Original handoff library compiled for ARM64. Dynamic reservations still require policy; no UEFI memory map generated.'
