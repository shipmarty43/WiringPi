/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Orange Pi Zero 2W, Linux GPIO character device ABI v2 (Linux >= 5.10).
 * Pin tables: Orange Pi Zero 2W manual v1.1, section 3.17; wiringOP next,
 * pinToGpio_ZERO_2_W / physToGpio_ZERO_2_W. No MMIO or pinmux overrides.
 */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/gpio.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "wiringPi.h"
#include "wiringPiH618.h"

static const int nativePins[28] = {
  264,263,269,224,225,226,257,227,261,270,228,231,232,262,
  230,229,233,266,265,256,271,267,268,258,76,272,260,259
};
static const int a733Pins[28] = {
  35, 34, 36, 41, 42, 32, 37, 33, 139, 354, 355, 130, 131, 96,
  129, 128, 132, 357, 356, 140, 141, 97, 99, 38, 98, 100, 40, 39
};
static const int physicalPins[28] = {
  3,5,7,8,10,11,12,13,15,16,18,19,21,22,23,24,26,27,28,
  29,31,32,33,35,36,37,38,40
};
static int chip = -1, standbyChip = -1;
static int registered;
static int fds[28];
static unsigned offsets[28];
static uint64_t lineFlags[28];
static int outputValues[28];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static int compatible(const char *data, size_t size, const char *boardName, const char *socName) {
  int board = 0, soc = 0;
  for (size_t pos = 0; pos < size;) {
    const char *end = memchr(data + pos, 0, size - pos);
    if (!end) return 0;
    if (!strcmp(data + pos, boardName)) board = 1;
    if (!strcmp(data + pos, socName)) soc = 1;
    pos = (size_t)(end - data) + 1;
  }
  return board && soc;
}

int h618Compatible(const char *data, size_t size) {
  return compatible(data, size, "xunlong,orangepi-zero2w", "allwinner,sun50i-h618");
}
int a733Compatible(const char *data, size_t size) {
  return compatible(data, size, "xunlong,orangepi-zero3w", "arm,sun60iw2p1");
}

static pthread_once_t detectOnce = PTHREAD_ONCE_INIT;
static int detected, isA733;

static void detectBoard(void) {
  char buf[1024];
  FILE *fp = fopen("/proc/device-tree/compatible", "rb");
  if (!fp) fp = fopen("/sys/firmware/devicetree/base/compatible", "rb");
  if (fp) {
    size_t n = fread(buf, 1, sizeof(buf), fp);
    if (!ferror(fp) && n < sizeof(buf)) {
      detected = h618Compatible(buf, n);
      isA733 = a733Compatible(buf, n);
      if (detected && isA733) detected = isA733 = 0;
    }
    fclose(fp);
  }
}

int h618Detected(void) {
  pthread_once(&detectOnce, detectBoard);
  return detected;
}

int a733Detected(void) {
  pthread_once(&detectOnce, detectBoard);
  return isA733;
}

static const int *boardPins(void) { return a733Detected() ? a733Pins : nativePins; }
static int lineChip(int i) { return a733Detected() && a733Pins[i] >= 352 ? standbyChip : chip; }

static int slot(int native) {
  for (int i = 0; i < 28; ++i) if (boardPins()[i] == native) return i;
  errno = EINVAL;
  return -1;
}
int h618Wpi(int pin) {
  if (pin >= 0 && pin < 28) return boardPins()[pin];
  errno = EINVAL;
  return -1;
}
int opiPhysical(int pin, int a733) {
  for (int i = 0; i < 28; ++i) if (physicalPins[i] == pin) return a733 ? a733Pins[i] : nativePins[i];
  errno = EINVAL;
  return -1;
}
int h618Physical(int pin) { return opiPhysical(pin, a733Detected()); }
int h618Map(int pin, int mode) {
  switch (mode) {
    case WPI_MODE_PINS: case WPI_MODE_GPIO_DEVICE_WPI: return h618Wpi(pin);
    case WPI_MODE_PHYS: case WPI_MODE_GPIO_DEVICE_PHYS: return h618Physical(pin);
    case WPI_MODE_GPIO: case WPI_MODE_GPIO_DEVICE_BCM:
      return slot(pin) < 0 ? -1 : pin;
    default: errno = EINVAL; return -1;
  }
}

