/* SPDX-License-Identifier: LGPL-3.0-or-later
 * H618 GPIO workers. Each worker owns a separate kernel line request.
 * No BCM-sized arrays, asynchronous cancellation, or callbacks under a lock.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <limits.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "wiringPi.h"
#include "wiringPiH618.h"

/* Native offsets, validated against the header map before indexing. */
#define NATIVE_LIMIT 288
#define IRQ 1
#define PWM 2
#define TONE 3
struct worker {
  int kind, stopping, error, fd, cancel, native, value, range, notify;
  pthread_t thread;
  void (*callback)(struct WPIWfiStatus, void *);
  void (*classic)(void);
  void (*raw)(const struct gpio_v2_line_event *, void *);
  void *data;
};
static struct worker workers[NATIVE_LIMIT];
static pthread_mutex_t workers_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workers_changed = PTHREAD_COND_INITIALIZER;
static unsigned long generations[NATIVE_LIMIT];
static _Thread_local int callback_thread;

static int valid(int pin) { return h618Map(pin, WPI_MODE_GPIO) >= 0; }
static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}
/* Return 1 on cancellation, 0 on timeout, -1 on an unexpected poll error. */
static int pause_ns(int fd, uint64_t ns) {
  uint64_t deadline = now_ns() + ns;
  for (;;) {
    uint64_t now = now_ns(), left = now < deadline ? deadline - now : 0;
    struct timespec ts = {.tv_sec = left / 1000000000u, .tv_nsec = left % 1000000000u};
    struct pollfd p = {.fd = fd, .events = POLLIN};
    int ret = ppoll(&p, 1, &ts, NULL);
    if (ret < 0 && errno == EINTR) continue;
    return ret < 0 ? -1 : (ret ? 1 : 0);
  }
}
static int output(int fd, int high) {
  struct gpio_v2_line_values v = {.mask = 1, .bits = !!high};
  return ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}
static void *run_worker(void *arg) {
  struct worker *w = arg;
  int error = 0;
  if (w->kind == IRQ) {
    unsigned sequence=0;
    for (;;) {
      struct pollfd p[2] = {{.fd=w->cancel, .events=POLLIN}, {.fd=w->fd, .events=POLLIN}};
      int ret = poll(p, 2, -1);
      if (ret < 0 && errno == EINTR) continue;
      if (ret < 0) { error = errno; break; }
      if (p[0].revents) break;
      if (!(p[1].revents & POLLIN)) { error = EIO; break; }
      struct gpio_v2_line_event ev;
      ssize_t n = read(w->fd, &ev, sizeof(ev));
      if (n < 0 && errno == EINTR) continue;
      if (n != sizeof(ev)) { error = n < 0 ? errno : EIO; break; }
      if (ev.seqno != ++sequence) { error=EOVERFLOW; break; }
      if (ev.id != GPIO_V2_LINE_EVENT_RISING_EDGE && ev.id != GPIO_V2_LINE_EVENT_FALLING_EDGE) {
        error=EPROTO; break;
      }
      struct WPIWfiStatus status = {.statusOK=1, .pinBCM=w->native,
        .edge=ev.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? INT_EDGE_RISING : INT_EDGE_FALLING,
        .timeStamp_us=(long long)(ev.timestamp_ns / 1000)};
      callback_thread = 1;
      if (w->raw) w->raw(&ev, w->data);
      else if (w->callback) w->callback(status, w->data);
      else if (w->classic) w->classic();
      callback_thread = 0;
    }
  } else {
    for (;;) {
      pthread_mutex_lock(&workers_lock);
      int value = w->value, range = w->range, stopping = w->stopping;
      pthread_mutex_unlock(&workers_lock);
      if (stopping) break;
      uint64_t high, low;
      if (w->kind == TONE) {
        high = low = value ? 500000000u / (unsigned)value : 0;
      } else {
        high = (uint64_t)value * 100000u;
        low = (uint64_t)(range - value) * 100000u;
      }
      if (!high || !low) {
        if (output(w->fd, high != 0) < 0) { error = errno; break; }
        int ret = pause_ns(w->cancel, 10000000u);
        if (ret) { if (ret < 0) error = errno; break; }
      } else {
        if (output(w->fd, 1) < 0) { error = errno; break; }
        int ret = pause_ns(w->cancel, high);
        if (ret) { if (ret < 0) error = errno; break; }
        if (output(w->fd, 0) < 0) { error = errno; break; }
        ret = pause_ns(w->cancel, low);
        if (ret) { if (ret < 0) error = errno; break; }
      }
    }
    if (output(w->fd, 0) < 0 && !error) error = errno;
  }
  pthread_mutex_lock(&workers_lock);
  w->error = error;
  pthread_mutex_unlock(&workers_lock);
  if (w->notify >= 0) {
    uint64_t one=1;
    ssize_t n;
    do { n=write(w->notify,&one,sizeof(one)); } while (n < 0 && errno == EINTR);
  }
  return NULL;
}

