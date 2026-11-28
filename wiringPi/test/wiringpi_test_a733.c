/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include "wiringPiH618.h"
#include "wiringPiH618Line.h"

static int wrong, missing, duplicate_chip, selected, offset;
static const char dt[]="xunlong,orangepi-zero3w\0arm,sun60iw2p1";
FILE *__wrap_fopen(const char *path,const char *mode) {
  assert(!strcmp(path,"/proc/device-tree/compatible"));
  return fmemopen((void *)dt,sizeof(dt),mode);
}
int __wrap_glob(const char *pat,int flags,int (*err)(const char*,int),glob_t *g) {
  (void)pat; (void)flags; (void)err;
  static char *devices[]={"/dev/gpiochip7","/dev/gpiochip4","/dev/gpiochip9"};
  g->gl_pathv=devices; g->gl_pathc=missing?1:duplicate_chip?3:2;
  return 0;
}
void __wrap_globfree(glob_t *g) {(void)g;}
int __wrap_open(const char *p,int f,...) {(void)f; return p[strlen(p)-1]-'0';}
int __wrap_close(int fd) {(void)fd; return 0;}
int __wrap_fstat(int fd,struct stat *st) {
  memset(st,0,sizeof(*st)); st->st_mode=S_IFCHR; st->st_rdev=makedev(254,fd); return 0;
}
char *__wrap_realpath(const char *p,char *out) {
  if (!strncmp(p,"/sys/dev/char/254:",18))
    return sprintf(out,"/sys/devices/platform/ctl%c/gpiochip",p[strlen(p)-1]),out;
  if (strstr(p,"/gpiochip/of_node")) return NULL;
  if (strstr(p,"/of_node"))
    return strcpy(out,strstr(p,"ctl7") ? "/sys/firmware/devicetree/base/soc@3000000/pinctrl@7025000" :
                                          "/sys/firmware/devicetree/base/soc@3000000/pinctrl@2000000");
  return NULL;
}
int __wrap_ioctl(int fd,unsigned long op,...) {
  va_list ap; va_start(ap,op);void *arg=va_arg(ap,void *);va_end(ap);
  if (op==GPIO_GET_CHIPINFO_IOCTL) {
    ((struct gpiochip_info *)arg)->lines=fd==7?64:352; return 0;
  }
  if (op==GPIO_V2_GET_LINEINFO_IOCTL) {
    struct gpio_v2_line_info *line=arg;
    if (wrong) strcpy(line->name,"foreign");
    selected=fd; offset=line->offset; return 0;
  }
  assert(op==GPIO_V2_GET_LINE_IOCTL);
  struct gpio_v2_line_request *r=arg; selected=fd;offset=r->offsets[0];
  assert(!strcmp(r->consumer,"wiringpi-a733-line"));
  r->fd=15; return 0;
}
int main(void) {
  static const int physical[]={3,5,7,8,10,11,12,13,15,16,18,19,21,22,23,24,26,27,28,29,31,32,33,35,36,37,38,40};
  static const int native[]={35,34,36,41,42,32,37,33,139,354,355,130,131,96,129,128,132,357,356,140,141,97,99,38,98,100,40,39};
  assert(a733Compatible(dt,sizeof(dt)) && !h618Compatible(dt,sizeof(dt)));
  wrong=1; assert(h618Setup()<0); wrong=0;
  missing=1; assert(h618Setup()<0); missing=0;
  duplicate_chip=1; assert(h618Setup()<0); duplicate_chip=0;
  assert(h618Setup()==0);
  for (size_t i=0;i<28;i++) {
    assert(wiringPiA733HeaderToNative(physical[i])==native[i]);
    assert(h618Wpi(i)==native[i] && h618Physical(physical[i])==native[i]);
    assert(wiringPiA733LineRequest(physical[i],0,0,0,0,0)==15);
    assert(selected==(native[i]>=352?7:4) && offset==native[i]-(selected==7?352:0));
  }
  assert(wiringPiA733HeaderToNative(6)<0);
  assert(wiringPiH618LineRequest(16,0,0,0,0,0)<0 && errno==ENODEV);
  h618Close(); puts("A733: all 28 physical pins, two controllers, ownership and mismatched layouts passed");
}
