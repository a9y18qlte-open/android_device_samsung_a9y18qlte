/*
 * eeprom_dump: read the calibration data the kernel cached from a camera
 * EEPROM/OTP subdev (VIDIOC_MSM_EEPROM_CFG: GET_CAL_DATA + READ_CAL_DATA).
 * Read-only. Freestanding static ARM32 binary (raw syscalls, no libc).
 *
 * usage: eeprom_dump /dev/v4l-subdevN > out.bin   (size + name go to stderr)
 */
#include <stdint.h>
#include <stddef.h>
#include <linux/ioctl.h>
#include <linux/videodev2.h>
#include <media/msm_cam_sensor.h>

static long sys3(long n, long a, long b, long c)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}
#define SYS_exit  1
#define SYS_write 4
#define SYS_open  5
#define SYS_ioctl 54

static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void err(const char *s) { sys3(SYS_write, 2, (long)s, slen(s)); }
static void errnum(long v)
{
    char b[24]; int i = 23; int neg = v < 0; unsigned long u = neg ? -v : v;
    b[i] = 0;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (neg) b[--i] = '-';
    err(b + i);
}

static uint8_t buf[256 * 1024];

void _start_c(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    if (argc < 2) { err("usage: eeprom_dump /dev/v4l-subdevN\n"); sys3(SYS_exit, 2, 0, 0); }
    long fd = sys3(SYS_open, (long)argv[1], 2 /* O_RDWR */, 0);
    if (fd < 0) { err("open failed "); errnum(fd); err("\n"); sys3(SYS_exit, 1, 0, 0); }

    struct msm_eeprom_cfg_data cfg;
    for (size_t i = 0; i < sizeof(cfg); i++) ((uint8_t *)&cfg)[i] = 0;
    cfg.cfgtype = CFG_EEPROM_GET_INFO;
    long r = sys3(SYS_ioctl, fd, VIDIOC_MSM_EEPROM_CFG, (long)&cfg);
    err("info rc="); errnum(r); err(" name="); cfg.cfg.eeprom_name[MAX_EEPROM_NAME - 1] = 0;
    err(cfg.cfg.eeprom_name); err(" supported="); errnum(cfg.is_supported); err("\n");

    for (size_t i = 0; i < sizeof(cfg); i++) ((uint8_t *)&cfg)[i] = 0;
    cfg.cfgtype = CFG_EEPROM_GET_CAL_DATA;
    r = sys3(SYS_ioctl, fd, VIDIOC_MSM_EEPROM_CFG, (long)&cfg);
    uint32_t n = cfg.cfg.get_data.num_bytes;
    err("get_cal rc="); errnum(r); err(" num_bytes="); errnum(n); err("\n");
    if (r < 0 || n == 0 || n > sizeof(buf)) sys3(SYS_exit, 1, 0, 0);

    for (size_t i = 0; i < sizeof(cfg); i++) ((uint8_t *)&cfg)[i] = 0;
    cfg.cfgtype = CFG_EEPROM_READ_CAL_DATA;
    cfg.cfg.read_data.dbuffer = buf;
    cfg.cfg.read_data.num_bytes = n;
    r = sys3(SYS_ioctl, fd, VIDIOC_MSM_EEPROM_CFG, (long)&cfg);
    err("read_cal rc="); errnum(r); err("\n");
    if (r < 0) sys3(SYS_exit, 1, 0, 0);
    for (uint32_t off = 0; off < n;) {
        long w = sys3(SYS_write, 1, (long)(buf + off), n - off);
        if (w <= 0) break;
        off += w;
    }
    sys3(SYS_exit, 0, 0, 0);
}

__asm__(".global _start\n_start:\n mov r0, sp\n bl _start_c\n");
