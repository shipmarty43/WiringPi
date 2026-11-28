/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef WIRING_PI_H618_LINE_H
#define WIRING_PI_H618_LINE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Checked ABI for the Zero 2W 40-pin header. All failures return -1 and set errno. */
int wiringPiH618LineABI(void);
int wiringPiH618HeaderToNative(int physical);
enum wpi_h618_direction { WPI_H618_INPUT, WPI_H618_OUTPUT };
enum wpi_h618_bias { WPI_H618_BIAS_DEFAULT, WPI_H618_BIAS_OFF, WPI_H618_BIAS_UP, WPI_H618_BIAS_DOWN };
enum wpi_h618_edge { WPI_H618_NO_EDGE, WPI_H618_RISING, WPI_H618_FALLING, WPI_H618_BOTH };
struct wpi_h618_line_info {
  int used;
  int direction; /* WPI_H618_INPUT, WPI_H618_OUTPUT or -1 if unspecified */
  char consumer[32];
};
/* Inspect the kernel line info without requesting the line or reading its level. */
int wiringPiH618LineInfo(int physical, struct wpi_h618_line_info *info);
/* A733 requests use physical header numbering and the same fd operations and event ABI. */
int wiringPiA733HeaderToNative(int physical);
int wiringPiA733LineInfo(int physical, struct wpi_h618_line_info *info);
int wiringPiA733LineRequest(int pin, int direction, int initial, int bias, int edge, uint32_t debounce_us);
/* Returns an exclusive, caller-owned fd. Release with close(2). Input initial = 0. */
int wiringPiH618LineRequest(int physical, int direction, int initial, int bias, int edge,
                            uint32_t debounce_us);
int wiringPiH618LineRead(int fd);
int wiringPiH618LineWrite(int fd, int value);
struct wpi_h618_event {
  uint64_t timestamp_ns;
  uint32_t sequence;
  uint32_t edge;
};
/* Call after poll(2) reports POLLIN. One reader per request. */
int wiringPiH618LineEvent(int fd, struct wpi_h618_event *event);

/* Kernel PWM1 on physical pin 32. Returns a positive, process-local lease.
 * Period in ns = round(range * divisor * 125 / 3), duty is in range units. */
int wiringPiH618PwmClaim(int physical, unsigned range, unsigned divisor, unsigned duty);
int wiringPiH618PwmSet(int lease, unsigned duty);
int wiringPiH618PwmRelease(int lease);
#ifdef __cplusplus
}
#endif
#endif
