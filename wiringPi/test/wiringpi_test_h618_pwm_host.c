/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Fake sysfs/DT in an isolated directory; no hardware access. */
#include <assert.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static ssize_t fake_write(int fd, const void *buf, size_t size);
#define write fake_write
#include "../wiringPiH618Pwm.c"
#undef write
static const char *fail;
static int exports, unexports, writes;
static void dirs(const char *path) {
  char buf[PATH_MAX]; snprintf(buf, sizeof(buf), "%s", path);
  for (char *p = buf + 1; *p; p++) if (*p == '/') {
    *p = 0; assert(!mkdir(buf, 0700) || errno == EEXIST); *p = '/';
  }
}
static void file(const char *path, const char *data, size_t size) {
  dirs(path);
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600); assert(fd >= 0);
  assert(write(fd, data, size) == (ssize_t)size); close(fd);
}
static ssize_t fake_write(int fd, const void *buf, size_t size) {
  char link[64], path[PATH_MAX]; snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
  ssize_t n = readlink(link, path, sizeof(path)-1); assert(n > 0); path[n] = 0;
  assert(strstr(path, H618_PWM_SYSFS)); assert(!strstr(path, "pwm5")); ++writes;
  const char *name = strrchr(path, '/') + 1;
  if (fail && !strcmp(name, fail)) { fail = NULL; errno = EIO; return -1; }
  if (!strcmp(name, "export")) {
    assert(size == 1 && !memcmp(buf,"1",1));
    if (!access(H618_PWM_SYSFS "/pwmchip7/pwm1/enable", F_OK)) { errno = EBUSY; return -1; }
    file(H618_PWM_SYSFS "/pwmchip7/pwm1/enable", "0", 1);
    file(H618_PWM_SYSFS "/pwmchip7/pwm1/duty_cycle", "0", 1);
    file(H618_PWM_SYSFS "/pwmchip7/pwm1/period", "0", 1);
    file(H618_PWM_SYSFS "/pwmchip7/pwm1/polarity", "normal", 6); ++exports;
  } else if (!strcmp(name,"unexport")) {
    assert(size == 1 && !memcmp(buf,"1",1));
    unlink(H618_PWM_SYSFS "/pwmchip7/pwm1/enable"); unlink(H618_PWM_SYSFS "/pwmchip7/pwm1/duty_cycle");
    unlink(H618_PWM_SYSFS "/pwmchip7/pwm1/period"); unlink(H618_PWM_SYSFS "/pwmchip7/pwm1/polarity");
    assert(!rmdir(H618_PWM_SYSFS "/pwmchip7/pwm1")); ++unexports;
  }
  assert(!ftruncate(fd, 0)); return write(fd, buf, size);
}
int main(void) {
  file(H618_PWM_DT "/soc/pwm1@0300a000/status", "okay", 5);
  file(H618_PWM_DT "/soc/pwm1@0300a000/pinctrl-0", "\0\0\0\27", 4);
  file(H618_PWM_DT "/soc/pinctrl@300b000/pwm1-pi-pin/phandle", "\0\0\0\27", 4);
  file(H618_PWM_DT "/soc/pinctrl@300b000/pwm1-pi-pin/pins", "PI11", 5);
  file(H618_PWM_DT "/soc/pinctrl@300b000/pwm1-pi-pin/function", "pwm1", 5);
  file(H618_PWM_SYSFS "/pwmchip7/npwm", "6\n", 2);
  file(H618_PWM_SYSFS "/pwmchip7/export", "", 0);
  file(H618_PWM_SYSFS "/pwmchip7/unexport", "", 0);
  file(H618_PWM_SYSFS "/300a000.pwm/of_node/compatible", "allwinner,sun50i-h616-pwm", sizeof("allwinner,sun50i-h616-pwm"));
  dirs(H618_PWM_SYSFS "/sunxi_pwm/");
  assert(!symlink("../300a000.pwm", H618_PWM_SYSFS "/pwmchip7/device"));
  assert(!symlink("../sunxi_pwm", H618_PWM_SYSFS "/300a000.pwm/driver"));
  assert(h618PwmStart(264) < 0 && errno == ENOTSUP);
  assert(h618PwmWrite(267, 10) < 0 && errno == EPERM);
  assert(!h618PwmRange(24000)); assert(!h618PwmClock(1));
  assert(!h618PwmStart(267)); assert(exports == 1 && h618PwmMode(267));
  assert(equalFile(H618_PWM_SYSFS "/pwmchip7/pwm1/period", "1000000", 7));
  assert(!h618PwmWrite(267,6000));
  assert(equalFile(H618_PWM_SYSFS "/pwmchip7/pwm1/duty_cycle", "250000", 6));
  int previous = writes;
  assert(h618PwmWrite(267,24001) < 0 && errno == EINVAL);
  assert(h618PwmRange(100) < 0 && errno == EINVAL); /* Existing value won't fit. */
  assert(h618PwmRange(UINT_MAX) < 0 && errno == ERANGE);
  assert(h618PwmClock(0) < 0 && errno == EINVAL);
  assert(h618PwmStart(267) < 0 && errno == EBUSY); assert(writes == previous);
  pid_t child = fork(); assert(child >= 0);
  if (!child) { assert(h618PwmClose() < 0 && errno == ECHILD); _exit(0); }
  int status; assert(waitpid(child,&status,0) == child && status == 0);
  fail = "period"; assert(h618PwmWrite(267,12000) < 0 && errno == EIO);
  assert(equalFile(H618_PWM_SYSFS "/pwmchip7/pwm1/enable", "0", 1));
  assert(!h618PwmWrite(267,24000)); assert(!h618PwmClose());
  assert(!h618PwmClose()); assert(unexports == 1 && !h618PwmMode(267));
  /* Export collision is never adopted or cleaned up. */
  file(H618_PWM_SYSFS "/pwmchip7/pwm1/enable", "1", 1);
  assert(h618PwmStart(267) < 0 && errno == EBUSY); assert(!h618PwmClose());
  assert(equalFile(H618_PWM_SYSFS "/pwmchip7/pwm1/enable", "1", 1)); assert(unexports == 1);
  unlink(H618_PWM_SYSFS "/pwmchip7/pwm1/enable"); rmdir(H618_PWM_SYSFS "/pwmchip7/pwm1");
  fail = "polarity"; assert(h618PwmStart(267) < 0 && errno == EIO);
  assert(exports == 2 && unexports == 2 && !h618PwmMode(267));
  file(H618_PWM_DT "/soc/pinctrl@300b000/pwm1-pi-pin/pins", "PI12", 5);
  previous = writes; assert(h618PwmStart(267) < 0 && errno == ENODEV); assert(writes == previous);
  puts("H618 PWM lifecycle, bounds, routing, collision, fork and I/O failure tests passed");
}
