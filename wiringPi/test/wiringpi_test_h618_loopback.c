/* SPDX-License-Identifier: LGPL-3.0-or-later
 * MANUAL HARDWARE TEST. Connect physical pins 37 <-> 29 through 1 kOhm.
 * Disconnect other devices from these two pins before running. No RF operations.
 * Build: cc -Wall wiringpi_test_h618_loopback.c -lwiringPi -o h618-loopback
 */
#include <errno.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <wiringPi.h>
#include <wiringPiH618.h>

/* Public setters are void: check their documented errno error reporting. */
#define SET(call) do { errno = 0; call; if (errno) { perror(#call); return -1; } } while (0)

static int expect(int pin, int level) {
  int actual = digitalRead(pin);
  if (actual == level) return 0;
  fprintf(stderr, "Pin %d: expected %d, got %d (errno=%d)\n", pin, level, actual, errno);
  return -1;
}

static int direction(int out, int in) {
  /* Always release the previous output direction before enabling another. */
  SET(pinMode(out, INPUT));
  SET(pinMode(in, INPUT));
  SET(pullUpDnControl(out, PUD_OFF));
  SET(pullUpDnControl(in, PUD_DOWN));
  SET(pinMode(out, OUTPUT));
  if (getAlt(out) != OUTPUT || getAlt(in) != INPUT) return -1;
  for (int i = 0; i < 20; ++i) {
    int level = i % 2;
    SET(digitalWrite(out, level));
    delay(10);
    if (expect(in, level) < 0) return -1;
  }
  /* Reconfiguration must preserve the last output value (HIGH). */
  SET(pullUpDnControl(out, PUD_UP));
  delay(10);
  if (getAlt(out) != OUTPUT || expect(in, HIGH) < 0) return -1;
  SET(pullUpDnControl(out, PUD_OFF));
  SET(pinMode(out, INPUT));
  SET(pullUpDnControl(in, PUD_OFF));
  /* With both lines inputs, one internal pull drives the connected pair. */
  SET(pullUpDnControl(out, PUD_UP));
  delay(10);
  if (expect(out, HIGH) < 0 || expect(in, HIGH) < 0) return -1;
  SET(pullUpDnControl(out, PUD_DOWN));
  delay(10);
  if (expect(out, LOW) < 0 || expect(in, LOW) < 0) return -1;
  SET(pullUpDnControl(out, PUD_OFF));
  return 0;
}

static int released(void) {
  const int physical[] = {37, 29};
  for (unsigned i = 0; i < sizeof(physical) / sizeof(physical[0]); ++i) {
    uint64_t flags;
    char consumer[32];
    if (h618Info(h618Physical(physical[i]), &flags, consumer, sizeof(consumer)) < 0)
      return -1;
    if (flags & GPIO_V2_LINE_FLAG_USED) {
      fprintf(stderr, "Physical pin %d still owned by %s\n", physical[i], consumer);
      return -1;
    }
  }
  return 0;
}

static int run(void) {
  const struct {
    const char *name;
    int (*setup)(void);
    int a, b;
  } modes[] = {
    {"physical", wiringPiSetupPhys, 37, 29},
    {"wPi", wiringPiSetup, 25, 19},
    {"sunxi", wiringPiSetupGpio, 272, 256},
  };
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
    if (modes[i].setup() < 0) return -1;
    if (direction(modes[i].a, modes[i].b) < 0 ||
        direction(modes[i].b, modes[i].a) < 0) return -1;
    SET(pinMode(modes[i].a, PM_OFF));
    SET(pinMode(modes[i].b, PM_OFF));
    if (released() < 0) return -1;
    printf("%s: both directions, 40 level checks, pull-up/down, output preservation, PM_OFF passed\n",
           modes[i].name);
    h618Close();
  }
  puts("120 loopback level checks passed (physical 37 <-> 29); both lines released");
  return 0;
}

int main(void) {
  if (!h618Detected()) { fputs("Requires Orange Pi Zero 2W\n", stderr); return 1; }
  int result = run();
  h618Close();
  return result < 0 ? 1 : 0;
}
