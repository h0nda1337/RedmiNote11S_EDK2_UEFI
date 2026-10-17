/* SPDX-License-Identifier: BSD-2-Clause */
/* This port's AArch64 Linux ABI definitions. abi_check.c compares these with
 * the installed cross-toolchain headers, without linking any library code.
 */
#ifndef MIEL_DIAG_ABI_H
#define MIEL_DIAG_ABI_H
typedef unsigned long usize;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
enum {
    D_IOCTL = 29, D_MKNODAT = 33, D_MKDIRAT = 34, D_SYMLINKAT = 36,
    D_MOUNT = 40, D_OPENAT = 56, D_CLOSE = 57, D_GETDENTS64 = 61,
    D_READ = 63, D_WRITE = 64, D_PPOLL = 73, D_EXIT = 93,
    D_NANOSLEEP = 101, D_REBOOT = 142, D_GETPID = 172,
    D_AT_FDCWD = -100, D_O_WRONLY = 1, D_O_RDWR = 2,
    D_O_NOCTTY = 0x100, D_O_NONBLOCK = 0x800, D_O_DIRECTORY = 0x4000,
    D_MS_NOSUID = 2, D_MS_NODEV = 4,
    D_TCGETS = 0x5401, D_TCSETS = 0x5402,
    D_FB_GET_VAR = 0x4600, D_FB_GET_FIX = 0x4602
};
struct diag_pollfd { int fd; short events, revents; };
struct diag_termios { u32 iflag, oflag, cflag, lflag; u8 line, cc[19]; };
static inline long call(long number, long a, long b, long c, long d, long e, long f)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x8 __asm__("x8") = number;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory");
    return x0;
}
#endif