/* Caller holds workers_lock. Kernel rejects lines already owned by digital IO,
 * another worker or another process; existing requests are never stolen. */
static int start(int pin, int kind, uint64_t flags, unsigned long debounce,
                 int value, int range, void (*callback)(struct WPIWfiStatus, void *),
                 void (*classic)(void), void *data,
                 void (*raw)(const struct gpio_v2_line_event *, void *), int notify) {
  if (!valid(pin)) return -1;
  struct worker *w = &workers[pin];
  if (w->kind) { errno = EBUSY; return -1; }
  int fd = h618Request(pin, flags, debounce);
  if (fd < 0) return -1;
  int cancel = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (cancel < 0) { int saved = errno; close(fd); errno = saved; return -1; }
  *w = (struct worker){.kind=kind, .fd=fd, .cancel=cancel, .native=pin,
    .value=value, .range=range, .callback=callback, .classic=classic, .data=data,
    .raw=raw, .notify=notify};
  int error = pthread_create(&w->thread, NULL, run_worker, w);
  if (error) { close(fd); close(cancel); memset(w, 0, sizeof(*w)); errno=error; return -1; }
  return 0;
}

int h618WorkerStop(int pin, int kind) {
  if (!valid(pin)) return -1;
  /* Joining workers from a callback can deadlock even between different pins. */
  if (callback_thread) { errno = EDEADLK; return -1; }
  pthread_mutex_lock(&workers_lock);
  struct worker *w = &workers[pin];
  if (!w->kind) { pthread_mutex_unlock(&workers_lock); return 0; }
  if (kind && kind != w->kind) {
    pthread_mutex_unlock(&workers_lock); errno=EBUSY; return -1;
  }
  if (w->stopping) {
    unsigned long generation=generations[pin];
    while (w->stopping && generation == generations[pin])
      pthread_cond_wait(&workers_changed,&workers_lock);
    pthread_mutex_unlock(&workers_lock);
    return 0;
  }
  w->stopping = 1;
  uint64_t one = 1;
  ssize_t n;
  do { n = write(w->cancel, &one, sizeof(one)); } while (n < 0 && errno == EINTR);
  pthread_t thread = w->thread;
  pthread_mutex_unlock(&workers_lock);
  int error = pthread_join(thread, NULL);
  if (error) { errno=error; return -1; }
  pthread_mutex_lock(&workers_lock);
  error = w->error;
  close(w->fd); close(w->cancel);
  memset(w, 0, sizeof(*w));
  ++generations[pin];
  pthread_cond_broadcast(&workers_changed);
  pthread_mutex_unlock(&workers_lock);
  if (error) { errno=error; return -1; }
  return 0;
}
int h618WorkersClose(void) {
  if (callback_thread) { errno=EDEADLK; return -1; }
  int error=0;
  for (int pin=0; pin<NATIVE_LIMIT; ++pin) {
    /* Only the 28 header GPIOs can have a worker. */
    if (h618Map(pin, WPI_MODE_GPIO) >= 0 && h618WorkerStop(pin, 0) < 0) error=errno;
  }
  errno=error;
  return error ? -1 : 0;
}
int h618WaveStart(int pin, int value, int range, int tone) {
  if ((!tone && (range <= 0 || range > INT_MAX/100 || value < 0 || value > range))) {
    errno=EINVAL; return -1;
  }
  pthread_mutex_lock(&workers_lock);
  int ret=start(pin, tone ? TONE : PWM, GPIO_V2_LINE_FLAG_OUTPUT | GPIO_V2_LINE_FLAG_BIAS_DISABLED,
                0, tone ? 0 : value, range, NULL, NULL, NULL, NULL, -1);
  pthread_mutex_unlock(&workers_lock);
  return ret;
}
int h618WaveWrite(int pin, int value, int tone) {
  if (!valid(pin)) return -1;
  pthread_mutex_lock(&workers_lock);
  struct worker *w=&workers[pin];
  if (w->kind != (tone ? TONE : PWM) || w->stopping || w->error) {
    int error=w->error ? w->error : EINVAL;
    pthread_mutex_unlock(&workers_lock); errno=error; return -1;
  }
  int max=tone ? 5000 : w->range;
  w->value=value < 0 ? 0 : (value > max ? max : value);
  pthread_mutex_unlock(&workers_lock);
  return 0;
}
int h618ISR(int pin, int edge, void (*callback)(struct WPIWfiStatus, void *),
            void (*classic)(void), unsigned long debounce, void *data) {
  uint64_t flags=GPIO_V2_LINE_FLAG_INPUT;
  if (edge == INT_EDGE_RISING || edge == INT_EDGE_BOTH) flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
  if (edge == INT_EDGE_FALLING || edge == INT_EDGE_BOTH) flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
  if (edge < INT_EDGE_FALLING || edge > INT_EDGE_BOTH || debounce > UINT32_MAX) {
    errno=EINVAL; return -1;
  }
  pthread_mutex_lock(&workers_lock);
  int ret=start(pin, IRQ, flags, debounce, 0, 0, callback, classic, data, NULL, -1);
  pthread_mutex_unlock(&workers_lock);
  return ret;
}
struct first_event { int fd, received; struct WPIWfiStatus status; };
static void first_callback(struct WPIWfiStatus status, void *data) {
  struct first_event *first=data;
  if (first->received) return;
  first->status=status;
  first->received=1;
  uint64_t one=1;
  ssize_t n;
  do { n=write(first->fd, &one, sizeof(one)); } while (n < 0 && errno == EINTR);
}
struct WPIWfiStatus h618Wait(int pin, int edge, int ms, unsigned long debounce) {
  struct WPIWfiStatus result={.statusOK=-1};
  if (callback_thread) { errno=EDEADLK; return result; }
  struct first_event first={.fd=eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
  if (first.fd < 0) return result;
  uint64_t flags=GPIO_V2_LINE_FLAG_INPUT;
  if (edge == INT_EDGE_RISING || edge == INT_EDGE_BOTH) flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
  if (edge == INT_EDGE_FALLING || edge == INT_EDGE_BOTH) flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
  if (edge < INT_EDGE_FALLING || edge > INT_EDGE_BOTH || debounce > UINT32_MAX) {
    close(first.fd); errno=EINVAL; return result;
  }
  pthread_mutex_lock(&workers_lock);
  int started=start(pin, IRQ, flags, debounce, 0, 0, first_callback, NULL, &first, NULL, first.fd);
  pthread_mutex_unlock(&workers_lock);
  if (started < 0) { int saved=errno; close(first.fd); errno=saved; return result; }
  /* Stop joins the callback before first/data/notification FD leave scope. */
  struct pollfd p={.fd=first.fd, .events=POLLIN};
  int ret;
  if (ms < 0) { do { ret=poll(&p, 1, -1); } while (ret < 0 && errno == EINTR); }
  else ret=pause_ns(first.fd, (uint64_t)ms*1000000u);
  int error=ret < 0 ? errno : 0;
  if (h618WorkerStop(pin, IRQ) < 0 && !error) error=errno;
  if (!error && ret > 0 && !first.received) error=ECANCELED;
  if (!error) result=ret > 0 ? first.status : (struct WPIWfiStatus){.statusOK=0};
  close(first.fd);
  errno=error;
  return result;
}

/* Kernel timestamps, not userspace scheduling, determine measured durations. */
struct measurement {
  int fd, frequency, level, primed;
  uint64_t first, last, count, width;
};
static void measured(const struct gpio_v2_line_event *ev, void *data) {
  struct measurement *m=data;
  /* Enabling a sunxi edge IRQ while the input is high can report an initial
   * event at request time. Discard it so a partial first pulse cannot become
   * a measured width or bias the frequency interval. */
  if (!m->primed) { m->primed=1; return; }
  if (m->frequency) {
    if (!m->count) m->first=ev->timestamp_ns;
    m->last=ev->timestamp_ns; ++m->count;
  } else if (!m->width) {
    unsigned start_edge=m->level == HIGH ? GPIO_V2_LINE_EVENT_RISING_EDGE : GPIO_V2_LINE_EVENT_FALLING_EDGE;
    if (ev->id == start_edge) m->first=ev->timestamp_ns;
    else if (m->first && ev->timestamp_ns > m->first) {
      m->width=ev->timestamp_ns-m->first;
      uint64_t one=1;
      ssize_t n;
      do { n=write(m->fd,&one,sizeof(one)); } while (n < 0 && errno == EINTR);
    }
  }
}
unsigned long long h618Measure(int pin, int level, uint64_t duration, int frequency) {
  if (callback_thread || !duration || duration > INT64_MAX || (level != LOW && level != HIGH)) {
    errno=callback_thread ? EDEADLK : EINVAL; return 0;
  }
  struct measurement m={.fd=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK), .frequency=frequency, .level=level};
  if (m.fd < 0) return 0;
  uint64_t flags=GPIO_V2_LINE_FLAG_INPUT|GPIO_V2_LINE_FLAG_EDGE_RISING;
  if (!frequency) flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
  pthread_mutex_lock(&workers_lock);
  int ret=start(pin,IRQ,flags,0,0,0,NULL,NULL,&m,measured,m.fd);
  pthread_mutex_unlock(&workers_lock);
  if (ret < 0) { int saved=errno; close(m.fd); errno=saved; return 0; }
  ret=pause_ns(m.fd,duration);
  int error=ret < 0 ? errno : 0;
  if (h618WorkerStop(pin,IRQ) < 0 && !error) error=errno;
  /* Frequency waits finish only at the deadline; pulse waits may wake on a
   * completed width. An external stop must not return a partial measurement. */
  if (!error && ret > 0 && (frequency || !m.width)) error=ECANCELED;
  close(m.fd);
  errno=error;
  if (error) return 0;
  if (!frequency) return m.width;
  return m.count >= 2 && m.last > m.first ? (m.count-1)*1000000000u/(m.last-m.first) : 0;
}
