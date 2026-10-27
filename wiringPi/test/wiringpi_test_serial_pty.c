/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Real Linux PTY roundtrip; never opens board UARTs or drives pins. */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "wiringSerial.h"
static int partial, interrupted, failStage;
int __real_tcgetattr(int,struct termios *);
int __real_tcsetattr(int,int,const struct termios *);
int __real_ioctl(int,unsigned long,...);
int __wrap_tcgetattr(int fd,struct termios *attr) {
  if (failStage==1) { errno=EIO; return -1; }
  return __real_tcgetattr(fd,attr);
}
int __wrap_tcsetattr(int fd,int action,const struct termios *attr) {
  if (failStage==2) { errno=EIO; return -1; }
  return __real_tcsetattr(fd,action,attr);
}
int __wrap_ioctl(int fd,unsigned long request,...) {
  va_list ap; va_start(ap,request); void *arg=va_arg(ap,void *); va_end(ap);
  if (request==TIOCMGET && failStage>=3) {
    if (failStage==3) { errno=EIO; return -1; }
    *(int *)arg=0; return 0;
  }
  if (request==TIOCMSET && failStage>=4) {
    assert((*(int *)arg&(TIOCM_DTR|TIOCM_RTS))==(TIOCM_DTR|TIOCM_RTS));
    if (failStage==4) { errno=EIO; return -1; }
    return 0;
  }
  return __real_ioctl(fd,request,arg);
}
ssize_t __real_write(int,const void *,size_t);
ssize_t __wrap_write(int fd,const void *buf,size_t n) {
  if (interrupted) { interrupted=0; errno=EINTR; return -1; }
  return __real_write(fd,buf,partial && n>2 ? 2:n);
}
static int fdcount(void) {
  DIR *d=opendir("/proc/self/fd"); assert(d);
  int count=0; while(readdir(d)) ++count; closedir(d); return count;
}
int main(void) {
  int base=fdcount();
  assert(serialOpen("/dev/null",12345)==-2 && errno==EINVAL);
  assert(serialOpen(NULL,115200)==-1 && errno==EINVAL);
  for (int i=0;i<20;++i) assert(serialOpen("/dev/null",115200)==-1 && errno==ENOTTY);
  assert(fdcount()==base);
  int master=posix_openpt(O_RDWR|O_NOCTTY|O_CLOEXEC); assert(master>=0);
  assert(!grantpt(master) && !unlockpt(master));
  for (failStage=1;failStage<=4;++failStage) {
    assert(serialOpen(ptsname(master),115200)==-1 && errno==EIO);
    assert(fdcount()==base+1);
  }
  int controlled=serialOpen(ptsname(master),115200); assert(controlled>=0);
  serialClose(controlled); failStage=0;
  int slave=serialOpen(ptsname(master),115200); assert(slave>=0);
  assert(fcntl(slave,F_GETFD)&FD_CLOEXEC);
  struct termios attr; assert(!tcgetattr(slave,&attr));
  assert(cfgetispeed(&attr)==B115200 && cfgetospeed(&attr)==B115200);
  assert(attr.c_cc[VMIN]==0 && attr.c_cc[VTIME]==100);
  assert(!(attr.c_lflag&(ICANON|ECHO)) && !(attr.c_cflag&CRTSCTS));
  assert(write(master,"\0\xffZ",3)==3);
  for (int i=0;i<100 && serialDataAvail(slave)!=3;++i) usleep(1000);
  assert(serialDataAvail(slave)==3);
  assert(serialGetchar(slave)==0 && serialGetchar(slave)==255 && serialGetchar(slave)=='Z');
  partial=1; interrupted=1;
  serialPutchar(slave,'A'); assert(errno==0);
  serialPuts(slave,"BCDE"); assert(errno==0);
  serialPrintf(slave,"%s:%d","FG",42); assert(errno==0);
  char buf[11]={0}; size_t got=0;
  while(got<10) { ssize_t n=read(master,buf+got,10-got); assert(n>0); got+=(size_t)n; }
  assert(!strcmp(buf,"ABCDEFG:42"));
  partial=0;
  assert(write(master,"flush",5)==5);
  for (int i=0;i<100 && serialDataAvail(slave)!=5;++i) usleep(1000);
  serialFlush(slave); assert(serialDataAvail(slave)==0);
  attr.c_cc[VTIME]=1; assert(!tcsetattr(slave,TCSANOW,&attr));
  assert(serialGetchar(slave)==-1); /* bounded no-data timeout */
  serialPuts(slave,NULL); assert(errno==EINVAL);
  serialClose(slave); assert(fcntl(slave,F_GETFD)==-1 && errno==EBADF);
  assert(serialDataAvail(slave)==-1 && errno==EBADF);
  serialPutchar(slave,'x'); assert(errno==EBADF);
  close(master); assert(fdcount()==base);
  puts("Serial PTY tests passed: all 8 APIs, 8N1, binary RX, partial/EINTR TX, flush, timeout, cleanup");
}
