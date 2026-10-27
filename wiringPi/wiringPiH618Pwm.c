/* SPDX-License-Identifier: LGPL-3.0-or-later
 * H618 PWM1 on Zero 2W physical 32 (PI11). Kernel sysfs only, no MMIO.
 * Deliberately accepts only the verified Armbian sunxi_pwm DT layout.
 */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wiringPi.h"
#include "wiringPiH618.h"
#ifndef H618_PWM_DT
#define H618_PWM_DT "/sys/firmware/devicetree/base"
#endif
#ifndef H618_PWM_SYSFS
#define H618_PWM_SYSFS "/sys/class/pwm"
#endif
static pthread_mutex_t pwmLock = PTHREAD_MUTEX_INITIALIZER;
static char pwmChip[PATH_MAX];
static pid_t owner;
static unsigned pwmRange = 1024, pwmDivisor = 32, pwmValue;

static int readBytes(const char *path, void *data, size_t size) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  ssize_t n = read(fd, data, size);
  int err = errno;
  close(fd); errno = err;
  return (int)n;
}
static int equalFile(const char *path, const char *value, size_t size) {
  char buf[128];
  int n = readBytes(path, buf, sizeof(buf));
  return n == (int)size && !memcmp(buf, value, size);
}
static int routeValid(void) {
  char a[8], b[8];
  return equalFile(H618_PWM_DT "/soc/pwm1@0300a000/status", "okay", 5) &&
    equalFile(H618_PWM_DT "/soc/pinctrl@300b000/pwm1-pi-pin/pins", "PI11", 5) &&
    equalFile(H618_PWM_DT "/soc/pinctrl@300b000/pwm1-pi-pin/function", "pwm1", 5) &&
    readBytes(H618_PWM_DT "/soc/pwm1@0300a000/pinctrl-0", a, sizeof(a)) == 4 &&
    readBytes(H618_PWM_DT "/soc/pinctrl@300b000/pwm1-pi-pin/phandle", b, sizeof(b)) == 4 &&
    memcmp(a, "\0\0\0\0", 4) && !memcmp(a, b, 4);
}
static int chipValid(const char *path) {
  char file[PATH_MAX], target[PATH_MAX];
  if (snprintf(file, sizeof(file), "%s/device", path) >= (int)sizeof(file) ||
      !realpath(file, target) || strcmp(strrchr(target, '/') + 1, "300a000.pwm")) return 0;
  if (snprintf(file, sizeof(file), "%s/device/driver", path) >= (int)sizeof(file) ||
      !realpath(file, target) || strcmp(strrchr(target, '/') + 1, "sunxi_pwm")) return 0;
  if (snprintf(file, sizeof(file), "%s/npwm", path) >= (int)sizeof(file) ||
      !equalFile(file, "6\n", 2)) return 0;
  if (snprintf(file, sizeof(file), "%s/device/of_node/compatible", path) >= (int)sizeof(file)) return 0;
  return equalFile(file, "allwinner,sun50i-h616-pwm", sizeof("allwinner,sun50i-h616-pwm"));
}
static int put(const char *name, const char *value) {
  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "%s/%s", pwmChip, name) >= (int)sizeof(path)) {
    errno = ENAMETOOLONG; return -1;
  }
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  size_t n = strlen(value);
  ssize_t written = write(fd, value, n);
  int err = written < 0 ? errno : EIO;
  int closed = close(fd);
  if (written != (ssize_t)n) { errno = err; return -1; }
  return closed;
}
static int number(const char *name, uint64_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return put(name, buf);
}
/* Logical 24 MHz tick. The kernel chooses the physical divider/clock.
 * Bound periods to 1 us..1 s; this is an API policy, not a hardware limit. */
static int periodFor(unsigned range, unsigned divisor, uint64_t *period) {
  if (!range || !divisor || divisor > 4095) { errno = EINVAL; return -1; }
  *period = ((uint64_t)range * divisor * 125 + 1) / 3;
  if (*period < 1000 || *period > 1000000000) { errno = ERANGE; return -1; }
  return 0;
}
/* Sysfs updates are not atomic: mute, change period/duty, then enable.
 * If an operation fails, leave the owned output disabled and report failure. */
