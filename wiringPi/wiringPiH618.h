/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef WIRING_PI_H618_H
#define WIRING_PI_H618_H
#include <stddef.h>
#include <stdint.h>
/* Internal board backend. Native numbers are sunxi bank * 32 + line. */
int h618Compatible(const char *data, size_t size);
int h618Detected(void);
int h618Setup(void);
void h618Close(void);
int h618Map(int pin, int mode);
int h618Physical(int pin);
int h618Wpi(int pin);
int h618PinMode(int pin, int mode);
int h618Mode(int pin);
int h618Pull(int pin, int pud);
int h618Read(int pin);
int h618Write(int pin, int value);
int h618Info(int native, uint64_t *flags, char *consumer, size_t size);
int h618Unsupported(const char *function);
int h618CurrentPin(int pin);
int h618Request(int pin, uint64_t flags, unsigned long debounce);
int h618WorkersClose(void);
int h618WorkerStop(int pin, int kind);
int h618WaveStart(int pin, int value, int range, int tone);
int h618WaveWrite(int pin, int value, int tone);
struct WPIWfiStatus;
int h618ISR(int pin, int edge, void (*callback)(struct WPIWfiStatus, void *),
            void (*classic)(void), unsigned long debounce, void *data);
struct WPIWfiStatus h618Wait(int pin, int edge, int ms, unsigned long debounce);
unsigned long long h618Measure(int pin, int level, uint64_t duration, int frequency);
#endif
