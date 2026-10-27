/* SPDX-License-Identifier: LGPL-3.0-or-later
 * MANUAL: physical 32 -> 1 kOhm -> 29, no other devices on these pins.
 * Requires stock pwm1-pi11 overlay. Only PWM1 is driven. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wiringPi.h"
#include "wiringPiH618.h"
static volatile sig_atomic_t stopped;
static void stop(int sig) { stopped = sig; }
static void check(const char *where) {
  if (errno || stopped) { if (errno) perror(where); h618Close(); exit(1); }
}
int main(int argc, char **argv) {
  if (argc != 2 || strcmp(argv[1], "--confirm-32-to-29")) return 2;
  signal(SIGTERM, stop); signal(SIGINT, stop);
  if (!h618Detected() || wiringPiSetupPhys() < 0) return 1;
  errno = 0; pwmSetMode(PWM_MODE_MS); check("mode");
  pwmSetClock(1); check("clock");
  pinMode(32, PWM_OUTPUT); check("start");
  if (!h618PwmMode(267)) { h618Close(); return 1; }
  for (int hz = 10; hz <= 1000; hz *= 10) {
    pwmWrite(32, 0); check("mute");
    unsigned range = 24000000 / hz;
    pwmSetPinRange(32, range); check("range");
    for (int percent = 25; percent <= 75; percent += 25) {
      pwmWrite(32, range * percent / 100); check("duty"); delay(50);
      unsigned long long frequency = frequencyIn(29, 500); check("frequency");
      unsigned long long high = pulseInNS(29,HIGH,500000000ULL); check("high");
      unsigned long long low = pulseInNS(29,LOW,500000000ULL); check("low");
      printf("requested_hz=%d percent=%d measured_hz=%llu high_ns=%llu low_ns=%llu\n", hz,percent,frequency,high,low);
      double period = 1000000000.0/hz, highTarget = period*percent/100, lowTarget = period-highTarget;
      double tolerance = hz*.02 < 1 ? 1 : hz*.02;
      double highTolerance = highTarget*.05 < 50000 ? 50000 : highTarget*.05;
      double lowTolerance = lowTarget*.05 < 50000 ? 50000 : lowTarget*.05;
      if (frequency < hz-tolerance || frequency > hz+tolerance ||
          high < highTarget-highTolerance || high > highTarget+highTolerance ||
          low < lowTarget-lowTolerance || low > lowTarget+lowTolerance) {
        fputs("Loopback measurement outside IRQ timestamp tolerance\n", stderr); h618Close(); return 1;
      }
    }
  }
  pwmWrite(32, 0); check("zero duty"); delay(50);
  if (digitalRead(29) != LOW) { h618Close(); return 1; }
  pinMode(29, PM_OFF); check("input release");
  /* Change numbering while preserving owned PWM. */
  if (wiringPiSetupGpio() < 0) { h618Close(); return 1; }
  pwmWrite(267,24000); check("native full duty"); delay(50);
  if (digitalRead(256) != HIGH) { h618Close(); return 1; }
  pinMode(256,PM_OFF); check("native input release");
  if (wiringPiSetup() < 0) { h618Close(); return 1; }
  pwmWrite(21,0); check("wpi zero duty");
  pinMode(21,PM_OFF); check("PWM release");
  h618Close();
  puts("PASS: PWM public API 10/100/1000 Hz, 0/25/50/75/100%, physical/native/WPi numbering; released");
  return 0;
}