void h618Close(void) {
  if (h618WorkersClose() < 0 && errno == EDEADLK) return;
  if (h618PwmClose() < 0) h618Error("wiringPi: PWM cleanup");
  pthread_mutex_lock(&lock);
  if (chip >= 0) {
    for (int i = 0; i < 28; ++i) {
      if (fds[i] >= 0) close(fds[i]);
      fds[i] = -1;
      lineFlags[i] = 0;
      outputValues[i] = 0;
    }
    if (standbyChip >= 0) { close(standbyChip); standbyChip = -1; }
    close(chip);
    chip = -1;
  }
  pthread_mutex_unlock(&lock);
}

static void exitBoard(void) {
  h618PwmExit();
  h618Close();
}

/* Match the gpiochip character device to its actual OF ancestor. BSP labels
 * are platform names, while gpiochip numbers are assigned at probe time. */
static int a733Controller(int fd) {
  struct stat st;
  char link[128], path[PATH_MAX], target[PATH_MAX];
  if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode)) return -1;
  snprintf(link, sizeof(link), "/sys/dev/char/%u:%u", major(st.st_rdev), minor(st.st_rdev));
  if (!realpath(link, path)) return -1;
  while (!strncmp(path, "/sys/devices/", 13)) {
    char node[PATH_MAX];
    if (snprintf(node, sizeof(node), "%s/of_node", path) >= (int)sizeof(node)) return -1;
    if (realpath(node, target)) {
      if (!strcmp(target, "/sys/firmware/devicetree/base/soc@3000000/pinctrl@2000000")) return 0;
      if (!strcmp(target, "/sys/firmware/devicetree/base/soc@3000000/pinctrl@7025000")) return 1;
    }
    *strrchr(path, '/') = 0;
  }
  return -1;
}

static int discoverA733(glob_t *paths) {
  int found[2] = {-1, -1}, error = ENODEV;
  for (size_t p = 0; p < paths->gl_pathc; ++p) {
    int fd = open(paths->gl_pathv[p], O_RDONLY | O_CLOEXEC);
    if (fd < 0) { if (errno == EACCES) error = errno; continue; }
    int ctl = a733Controller(fd);
    if (ctl < 0) { close(fd); continue; }
    struct gpiochip_info info = {0};
    if (found[ctl] >= 0 || ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) < 0 ||
        info.lines != (ctl ? 64u : 352u)) { close(fd); goto fail; }
    found[ctl] = fd;
    for (int i = 0; i < 28; ++i) {
      int native = a733Pins[i];
      if ((native >= 352) != ctl) continue;
      struct gpio_v2_line_info li = {.offset = (unsigned)(native - (ctl ? 352 : 0))};
      char pad[16];
      snprintf(pad, sizeof(pad), "P%c%d", 'A' + native/32, native%32);
      if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &li) < 0 ||
          (li.name[0] && strcmp(li.name, pad))) goto fail;
      offsets[i] = li.offset;
    }
  }
  if (found[0] < 0 || found[1] < 0) goto fail;
  chip = found[0];
  standbyChip = found[1];
  return 0;
fail:
  for (int i = 0; i < 2; ++i) if (found[i] >= 0) close(found[i]);
  errno = error;
  return -1;
}

int h618Setup(void) {
  glob_t paths = {0};
  pthread_mutex_lock(&lock);
  if (chip >= 0) { pthread_mutex_unlock(&lock); return 0; }
  int error = ENODEV;
  if (!glob("/dev/gpiochip*", 0, NULL, &paths)) {
    if (a733Detected()) {
      if (discoverA733(&paths) < 0) error = errno;
    } else for (size_t p = 0; p < paths.gl_pathc && chip < 0; ++p) {
      int fd = open(paths.gl_pathv[p], O_RDONLY | O_CLOEXEC);
      if (fd < 0) { if (errno == EACCES) error = EACCES; continue; }
      struct gpiochip_info info = {0};
      if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) < 0 ||
          (strcmp(info.label, "300b000.pinctrl") && strcmp(info.label, "0300b000.pinctrl"))) {
        close(fd); continue;
      }
      /* Find named lines; never assume gpiochip number, global base or offset.
       * An image overriding pin names is deliberately rejected, not guessed. */
      unsigned found = 0;
      for (unsigned off = 0; off < info.lines; ++off) {
        struct gpio_v2_line_info li = {.offset = off};
        if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &li) < 0) break;
        for (int i = 0; i < 28; ++i) {
          char name[16];
          snprintf(name, sizeof(name), "P%c%d", 'A' + nativePins[i]/32, nativePins[i]%32);
          if (!strcmp(li.name, name)) {
            if (found & (1u << i)) { found = 0; goto reject; }
            offsets[i] = off;
            found |= 1u << i;
          }
        }
      }
      /* Mainline sun50i-h616 has pin_base=0, last pin PI16, ngpio=288,
       * but sunxi does not populate gpio_chip.names. For this exact controller
       * identity and layout, its hardware offsets equal the sunxi numbers.
       * This is not the dynamically assigned global Linux GPIO base.
       * Named or partially named unknown layouts must not use this fallback. */
      if (found == 0 && info.lines == 288) {
        for (int i = 0; i < 28; ++i) {
          struct gpio_v2_line_info li = {.offset = (unsigned)nativePins[i]};
          if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &li) < 0 || li.name[0]) {
            found = 0; break;
          }
          offsets[i] = (unsigned)nativePins[i];
          found |= 1u << i;
        }
      }