static int apply(unsigned range, unsigned divisor, unsigned value) {
  uint64_t period;
  if (value > range) { errno = EINVAL; return -1; }
  if (periodFor(range, divisor, &period) < 0) return -1;
  if (owner && owner != getpid()) { errno = ECHILD; return -1; }
  if (owner) {
    if (put("pwm1/enable", "0") < 0 || put("pwm1/duty_cycle", "0") < 0 ||
        number("pwm1/period", period) < 0 ||
        number("pwm1/duty_cycle", period * value / range) < 0 ||
        put("pwm1/enable", "1") < 0) {
      int err = errno; put("pwm1/enable", "0"); errno = err; return -1;
    }
  }
  pwmRange = range; pwmDivisor = divisor; pwmValue = value;
  errno = 0; return 0;
}
/* Caller holds pwmLock. Never adopt or unexport another consumer's channel. */
static int release(void) {
  if (!owner) { errno = 0; return 0; }
  if (owner != getpid()) { errno = ECHILD; return -1; }
  int err = 0;
  if (put("pwm1/duty_cycle", "0") < 0) err = errno;
  if (put("pwm1/enable", "0") < 0 && !err) err = errno;
  if (put("unexport", "1") < 0) { if (!err) err = errno; }
  else { owner = 0; pwmValue = 0; pwmChip[0] = 0; }
  errno = err; return err ? -1 : 0;
}
int h618PwmClose(void) {
  pthread_mutex_lock(&pwmLock);
  int ret = release();
  pthread_mutex_unlock(&pwmLock); return ret;
}
int h618PwmMode(int pin) {
  pthread_mutex_lock(&pwmLock);
  int ret = pin == 267 && owner == getpid();
  pthread_mutex_unlock(&pwmLock); return ret;
}
int h618PwmStart(int pin) {
  if (pin != 267) { errno = ENOTSUP; return -1; }
  pthread_mutex_lock(&pwmLock);
  int ret = -1;
  if (owner) { errno = owner == getpid() ? EBUSY : ECHILD; goto out; }
  if (!routeValid()) { errno = ENODEV; goto out; }
  glob_t paths = {0};
  unsigned matches = 0;
  if (!glob(H618_PWM_SYSFS "/pwmchip*", 0, NULL, &paths)) {
    for (size_t i = 0; i < paths.gl_pathc; ++i) if (chipValid(paths.gl_pathv[i])) {
      if (strlen(paths.gl_pathv[i]) >= sizeof(pwmChip)) continue;
      strcpy(pwmChip, paths.gl_pathv[i]); ++matches;
    }
  }
  globfree(&paths);
  if (matches != 1) { errno = ENODEV; goto out; }
  if (put("export", "1") < 0) goto out;
  owner = getpid();
  /* Kernel sysfs registration is synchronous. No daemon/permission changes. */
  if (put("pwm1/enable", "0") < 0 || put("pwm1/polarity", "normal") < 0 ||
      apply(pwmRange, pwmDivisor, 0) < 0) {
    int err = errno; release(); errno = err; goto out;
  }
  ret = 0; errno = 0;
out:
  pthread_mutex_unlock(&pwmLock); return ret;
}
int h618PwmRange(unsigned range) {
  pthread_mutex_lock(&pwmLock);
  int ret = apply(range, pwmDivisor, pwmValue);
  pthread_mutex_unlock(&pwmLock); return ret;
}
int h618PwmClock(int divisor) {
  if (divisor <= 0) { errno = EINVAL; return -1; }
  pthread_mutex_lock(&pwmLock);
  int ret = apply(pwmRange, (unsigned)divisor, pwmValue);
  pthread_mutex_unlock(&pwmLock); return ret;
}
int h618PwmWrite(int pin, int value) {
  if (pin != 267) { errno = ENOTSUP; return -1; }
  if (value < 0) { errno = EINVAL; return -1; }
  pthread_mutex_lock(&pwmLock);
  int ret = -1;
  if (!owner) errno = EPERM;
  else ret = apply(pwmRange, pwmDivisor, (unsigned)value);
  pthread_mutex_unlock(&pwmLock); return ret;
}
