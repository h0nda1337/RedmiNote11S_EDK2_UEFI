/* SPDX-License-Identifier: BSD-2-Clause */
/* Original freestanding Linux PID 1 readout for miel/MT6781. Not UEFI.
 * Direct AArch64 syscalls: no libc, external init or external recovery payload.
 * Only virtual filesystems/configfs and character devices are used.
 * No block devices or persistent filesystems are opened or mounted.
 * USB readout design follows the owner's OppoReno5Lite_EDK2_UEFI diagnostic;
 * this implementation and MIEL protocol are specific to this port.
 */
#include "abi.h"

void *memset(void *target, int value, usize size)
{
    u8 *p = target;
    while (size--) *p++ = (u8)value;
    return target;
}
void *memcpy(void *target, const void *source, usize size)
{
    u8 *p = target;
    const u8 *s = source;
    while (size--) *p++ = *s++;
    return target;
}
static usize length(const char *s) { usize n = 0; while (s[n]) n++; return n; }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int log_fd = -1;
static void log_value(const char *label, long value);
static int open_file(const char *p, long flags) { return (int)call(D_OPENAT, D_AT_FDCWD, (long)p, flags, 0, 0, 0); }
static void close_file(int fd) { if (fd >= 0) call(D_CLOSE, fd, 0, 0, 0, 0, 0); }
static long read_file(int fd, void *p, usize n) { return call(D_READ, fd, (long)p, n, 0, 0, 0); }
static void directory(const char *p) { call(D_MKDIRAT, D_AT_FDCWD, (long)p, 0755, 0, 0, 0); }
static void sleep_second(void) { long t[2] = {1, 0}; call(D_NANOSLEEP, (long)t, 0, 0, 0, 0, 0); }
static void mount_fs(const char *type, const char *target)
{
    long flags = equal(type, "devtmpfs") ? D_MS_NOSUID : D_MS_NOSUID | D_MS_NODEV;
    call(D_MOUNT, (long)type, (long)target, (long)type, flags, 0, 0);
}
static int put(const char *p, const char *text)
{
    int fd = open_file(p, D_O_WRONLY);
    long n;
    if (fd < 0) return -1;
    n = call(D_WRITE, fd, (long)text, length(text), 0, 0, 0);
    close_file(fd);
    return n == (long)length(text) ? 0 : -1;
}
static long read_path(const char *p, void *buffer, usize limit)
{
    int fd = open_file(p, 0);
    usize total = 0;
    long n;
    if (fd < 0) return -1;
    while (total < limit) {
        n = read_file(fd, (u8 *)buffer + total, limit - total);
        if (n < 0) { close_file(fd); return -1; }
        if (!n) { close_file(fd); return (long)total; }
        total += (usize)n;
    }
    /* Refuse a file whose size might exceed the protocol bound. */
    close_file(fd);
    return -1;
}
static int battery(void)
{
    char text[16] = {0};
    int result = 0, i = 0;
    long n = read_path("/sys/class/power_supply/battery/capacity", text, sizeof(text)-1);
    if (n <= 0) return -1;
    while (text[i] >= '0' && text[i] <= '9') result = result * 10 + text[i++] - '0';
    if (!i || result > 100 || (text[i] && text[i] != '\n')) return -1;
    return result;
}
static int node(const char *source, const char *target)
{
    char text[32] = {0};
    unsigned major = 0, minor = 0, i = 0;
    if (read_path(source, text, sizeof(text)-1) <= 0) return -1;
    while (text[i] >= '0' && text[i] <= '9') major = major * 10 + text[i++] - '0';
    if (text[i++] != ':') return -1;
    while (text[i] >= '0' && text[i] <= '9') minor = minor * 10 + text[i++] - '0';
    if (major > 4095 || minor > 1048575) return -1;
    call(D_MKNODAT, D_AT_FDCWD, (long)target, 0020600, (minor & 255) | ((usize)major << 8) | ((usize)(minor & ~255U) << 12), 0, 0);
    return 0;
}