reject:
      if (found != ((1u << 28) - 1)) { close(fd); continue; }
      chip = fd;
    }
    if (chip >= 0) {
      for (int i = 0; i < 28; ++i) fds[i] = -1;
      if (!registered) { atexit(exitBoard); registered = 1; }
    }
  }
  globfree(&paths);
  int result = chip < 0 ? -1 : 0;
  pthread_mutex_unlock(&lock);
  if (result < 0) errno = error;
  return result;
}

/* Separate exclusive request for an IRQ/PWM worker. The kernel arbitrates
 * ownership against normal GPIO requests and other processes. */
int h618RequestConfigured(int pin, const struct gpio_v2_line_config *config,
                          const char *consumer) {
  int i = slot(pin), ret = -1;
  if (i < 0) return -1;
  pthread_mutex_lock(&lock);
  if (chip < 0) errno = ENODEV;
  else {
    struct gpio_v2_line_request req = {.num_lines = 1, .event_buffer_size = 64,
                                       .config = *config};
    req.offsets[0] = offsets[i];
    snprintf(req.consumer, sizeof(req.consumer), "%s", consumer);
    if (ioctl(lineChip(i), GPIO_V2_GET_LINE_IOCTL, &req) == 0) ret = req.fd;
  }
  pthread_mutex_unlock(&lock);
  return ret;
}

int h618Request(int pin, uint64_t flags, unsigned long debounce) {
  struct gpio_v2_line_config config = {.flags = flags};
  if (debounce) {
    config.num_attrs = 1;
    config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
    config.attrs[0].attr.debounce_period_us = debounce;
    config.attrs[0].mask = 1;
  }
  return h618RequestConfigured(pin, &config, "wiringpi-h618-worker");
}

/* Caller holds lock. Reconfigure an owned request atomically: no release gap,
 * and keep the existing request and cached state intact on ioctl failure. */
static int configure(int i, uint64_t flags) {
  if (chip < 0) { errno = ENODEV; return -1; }
  struct gpio_v2_line_config config = {.flags = flags};
  if (flags & GPIO_V2_LINE_FLAG_OUTPUT) {
    config.num_attrs = 1;
    config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    config.attrs[0].attr.values = outputValues[i];
    config.attrs[0].mask = 1;
  }
  if (fds[i] >= 0) {
    if (ioctl(fds[i], GPIO_V2_LINE_SET_CONFIG_IOCTL, &config) < 0) return -1;
  } else {
    struct gpio_v2_line_request req = {.num_lines = 1, .config = config};
    req.offsets[0] = offsets[i];
    strcpy(req.consumer, "wiringpi-h618");
    if (ioctl(lineChip(i), GPIO_V2_GET_LINE_IOCTL, &req) < 0) return -1;
    fds[i] = req.fd;
  }
  lineFlags[i] = flags;
  return 0;
}

