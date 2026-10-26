/* SPDX-License-Identifier: LGPL-3.0-or-later
 * MANUAL hardware test: physical 37 <-> 29 through 1 kOhm; no other devices.
 * Build: cc -Wall -I.. wiringpi_test_h618_events.c -lwiringPi -lpthread -o h618-events
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include "wiringPi.h"
#include "wiringPiH618.h"
#include "softPwm.h"
#include "softTone.h"
static atomic_int rises, falls, bad;
static void event(struct WPIWfiStatus s, void *data) {
  if (s.statusOK != 1 || s.pinBCM != 256 || s.timeStamp_us <= 0 || data != &rises)
    atomic_fetch_add(&bad, 1);
  if (s.edge == INT_EDGE_RISING) atomic_fetch_add(&rises, 1);
  else if (s.edge == INT_EDGE_FALLING) atomic_fetch_add(&falls, 1);
  else atomic_fetch_add(&bad, 1);
}
static void *delayed_high(void *unused) {
  (void)unused; delay(100); digitalWrite(37, HIGH); return NULL;
}
static void reset_counts(void) { rises=0; falls=0; bad=0; }
int main(void) {
  if (!h618Detected()) return 77;
  assert(wiringPiSetupPhys() == 0);
  assert(h618PinMode(272, OUTPUT) == 0);
  assert(h618Write(272, LOW) == 0);
  assert(wiringPiISR2(29, INT_EDGE_BOTH, event, 0, &rises) == 0);
  errno=0;
  assert(wiringPiISR2(29, INT_EDGE_BOTH, event, 0, &rises) == -1 && errno == EBUSY);
  for (int i=0; i<10; ++i) { digitalWrite(37,HIGH); delay(20); digitalWrite(37,LOW); delay(20); }
  assert(wiringPiISRStop(29) == 0);
  printf("IRQ: rising=%d falling=%d bad=%d\n", (int)rises, (int)falls, (int)bad);
  assert(rises == 10 && falls == 10 && bad == 0);
  struct WPIWfiStatus s=waitForInterrupt2(29, INT_EDGE_RISING, 30, 0);
  assert(s.statusOK == 0);
  pthread_t sender;
  assert(pthread_create(&sender,NULL,delayed_high,NULL) == 0);
  s=waitForInterrupt2(29, INT_EDGE_RISING, 1000, 0);
  pthread_join(sender,NULL);
  assert(s.statusOK == 1 && s.edge == INT_EDGE_RISING && s.pinBCM == 256);
  puts("waitForInterrupt2: timeout and rising event passed");
  assert(h618PinMode(272, PM_OFF) == 0);
  reset_counts();
  assert(wiringPiISR2(29,INT_EDGE_BOTH,event,0,&rises) == 0);
  assert(softPwmCreate(37,50,100) == 0);
  delay(500);
  printf("Software PWM 100 Hz nominal: rising=%d falling=%d\n", (int)rises, (int)falls);
  assert(rises > 20 && falls > 20 && bad == 0);
  softPwmWrite(37,0); delay(30); reset_counts(); delay(80);
  assert(rises == 0 && falls == 0);
  softPwmWrite(37,100); delay(30); reset_counts(); delay(80);
  assert(rises == 0 && falls == 0);
  assert(wiringPiISRStop(29) == 0);
  assert(digitalRead(29) == HIGH);
  pinMode(29,PM_OFF);
  softPwmStop(37);
  reset_counts();
  assert(wiringPiISR2(29,INT_EDGE_BOTH,event,0,&rises) == 0);
  assert(softToneCreate(37) == 0);
  softToneWrite(37,100);
  delay(500);
  printf("Software tone 100 Hz nominal: rising=%d falling=%d\n", (int)rises, (int)falls);
  assert(rises > 20 && falls > 20 && bad == 0);
  softToneWrite(37,0); delay(30); reset_counts(); delay(80);
  assert(rises == 0 && falls == 0);
  softToneStop(37);
  assert(wiringPiISRStop(29) == 0);
  assert(softPwmCreate(37,50,100) == 0);
  unsigned long long hz=frequencyIn(29,300);
  unsigned long long ns=pulseInNS(29,HIGH,1000000000ULL);
  unsigned int us=pulseIn(29,LOW,1000000);
  printf("Kernel event measurements: %llu Hz, HIGH %llu ns, LOW %u us\n",hz,ns,us);
  assert(hz >= 70 && hz <= 120 && ns >= 3000000 && ns <= 15000000 && us >= 3000 && us <= 15000);
  softPwmStop(37);
  /* Close with active workers must join and release them, then allow reuse. */
  assert(wiringPiISR2(29,INT_EDGE_BOTH,event,1000,&rises) == 0);
  assert(softPwmCreate(37,50,100) == 0);
  delay(80);
  h618Close();
  assert(wiringPiSetupGpio() == 0);
  assert(softPwmCreate(272,0,100) == 0);
  softPwmStop(272);
  assert(wiringPiISR2(256,INT_EDGE_BOTH,event,0,&rises) == 0);
  assert(wiringPiISRStop(256) == 0);
  h618Close();
  puts("H618 IRQ / software PWM / tone / debounce setup / cleanup tests passed");
}