#define G "/config/usb_gadget/miel_diag"
static int setup_usb(void)
{
    u8 entries[4096];
    char controller[128] = {0};
    long n;
    int fd;
    usize offset = 0;
    directory(G);
    if (put(G "/idVendor", "0x1d6b") || put(G "/idProduct", "0x0104") || put(G "/bDeviceClass", "2")) return -10;
    directory(G "/strings/0x409");
    if (put(G "/strings/0x409/serialnumber", "MIELUEFIDIAG") ||
        put(G "/strings/0x409/manufacturer", "h0nda1337") ||
        put(G "/strings/0x409/product", "MIEL Linux readout")) return -11;
    directory(G "/configs/c.1");
    directory(G "/configs/c.1/strings/0x409");
    put(G "/configs/c.1/strings/0x409/configuration", "Readout ACM");
    put(G "/configs/c.1/MaxPower", "500");
    directory(G "/functions/acm.usb0");
    call(D_SYMLINKAT, (long)(G "/functions/acm.usb0"), D_AT_FDCWD, (long)(G "/configs/c.1/acm.usb0"), 0, 0, 0);
    fd = open_file("/sys/class/udc", D_O_DIRECTORY);
    if (fd < 0) return -12;
    n = call(D_GETDENTS64, fd, (long)entries, sizeof(entries), 0, 0, 0);
    close_file(fd);
    log_value("udc-getdents", n);
    while (n > 0 && offset + 20 <= (usize)n) {
        u16 record = (u16)entries[offset+16] | ((u16)entries[offset+17] << 8);
        char *name = (char *)entries + offset + 19;
        usize size;
        if (record < 20 || offset + record > (usize)n) return -13;
        size = 0;
        while (19 + size < record && name[size]) size++;
        if (name[0] != '.' && size && size < sizeof(controller) && 19+size < record) {
            memcpy(controller, name, size);
            break;
        }
        offset += record;
    }
    if (!controller[0] || put(G "/UDC", controller)) return -14;
    log_value("udc-bound", 0);
    put("/sys/class/udc/musb-hdrc/device/cmode", "1");
    log_value("usb-role-requested", 0);
    return node("/sys/class/tty/ttyGS0/dev", "/dev/ttyGS0");
}

