/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <errno.h>
#include <limits.h>
#include <linux/gpio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wiringPi.h>
#include <wiringPiH618.h>

void h618Readall(void) {
  puts("Orange Pi Zero 2W / H618 — 40-pin header");
  puts(" Phys  wPi  sunxi  Name  GPIO state / consumer");
  for (int phys = 1; phys <= 40; ++phys) {
    int native = h618Physical(phys);
    if (native < 0) {
      printf(" %4d    -      -  %-4s\n", phys,
        phys == 1 || phys == 17 ? "3.3V" : phys == 2 || phys == 4 ? "5V" : "GND");
      continue;
    }
    int wpi = 0;
    while (h618Wpi(wpi) != native) ++wpi;
    uint64_t flags;
    char consumer[GPIO_MAX_NAME_SIZE];
    printf(" %4d  %3d  %5d  P%c%-2d  ", phys, wpi, native, 'A' + native/32, native%32);
    if (h618Info(native, &flags, consumer, sizeof(consumer)) < 0) printf("unavailable (%s)", strerror(errno));
    else printf("%s%s%s", flags & GPIO_V2_LINE_FLAG_USED ? "USED" : "GPIO-unclaimed",
      consumer[0] ? " / " : "", consumer);
    putchar('\n');
  }
  puts("No lines requested. GPIO metadata does not expose peripheral mux or unowned voltage levels.");
}

static int number(const char *arg, int max) {
  char *end;
  errno = 0;
  long n = strtol(arg, &end, 10);
  if (errno || end == arg || *end || n < 0 || n > max) return -1;
  return (int)n;
}

int h618Command(int argc, char **argv, int mode) {
  if (argc == 2 && (!strcmp(argv[1], "readall") || !strcmp(argv[1], "nreadall") || !strcmp(argv[1], "allreadall"))) {
    h618Readall(); return 0;
  }
  if (argc < 3) goto usage;
  int pin = h618Map(number(argv[2], INT_MAX), mode), ret = -1;
  if (pin < 0) { fprintf(stderr, "Invalid H618 header pin\n"); return 1; }
  if (!strcmp(argv[1], "read") && argc == 3) {
    ret = h618Read(pin);
    if (ret >= 0) { printf("%d\n", ret); return 0; }
  } else if ((!strcmp(argv[1], "hold") || !strcmp(argv[1], "write")) && (argc == 4 || argc == 5)) {
    int value = number(argv[3], 1);
    int seconds = argc == 5 ? number(argv[4], INT_MAX) : 0;
    if (value < 0 || seconds < 0 || (!strcmp(argv[1], "write") && argc != 4)) goto usage;
    ret = h618PinMode(pin, OUTPUT);
    if (!ret) ret = h618Write(pin, value);
    if (!ret) {
      if (!strcmp(argv[1], "hold")) {
        if (seconds) sleep((unsigned)seconds);
        else for (;;) pause(); /* SIGINT terminates and releases request. */
      } else fprintf(stderr, "GPIO request released on exit; use 'hold' for a sustained level.\n");
    }
  } else if (!strcmp(argv[1], "mode") && argc == 4) {
    if (!strcmp(argv[3], "in")) ret = h618PinMode(pin, INPUT);
    else if (!strcmp(argv[3], "out")) ret = h618PinMode(pin, OUTPUT);
    else if (!strcmp(argv[3], "up")) ret = h618Pull(pin, PUD_UP);
    else if (!strcmp(argv[3], "down")) ret = h618Pull(pin, PUD_DOWN);
    else if (!strcmp(argv[3], "off")) ret = h618Pull(pin, PUD_OFF);
    else goto usage;
    if (!ret) fprintf(stderr, "Mode applies only during this process; use the library to retain ownership.\n");
  } else goto usage;
  if (ret < 0) perror("H618 GPIO");
  return ret < 0;
usage:
  fprintf(stderr, "H618 commands: readall | read pin | write pin 0/1 | hold pin 0/1 [seconds] | mode pin in/out/up/down/off\n"
    "Default numbering: wiringOP wPi; -1: physical; -g: sunxi. Other commands are unsupported.\n");
  return 1;
}
