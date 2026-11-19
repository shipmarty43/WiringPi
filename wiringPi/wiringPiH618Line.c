/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <errno.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "wiringPiH618.h"
#include "wiringPiH618Line.h"

int wiringPiH618LineABI(void) { return 1; }
int wiringPiH618HeaderToNative(int physical) { return h618Physical(physical); }

static int setupBoard(void) {
  if (!h618Detected()) { errno = ENODEV; return -1; }
  return h618Setup();
}

int wiringPiH618LineInfo(int physical, struct wpi_h618_line_info *info) {
  int native = h618Physical(physical);
  uint64_t flags;
  if (native < 0) return -1;
  if (!info) { errno = EINVAL; return -1; }
  if (setupBoard() < 0 || h618Info(native, &flags, info->consumer, sizeof(info->consumer)) < 0)
    return -1;
  info->used = !!(flags & GPIO_V2_LINE_FLAG_USED);
  info->direction = -1;
  if (flags & GPIO_V2_LINE_FLAG_INPUT) info->direction = WPI_H618_INPUT;
  if (flags & GPIO_V2_LINE_FLAG_OUTPUT) info->direction = WPI_H618_OUTPUT;
  return 0;
}

int wiringPiH618LineRequest(int physical, int direction, int initial, int bias, int edge,
                            uint32_t debounce_us) {
  struct gpio_v2_line_config config = {0};
  static const uint64_t biases[] = {0, GPIO_V2_LINE_FLAG_BIAS_DISABLED,
                                    GPIO_V2_LINE_FLAG_BIAS_PULL_UP, GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN};
  int native = h618Physical(physical);
  if (native < 0) return -1;
  if ((direction != WPI_H618_INPUT && direction != WPI_H618_OUTPUT) ||
      bias < WPI_H618_BIAS_DEFAULT || bias > WPI_H618_BIAS_DOWN ||
      edge < WPI_H618_NO_EDGE || edge > WPI_H618_BOTH ||
      initial < 0 || initial > 1 || (direction == WPI_H618_INPUT && initial) ||
      (direction == WPI_H618_OUTPUT && (bias || edge)) || (debounce_us && !edge)) {
    errno = EINVAL;
    return -1;
  }
  if (setupBoard() < 0) return -1;
  if (physical == 32 && h618PwmRouted()) { errno = EBUSY; return -1; }
  if (direction == WPI_H618_OUTPUT) {
    config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    config.num_attrs = 1;
    config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    config.attrs[0].attr.values = initial;
    config.attrs[0].mask = 1;
  } else {
    config.flags = GPIO_V2_LINE_FLAG_INPUT | biases[bias];
    if (edge & WPI_H618_RISING) config.flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
    if (edge & WPI_H618_FALLING) config.flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
    if (debounce_us) {
      config.num_attrs = 1;
      config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
      config.attrs[0].attr.debounce_period_us = debounce_us;
      config.attrs[0].mask = 1;
    }
  }
  return h618RequestConfigured(native, &config, "wiringpi-h618-line");
}

int wiringPiH618LineRead(int fd) {
  struct gpio_v2_line_values values = {.mask = 1};
  if (ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) return -1;
  return !!values.bits;
}

int wiringPiH618LineWrite(int fd, int value) {
  if (value != 0 && value != 1) { errno = EINVAL; return -1; }
  struct gpio_v2_line_values values = {.mask = 1, .bits = value};
  return ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
}

int wiringPiH618LineEvent(int fd, struct wpi_h618_event *event) {
  struct gpio_v2_line_event raw;
  if (!event) { errno = EINVAL; return -1; }
  ssize_t n;
  do { n = read(fd, &raw, sizeof(raw)); } while (n < 0 && errno == EINTR);
  if (n < 0) return -1;
  if (n != sizeof(raw)) { errno = EIO; return -1; }
  switch (raw.id) {
    case GPIO_V2_LINE_EVENT_RISING_EDGE: event->edge = WPI_H618_RISING; break;
    case GPIO_V2_LINE_EVENT_FALLING_EDGE: event->edge = WPI_H618_FALLING; break;
    default: errno = EPROTO; return -1;
  }
  event->timestamp_ns = raw.timestamp_ns;
  event->sequence = raw.line_seqno;
  return 0;
}

int wiringPiH618PwmClaim(int physical, unsigned range, unsigned divisor, unsigned duty) {
  if (physical != 32) { errno = EINVAL; return -1; }
  if (setupBoard() < 0) return -1;
  return h618PwmClaim(range, divisor, duty);
}