static int wait_io(int fd, short events)
{
    struct diag_pollfd p = {fd, events, 0};
    long t[2] = {1, 0};
    return call(D_PPOLL, (long)&p, 1, (long)t, 0, 0, 0) > 0 && (p.revents & events);
}
static int send_bytes(int fd, const void *data, usize size)
{
    const u8 *p = data;
    while (size) {
        long n;
        if (!wait_io(fd, 4)) return -1;
        n = call(D_WRITE, fd, (long)p, size, 0, 0, 0);
        if (n == -4 || n == -11) continue;
        if (n <= 0) return -1;
        p += n;
        size -= (usize)n;
    }
    return 0;
}
static char *decimal(char *p, usize value)
{
    char reverse[24];
    int n = 0;
    do { reverse[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = reverse[--n];
    return p;
}
static void log_value(const char *label, long value)
{
    char text[128], *p = text;
    usize size = length(label);
    if (log_fd < 0 || size > 64) return;
    memcpy(p, "<6>miel-diag: ", 14); p += 14;
    memcpy(p, label, size); p += size; *p++ = '=';
    if (value < 0) { *p++ = '-'; value = -value; }
    p = decimal(p, (usize)value); *p++ = '\n';
    call(D_WRITE, log_fd, (long)text, (long)(p-text), 0, 0, 0);
}
static int send_data(int fd, const char *name, const void *data, usize size)
{
    char header[96], *p = header;
    memcpy(p, "FILE ", 5); p += 5;
    memcpy(p, name, length(name)); p += length(name); *p++ = ' ';
    p = decimal(p, size); *p++ = '\n';
    if (send_bytes(fd, header, (usize)(p-header)) || send_bytes(fd, data, size)) return -1;
    return send_bytes(fd, "\n", 1);
}
static u8 file_buffer[2 * 1024 * 1024];
/* Keep the writable LOAD file-backed. With only an unaligned, zero-file-size
 * BSS segment, this phone's Linux 4.19 ELF loader cannot zero its first page:
 * exec fails with -EFAULT before _start. The initialized phase also provides
 * a small debugger-visible progress indicator without persistent writes.
 */
static volatile u32 diagnostic_stage = 1;
static int snapshot(int fd)
{
    const char *paths[] = {"/sys/firmware/fdt", "/proc/iomem", "/proc/cmdline", "/sys/class/power_supply/battery/capacity"};
    const char *names[] = {"runtime.dtb", "iomem.txt", "cmdline.txt", "battery.txt"};
    u8 fix[80] = {0}, var[160] = {0};
    int i, fb;
    if (send_bytes(fd, "MIEL-DIAG/1 BEGIN\n", 18)) return -1;
    for (i = 0; i < 4; i++) {
        long n = read_path(paths[i], file_buffer, sizeof(file_buffer));
        if (send_data(fd, names[i], file_buffer, n < 0 ? 0 : (usize)n)) return -1;
    }
    node("/sys/class/graphics/fb0/dev", "/dev/fb0");
    fb = open_file("/dev/fb0", 0);
    if (fb >= 0) {
        if (call(D_IOCTL, fb, 0x4602, (long)fix, 0, 0, 0) < 0) memset(fix, 0, sizeof(fix));
        if (call(D_IOCTL, fb, 0x4600, (long)var, 0, 0, 0) < 0) memset(var, 0, sizeof(var));
        close_file(fb);
    }
    if (send_data(fd, "fb-fix.bin", fix, sizeof(fix)) || send_data(fd, "fb-var.bin", var, sizeof(var))) return -1;
    return send_bytes(fd, "MIEL-DIAG/1 END\n", 16);
}
int diag_main(void)
{
    int i, fd = -1;
    char command[32] = {0};
    usize used = 0;
    struct diag_termios term = {0};
    if (call(D_GETPID, 0, 0, 0, 0, 0, 0) != 1) return 2;
    diagnostic_stage = 2;
    directory("/dev"); directory("/proc"); directory("/sys"); directory("/config");
    mount_fs("devtmpfs", "/dev"); mount_fs("proc", "/proc");
    mount_fs("sysfs", "/sys"); mount_fs("configfs", "/config");
    node("/sys/class/mem/kmsg/dev", "/dev/kmsg");
    log_fd = open_file("/dev/kmsg", D_O_WRONLY);
    log_value("pid1-entered", call(D_GETPID, 0, 0, 0, 0, 0, 0));
    for (i = 0; i < 20 && battery() < 0; i++) sleep_second();
    log_value("battery", battery());
    if (battery() < 20) goto reboot;
    diagnostic_stage = 3;
    for (i = 0; i < 20; i++) {
        int status = setup_usb();
        log_value("usb-setup", status);
        if (!status) break;
        sleep_second();
    }
    fd = open_file("/dev/ttyGS0", D_O_RDWR | D_O_NOCTTY | D_O_NONBLOCK);
    log_value("tty-open", fd);
    if (fd < 0) goto reboot;
    if (call(D_IOCTL, fd, D_TCGETS, (long)&term, 0, 0, 0) == 0) {
        term.iflag = term.oflag = term.lflag = 0; term.cflag = 0x18b2;
        term.cc[5] = term.cc[6] = 0;
        call(D_IOCTL, fd, D_TCSETS, (long)&term, 0, 0, 0);
    }
    for (i = 0; i < 180; i++) {
        char value;
        if (!wait_io(fd, 1) || read_file(fd, &value, 1) != 1) continue;
        if (value == '\n') {
            command[used] = 0;
            if (equal(command, "READ") && battery() >= 20) snapshot(fd);
            else if (equal(command, "REBOOT")) break;
            used = 0;
        } else if (value != '\r' && used < sizeof(command)-1) command[used++] = value;
    }
reboot:
    diagnostic_stage = 4;
    close_file(fd);
    log_value("reboot-request", 0);
    // Same kernel reboot command used by the verified adb reboot bootloader path.
    log_value("reboot-result", call(D_REBOOT, 0xfee1dead, 0x28121969, 0xa1b2c3d4, (long)"bootloader", 0, 0));
    for (;;) sleep_second();
}
