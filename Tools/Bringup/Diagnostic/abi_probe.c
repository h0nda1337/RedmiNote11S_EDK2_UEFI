/* SPDX-License-Identifier: BSD-2-Clause */
/* Read-only ordinary-process check on the real AArch64 kernel. Not PID 1.
 * No mount, USB configuration, reboot, block-device or framebuffer operation.
 */
#include "abi.h"
int diag_main(void)
{
    u8 entries[4096];
    const char expected[] = "musb-hdrc";
    const char message[] = "MIEL ABI directory probe OK\n";
    long fd, count;
    usize offset = 0;
    if (call(D_GETPID, 0, 0, 0, 0, 0, 0) == 1) return 2;
    fd = call(D_OPENAT, D_AT_FDCWD, (long)"/sys/class/udc", D_O_DIRECTORY, 0, 0, 0);
    if (fd < 0) return 10;
    count = call(D_GETDENTS64, fd, (long)entries, sizeof(entries), 0, 0, 0);
    call(D_CLOSE, fd, 0, 0, 0, 0, 0);
    if (count <= 0) return 11;
    while (offset + 20 <= (usize)count) {
        u16 record = (u16)entries[offset+16] | ((u16)entries[offset+17] << 8);
        usize i = 0;
        if (record < 20 || offset + record > (usize)count) return 12;
        while (19+i < record && i < sizeof(expected) && entries[offset+19+i] == (u8)expected[i]) {
            if (!expected[i]) {
                return call(D_WRITE, 1, (long)message, sizeof(message)-1, 0, 0, 0) == sizeof(message)-1 ? 0 : 13;
            }
            i++;
        }
        offset += record;
    }
    return 14;
}
