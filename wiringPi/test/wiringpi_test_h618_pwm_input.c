/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Input-only probe for the manual PWM1 physical 32 -> resistor -> 29 test.
 * Does not configure PWM or drive a GPIO. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "wiringPi.h"
#include "wiringPiH618.h"

int main(int argc, char **argv) {
  int result=1;
  if (argc != 2 || (strcmp(argv[1],"level") && strcmp(argv[1],"wave"))) return 2;
  if (!h618Detected() || wiringPiSetupPhys() < 0) return 1;
  if (!strcmp(argv[1],"level")) {
    int level=digitalRead(29);
    if (level < 0) { perror("digitalRead"); goto done; }
    printf("level=%d\n",level);
  } else {
    unsigned long long hz=frequencyIn(29,500);
    if (errno) { perror("frequencyIn"); goto done; }
    unsigned long long high=pulseInNS(29,HIGH,500000000ULL);
    if (errno) { perror("pulseInNS HIGH"); goto done; }
    unsigned long long low=pulseInNS(29,LOW,500000000ULL);
    if (errno) { perror("pulseInNS LOW"); goto done; }
    printf("frequency=%llu high_ns=%llu low_ns=%llu\n",hz,high,low);
  }
  result=0;
done:
  h618Close();
  return result;
}