int h618PinMode(int pin, int mode) {
  int i = slot(pin), ret = -1;
  if (i < 0) return -1;
  if (mode == PWM_OUTPUT) return a733Detected() ? h618Unsupported("use PWM002 on A733") : h618PwmStart(pin);
  if (pin == 267 && mode == PM_OFF && h618PwmClose() < 0) return -1;
  if (h618PwmMode(pin)) { errno = EBUSY; return -1; }
  if (mode != INPUT && mode != OUTPUT && mode != PM_OFF)
    return h618Unsupported("pinMode (only INPUT, OUTPUT, PM_OFF)");
  pthread_mutex_lock(&lock);
  if (chip < 0) errno = ENODEV;
  else if (mode == PM_OFF) {
    if (fds[i] >= 0) close(fds[i]);
    fds[i] = -1; lineFlags[i] = 0; outputValues[i] = 0; ret = 0;
  } else {
    uint64_t flags = lineFlags[i] & ~(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_OUTPUT);
    ret = configure(i, flags | (mode == INPUT ? GPIO_V2_LINE_FLAG_INPUT : GPIO_V2_LINE_FLAG_OUTPUT));
  }
  pthread_mutex_unlock(&lock);
  return ret;
}
int h618Mode(int pin) {
  int i = slot(pin), ret = -1;
  if (i < 0) return -1;
  pthread_mutex_lock(&lock);
  if (chip < 0) errno = ENODEV;
  else if (fds[i] < 0) errno = ENODATA; /* Do not invent a peripheral mux value. */
  else ret = (lineFlags[i] & GPIO_V2_LINE_FLAG_OUTPUT) ? OUTPUT : INPUT;
  pthread_mutex_unlock(&lock);
  return ret;
}
int h618Pull(int pin, int pud) {
  int i = slot(pin), ret;
  if (i < 0) return -1;
  if (pud < PUD_OFF || pud > PUD_UP) { errno = EINVAL; return -1; }
  const uint64_t bias[] = {GPIO_V2_LINE_FLAG_BIAS_DISABLED,
    GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN, GPIO_V2_LINE_FLAG_BIAS_PULL_UP};
  pthread_mutex_lock(&lock);
  uint64_t flags = lineFlags[i] & ~(GPIO_V2_LINE_FLAG_BIAS_DISABLED |
    GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN | GPIO_V2_LINE_FLAG_BIAS_PULL_UP);
  if (!(flags & (GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_OUTPUT))) flags |= GPIO_V2_LINE_FLAG_INPUT;
  ret = configure(i, flags | bias[pud]);
  pthread_mutex_unlock(&lock);
  return ret;
}
int h618Read(int pin) {
  int i = slot(pin), ret = -1;
  if (i < 0) return -1;
  pthread_mutex_lock(&lock);
  if (chip < 0) errno = ENODEV;
  else if (fds[i] >= 0 || configure(i, GPIO_V2_LINE_FLAG_INPUT) == 0) {
    struct gpio_v2_line_values values = {.mask = 1};
    if (ioctl(fds[i], GPIO_V2_LINE_GET_VALUES_IOCTL, &values) == 0) ret = !!values.bits;
  }
  pthread_mutex_unlock(&lock);
  return ret;
}
int h618Write(int pin, int value) {
  int i = slot(pin), ret = -1;
  if (i < 0) return -1;
  pthread_mutex_lock(&lock);
  if (chip < 0) errno = ENODEV;
  else if (fds[i] < 0 || !(lineFlags[i] & GPIO_V2_LINE_FLAG_OUTPUT)) errno = EPERM;
  else {
    struct gpio_v2_line_values values = {.mask = 1, .bits = !!value};
    ret = ioctl(fds[i], GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
    if (!ret) outputValues[i] = !!value;
  }
  pthread_mutex_unlock(&lock);
  return ret;
}
int h618Info(int native, uint64_t *flags, char *consumer, size_t size) {
  int i = slot(native), ret = -1;
  if (i < 0) return -1;
  pthread_mutex_lock(&lock);
  if (chip < 0) errno = ENODEV;
  else {
    struct gpio_v2_line_info info = {.offset = offsets[i]};
    ret = ioctl(lineChip(i), GPIO_V2_GET_LINEINFO_IOCTL, &info);
    if (!ret) {
      *flags = info.flags;
      if (size) snprintf(consumer, size, "%.*s", GPIO_MAX_NAME_SIZE, info.consumer);
    }
  }
  pthread_mutex_unlock(&lock);
  return ret;
}
int h618Unsupported(const char *function) {
  fprintf(stderr, "wiringPi: %s is not supported by the H618 GPIO backend\n", function);
  errno = ENOTSUP;
  return -1;
}

void h618Error(const char *function) {
  int error = errno;
  fprintf(stderr, "%s: %s\n", function, strerror(error));
  errno = error;
}
