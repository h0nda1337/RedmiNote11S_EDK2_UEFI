# SPDX-License-Identifier: BSD-2-Clause
"""Bounded checks for this port's static AArch64 PID 1, without executing it."""
import struct


def validate_init_elf(raw):
    if not 64 <= len(raw) <= 1024 * 1024 or raw[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("Expected a bounded ELF64 little-endian init")
    kind, machine, version = struct.unpack_from("<HHI", raw, 16)
    entry, phoff = struct.unpack_from("<QQ", raw, 24)
    ehsize, phsize, phcount = struct.unpack_from("<HHH", raw, 52)
    if (kind, machine, version, ehsize, phsize) != (2, 183, 1, 64, 56):
        raise ValueError("Expected a static AArch64 ET_EXEC with standard headers")
    if not 1 <= phcount <= 32 or phoff < 64 or phoff + phsize * phcount > len(raw):
        raise ValueError("Invalid ELF program table")
    loads = []
    for index in range(phcount):
        tag, flags, offset, address, _, filesz, memsz, alignment = struct.unpack_from(
            "<IIQQQQQQ", raw, phoff + index * phsize)
        if tag in (2, 3):
            raise ValueError("Init cannot require a dynamic loader")
        if tag == 0x6474e551 and flags & 1:
            raise ValueError("Init cannot require an executable stack")
        if tag != 1:
            continue
        # Linux 4.19 padzero() must be able to write the partial first BSS
        # page. The failed build had an unaligned, entirely file-less RW LOAD;
        # QEMU user mode accepted it, but the actual phone's exec returned -14.
        # Require real file bytes in every LOAD instead of relying on that case.
        if not filesz:
            raise ValueError("Every init LOAD must be file-backed, including BSS")
        if offset + filesz > len(raw) or filesz > memsz or memsz > 16 * 1024 * 1024:
            raise ValueError("Invalid ELF load bounds")
        if not address or address + memsz >= 1 << 48:
            raise ValueError("Invalid ELF address bounds")
        if alignment < 65536 or alignment & (alignment - 1) or (address - offset) % alignment:
            raise ValueError("Init LOAD must support 4K, 16K and 64K page alignment")
        if flags & 3 == 3 or flags & ~7 or memsz > filesz and not flags & 2:
            raise ValueError("Invalid init LOAD permissions")
        if any(address < old["address"] + old["memory_size"] and old["address"] < address + memsz
               for old in loads):
            raise ValueError("Overlapping init LOAD segments")
        loads.append({"address": address, "file_offset": offset, "file_size": filesz,
                      "memory_size": memsz, "flags": flags})
    if not any(item["flags"] & 1 and item["address"] <= entry < item["address"] + item["file_size"]
               for item in loads) or not any(item["flags"] & 2 for item in loads):
        raise ValueError("Init needs a file-backed entry and writable segment")
    return {"entry": entry, "loads": loads, "static_aarch64": True,
            "all_loads_file_backed": True, "physical_pid1_execution_proved": False}


if __name__ == "__main__":
    import json
    import sys
    from pathlib import Path
    print(json.dumps(validate_init_elf(Path(sys.argv[1]).read_bytes()), indent=2))
