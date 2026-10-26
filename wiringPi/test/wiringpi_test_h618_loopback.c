/* SPDX-License-Identifier: LGPL-3.0-or-later
 * MANUAL HARDWARE TEST. Connect physical pins 37 -> 29 through 1 kOhm.
 * Disconnect other devices from these two pins before running. No RF operations.
 * Build: cc -Wall wiringpi_test_h618_loopback.c -lwiringPi -o h618-loopback
 */
#include <stdio.h>
#include <wiringPi.h>
#include <wiringPiH618.h>
int main(void) {
  if (!h618Detected()) { fputs("Requires Orange Pi Zero 2W\n", stderr); return 1; }
  if (wiringPiSetupPhys() < 0) return 1;
  int out = h618Physical(37), in = h618Physical(29), result = 1;
  if (h618PinMode(in, INPUT) < 0 || h618Pull(in, PUD_DOWN) < 0 ||
      h618PinMode(out, OUTPUT) < 0) { perror("request loopback GPIO"); goto done; }
  for (int i = 0; i < 20; ++i) {
    int level = i % 2;
    digitalWrite(37, level);
    delay(10);
    int actual = digitalRead(29);
    if (actual != level) { fprintf(stderr, "Loopback step %d: expected %d, got %d\n", i, level, actual); goto done; }
  }
  puts("20 loopback transitions passed (physical 37 -> 29)");
  result = 0;
done:
  h618Close();
  return result;
}
