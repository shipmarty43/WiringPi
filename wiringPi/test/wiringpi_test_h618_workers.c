/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Real pthread/poll/eventfd lifecycle, fake GPIO request and value ioctl. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include "wiringPi.h"
#include "wiringPiH618.h"
static atomic_int peer=-1, request_error, thread_error, values, high, callbacks, self_stop;
static int last_fd;
int h618Map(int pin,int mode) {
  (void)mode;
  if (pin==256 || pin==272) return pin;
  errno=EINVAL; return -1;
}
int h618Request(int pin,uint64_t flags,unsigned long debounce) {
  (void)pin; (void)flags; (void)debounce;
  if (request_error) { errno=EBUSY; return -1; }
  int pair[2]; assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);
  last_fd=pair[0]; peer=pair[1]; return pair[0];
}
int worker_ioctl(int fd,unsigned long op,...) {
  assert(fd==last_fd && op==GPIO_V2_LINE_SET_VALUES_IOCTL);
  va_list ap; va_start(ap,op); struct gpio_v2_line_values *v=va_arg(ap,void *); va_end(ap);
  assert(v->mask==1); high=!!v->bits; ++values; return 0;
}
int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __wrap_pthread_create(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *data) {
  if (thread_error) return EAGAIN;
  return __real_pthread_create(t,a,fn,data);
}
static void callback(struct WPIWfiStatus s,void *data) {
  assert(data==&callbacks && s.statusOK==1 && s.pinBCM==256 && s.timeStamp_us>0);
  if (h618WorkerStop(256,1)<0 && errno==EDEADLK) self_stop=1;
  ++callbacks;
}
static void send_edge(unsigned seq,unsigned edge,uint64_t ns) {
  struct gpio_v2_line_event ev={.seqno=seq,.line_seqno=seq,.id=edge,.timestamp_ns=ns};
  assert(write(peer,&ev,sizeof(ev))==sizeof(ev));
}
static void await_count(atomic_int *value,int minimum) {
  for (int i=0;i<2000 && *value<minimum;++i) usleep(1000);
  assert(*value>=minimum);
}
static void release_peer(void) { close(peer); peer=-1; }
static void *cancelled_wait(void *unused) {
  (void)unused;
  struct WPIWfiStatus s=h618Wait(256,INT_EDGE_BOTH,-1,0);
  assert(s.statusOK == -1 && errno == ECANCELED);
  return NULL;
}
static void *cancelled_measurement(void *unused) {
  (void)unused;
  assert(h618Measure(256,HIGH,10000000000ULL,1)==0 && errno==ECANCELED);
  return NULL;
}
static void *complete_pulse(void *unused) {
  (void)unused;
  assert(h618Measure(256,HIGH,1000000000ULL,0)==75000000 && errno==0);
  return NULL;
}
int main(void) {
  int saved_stdin=dup(0); close(0);
  assert(h618ISR(256,INT_EDGE_BOTH,callback,NULL,0,&callbacks)==0);
  assert(last_fd==0); /* The request fd must not be treated as a sentinel. */
  assert(h618ISR(256,INT_EDGE_BOTH,callback,NULL,0,&callbacks)==-1 && errno==EBUSY);
  send_edge(1,GPIO_V2_LINE_EVENT_RISING_EDGE,1000000);
  send_edge(2,GPIO_V2_LINE_EVENT_FALLING_EDGE,2000000);
  await_count(&callbacks,2); assert(self_stop);
  assert(h618WorkerStop(256,1)==0);
  assert(fcntl(last_fd,F_GETFD)==-1 && errno==EBADF);
  release_peer();
  if (saved_stdin>=0) { dup2(saved_stdin,0); close(saved_stdin); }
  request_error=1;
  assert(h618ISR(256,INT_EDGE_RISING,NULL,NULL,0,NULL)==-1 && errno==EBUSY);
  request_error=0;
  thread_error=1;
  assert(h618ISR(256,INT_EDGE_RISING,NULL,NULL,0,NULL)==-1 && errno==EAGAIN);
  assert(fcntl(last_fd,F_GETFD)==-1 && errno==EBADF);
  release_peer(); thread_error=0;
  assert(h618ISR(256,INT_EDGE_RISING,NULL,NULL,0,NULL)==0);
  send_edge(2,GPIO_V2_LINE_EVENT_RISING_EDGE,1000000); /* Lost first event. */
  usleep(30000);
  assert(h618WorkerStop(256,1)==-1 && errno==EOVERFLOW);
  release_peer();
  assert(h618WaveStart(272,-1,100,0)==-1 && errno==EINVAL);
  assert(h618WaveStart(272,1,0,0)==-1 && errno==EINVAL);
  assert(h618WaveStart(-1,1,100,0)==-1 && errno==EINVAL);
  assert(h618WaveStart(272,50,100,0)==0);
  assert(h618WaveStart(272,50,100,0)==-1 && errno==EBUSY);
  await_count(&values,4);
  assert(h618WaveWrite(272,1000,0)==0); usleep(30000); assert(high==1);
  assert(h618WorkerStop(272,3)==-1 && errno==EBUSY);
  assert(h618WorkerStop(272,2)==0); assert(high==0);
  int stopped=values; usleep(20000); assert(values==stopped); release_peer();
  assert(h618WaveWrite(272,1,0)==-1 && errno==EINVAL);
  assert(h618WaveStart(272,0,0,1)==0);
  assert(h618WaveWrite(272,100,1)==0);
  stopped=values; await_count(&values,stopped+4);
  assert(h618WorkersClose()==0); assert(high==0); release_peer();
  struct WPIWfiStatus timeout=h618Wait(256,INT_EDGE_RISING,5,0);
  assert(timeout.statusOK==0); release_peer();
  pthread_t waiting;
  assert(pthread_create(&waiting,NULL,cancelled_wait,NULL)==0);
  for (int i=0;i<2000 && peer<0;++i) usleep(1000);
  assert(peer>=0);
  assert(h618WorkerStop(256,1)==0);
  pthread_join(waiting,NULL); release_peer();
  assert(pthread_create(&waiting,NULL,cancelled_measurement,NULL)==0);
  for (int i=0;i<2000 && peer<0;++i) usleep(1000);
  assert(peer>=0);
  assert(h618WorkerStop(256,1)==0);
  pthread_join(waiting,NULL); release_peer();
  assert(pthread_create(&waiting,NULL,complete_pulse,NULL)==0);
  for (int i=0;i<2000 && peer<0;++i) usleep(1000);
  assert(peer>=0);
  /* First rise is the activation-time event halfway through a high pulse. */
  send_edge(1,GPIO_V2_LINE_EVENT_RISING_EDGE,50000000);
  send_edge(2,GPIO_V2_LINE_EVENT_FALLING_EDGE,75000000);
  send_edge(3,GPIO_V2_LINE_EVENT_RISING_EDGE,100000000);
  send_edge(4,GPIO_V2_LINE_EVENT_FALLING_EDGE,175000000);
  pthread_join(waiting,NULL); release_peer();
  puts("H618 worker lifecycle tests passed (FD0, callbacks, failure cleanup, overflow, PWM/tone, timeout)");
}
