/* SPDX-License-Identifier: BSD-2-Clause */
/* Host build check only. No object from this file is linked into the init.
 * System headers are used solely to check the kernel interface declarations.
 */
#define _GNU_SOURCE
#include <asm/unistd.h>
#include <asm/termbits.h>
#include <asm/ioctls.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <stddef.h>
#include <sys/mount.h>
#include "abi.h"
#define CHECK(left, right) _Static_assert((unsigned long)(left) == (unsigned long)(right), #left " != " #right)
CHECK(D_IOCTL, __NR_ioctl);
CHECK(D_MKNODAT, __NR_mknodat);
CHECK(D_MKDIRAT, __NR_mkdirat);
CHECK(D_SYMLINKAT, __NR_symlinkat);
CHECK(D_MOUNT, __NR_mount);
CHECK(D_OPENAT, __NR_openat);
CHECK(D_CLOSE, __NR_close);
CHECK(D_GETDENTS64, __NR_getdents64);
CHECK(D_READ, __NR_read);
CHECK(D_WRITE, __NR_write);
CHECK(D_PPOLL, __NR_ppoll);
CHECK(D_EXIT, __NR_exit);
CHECK(D_NANOSLEEP, __NR_nanosleep);
CHECK(D_REBOOT, __NR_reboot);
CHECK(D_GETPID, __NR_getpid);
CHECK(D_AT_FDCWD, AT_FDCWD);
CHECK(D_O_WRONLY, O_WRONLY);
CHECK(D_O_RDWR, O_RDWR);
CHECK(D_O_DIRECTORY, O_DIRECTORY);
CHECK(D_O_NONBLOCK, O_NONBLOCK);
CHECK(D_O_NOCTTY, O_NOCTTY);
CHECK(D_MS_NOSUID, MS_NOSUID);
CHECK(D_MS_NODEV, MS_NODEV);
CHECK(D_TCGETS, TCGETS);
CHECK(D_TCSETS, TCSETS);
CHECK(D_FB_GET_VAR, FBIOGET_VSCREENINFO);
CHECK(D_FB_GET_FIX, FBIOGET_FSCREENINFO);
CHECK(sizeof(struct diag_pollfd), sizeof(struct pollfd));
CHECK(sizeof(struct diag_termios), sizeof(struct termios));
CHECK(offsetof(struct diag_termios, cc), offsetof(struct termios, c_cc));
CHECK(sizeof(struct fb_fix_screeninfo), 80);
CHECK(sizeof(struct fb_var_screeninfo), 160);
CHECK(offsetof(struct fb_fix_screeninfo, smem_start), 16);
CHECK(offsetof(struct fb_fix_screeninfo, line_length), 48);
