/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Fake kernel: two chips, non-native offsets, busy lines and ioctl failures. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wiringPi.h"
#include "wiringPiH618.h"
static int requests, closes, fail, busy, value, configures, chipinfo_fail, unnamed;
static uint64_t last_flags;
static const int expected[] = {264,263,269,224,225,226,257,227,261,270,228,231,232,262,230,229,233,266,265,256,271,267,268,258,76,272,260,259};
int wpi_test_glob(const char *pat, int flags, int (*err)(const char *, int), glob_t *g) {
  (void)flags; (void)err;
  static char *paths[] = {"/dev/gpiochip4", "/dev/gpiochip7"};
  assert(!strcmp(pat, "/dev/gpiochip*"));
  g->gl_pathc = 2; g->gl_pathv = paths; return 0;
}
void wpi_test_globfree(glob_t *g) { (void)g; }
int wpi_test_open(const char *p, int flags, ...) {
  assert(flags == (O_RDONLY | O_CLOEXEC));
  return !strcmp(p, "/dev/gpiochip4") ? 4 : 7;
}
int wpi_test_close(int fd) { assert(fd == 0 || fd == 4 || fd == 7); ++closes; return 0; }
int wpi_test_ioctl(int fd, unsigned long op, ...) {
  va_list ap; va_start(ap,op); void *arg=va_arg(ap,void *); va_end(ap);
  if (op == GPIO_GET_CHIPINFO_IOCTL) {
    if (chipinfo_fail) { errno = EIO; return -1; }
    struct gpiochip_info *i=arg; i->lines=unnamed ? 288 : 28;
    strcpy(i->label,fd==4?"other-chip":"300b000.pinctrl"); return 0;
  }
  if (op == GPIO_V2_GET_LINEINFO_IOCTL) {
    struct gpio_v2_line_info *i=arg;
    assert(fd==7 && i->offset < (unnamed ? 288u : 28u));
    if (!unnamed) {
      int n=expected[i->offset];
      snprintf(i->name,sizeof(i->name),"P%c%d",'A'+n/32,n%32);
    }
    if (unnamed == 2 && i->offset == 264) strcpy(i->name, "custom-name");
    i->flags=GPIO_V2_LINE_FLAG_USED; strcpy(i->consumer,"spi1"); return 0;
  }
  if (op == GPIO_V2_GET_LINE_IOCTL) {
    struct gpio_v2_line_request *r=arg;
    assert(fd==7 && r->offsets[0]==(unnamed ? 264u : 0u) && r->num_lines==1);
    ++requests;
    if (busy) { errno=EBUSY; return -1; }
    last_flags=r->config.flags; r->fd=0; return 0; /* FD zero is valid. */
  }
  assert(fd==0);
  if (op == GPIO_V2_LINE_SET_CONFIG_IOCTL) {
    ++configures;
    if (fail) { errno=EIO; return -1; }
    struct gpio_v2_line_config *c=arg;
    last_flags=c->flags;
    if (last_flags & GPIO_V2_LINE_FLAG_OUTPUT) {
      assert(c->attrs[0].mask==1); assert((int)c->attrs[0].attr.values==value);
    }
    return 0;
  }
  struct gpio_v2_line_values *v=arg; assert(v->mask==1);
  if (op==GPIO_V2_LINE_GET_VALUES_IOCTL) {v->bits=value; return 0;}
  assert(op==GPIO_V2_LINE_SET_VALUES_IOCTL); value=v->bits; return 0;
}
#ifdef WPI_INTEGRATION
extern int h618Command(int argc, char **argv, int mode);
int __wrap_h618Detected(void) { return 1; }
#endif
int main(void) {
  const char compat[]="xunlong,orangepi-zero2w\0allwinner,sun50i-h618\0";
  assert(h618Compatible(compat,sizeof(compat)));
  assert(!h618Compatible(compat,sizeof(compat)-2));
  const char other[]="xunlong,orangepi-zero3\0allwinner,sun50i-h618\0";
  assert(!h618Compatible(other,sizeof(other)));
  for (int i=0;i<28;++i) assert(h618Wpi(i)==expected[i]);
  assert(h618Map(3,WPI_MODE_PHYS)==264);
  assert(h618Map(0,WPI_MODE_PINS)==264);
  assert(h618Map(264,WPI_MODE_GPIO)==264);
  assert(h618Map(264,WPI_MODE_PINS)==-1);
  assert(h618Physical(1)==-1 && h618Physical(0)==-1 && h618Physical(41)==-1);
  assert(h618Wpi(-1)==-1 && h618Wpi(64)==-1 && h618Map(20,WPI_MODE_GPIO)==-1);
  assert(h618Read(264)==-1 && errno==ENODEV);
  chipinfo_fail=1; assert(h618Setup()==-1); chipinfo_fail=0;
  assert(h618Setup()==0); assert(h618Setup()==0);
  uint64_t flags; char consumer[32];
  assert(h618Info(264,&flags,consumer,sizeof(consumer))==0);
  assert(!strcmp(consumer,"spi1") && requests==0); /* readall doesn't claim pins */
  busy=1; assert(h618Read(264)==-1 && errno==EBUSY); busy=0;
  assert(h618PinMode(264,OUTPUT)==0);
  assert(last_flags & GPIO_V2_LINE_FLAG_OUTPUT);
  assert(h618Write(264,1)==0 && h618Read(264)==1);
  assert(h618Pull(264,PUD_UP)==0 && configures==1);
  assert((last_flags & GPIO_V2_LINE_FLAG_OUTPUT) && (last_flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP));
  fail=1; assert(h618PinMode(264,INPUT)==-1); fail=0;
  assert(h618Write(264,0)==0); /* failed reconfigure preserves output */
  assert(h618PinMode(264,INPUT)==0);
  assert(h618Write(264,1)==-1 && errno==EPERM);
  assert(h618PinMode(264,PWM_OUTPUT)==-1 && errno==ENOTSUP);
  assert(h618PinMode(264,PM_OFF)==0);
  int old=closes; h618Close(); assert(closes==old+1); h618Close(); assert(closes==old+1);
  assert(h618Read(264)==-1 && errno==ENODEV);
#ifdef WPI_INTEGRATION
  int model, rev, mem, maker, warranty;
  piBoardId(&model, &rev, &mem, &maker, &warranty);
  assert(model == PI_MODEL_ORANGEPI_ZERO_2W);
  assert(!strcmp(piModelNames[model], "Orange Pi Zero 2W"));
  assert(maker == PI_MAKER_XUNLONG && !strcmp(piMakerNames[maker], "Xunlong"));
  assert(piMemorySize[mem] == 0);
  setenv("WIRINGPI_FORCE_REVISION", "a32082", 1);
  piBoardId(&model, &rev, &mem, &maker, &warranty);
  assert(model == PI_MODEL_3B && maker == 3);
  assert(!strcmp(piMakerNames[maker], "Sony Japan"));
  assert(h618Detected()); /* The test hook must not change hardware dispatch. */
  unsetenv("WIRINGPI_FORCE_REVISION");
  piBoardId(&model, &rev, &mem, &maker, &warranty);
  assert(model == PI_MODEL_ORANGEPI_ZERO_2W && maker == PI_MAKER_XUNLONG);
  assert(wiringPiSetup() == 0);
  assert(wpiPinToGpio(0) == 264 && physPinToGpio(3) == 264);
  pinMode(0, OUTPUT); digitalWrite(0, 1); assert(digitalRead(0) == 1);
  assert(getAlt(0) == OUTPUT);
  assert(wiringPiSetupPhys() == 0 && digitalRead(3) == 1);
  assert(wiringPiSetupGpio() == 0 && digitalRead(264) == 1);
  assert(digitalRead(1) == -1); /* no BCM masking / alias */
  int held_requests = requests;
  assert(wiringPiSetup() == 0 && digitalRead(0) == 1);
  assert(requests == held_requests); /* Changing numbering preserves ownership. */
  assert(wiringPiSetupSys() == 0 && digitalRead(264) == 1);
  pinMode(264, PM_OFF);
  errno = 0; pwmWrite(264, 12); assert(errno == ENOTSUP);
  assert(wiringPiISR(264, INT_EDGE_BOTH, NULL) == -1 && errno == ENOTSUP);
  assert(wiringPiGlobalMemoryAccess() == 0);
  int before = requests;
  char *args[] = {"gpio", "readall"};
  assert(h618Command(2, args, WPI_MODE_PINS) == 0);
  assert(requests == before);
  h618Close();
  chipinfo_fail = 1;
  assert(wiringPiSetup() == -1); /* A stale setup flag must not hide reopen errors. */
  chipinfo_fail = 0;
  assert(wiringPiSetupPhys() == 0 && digitalRead(3) == 1);
  h618Close();
  assert(wiringPiSetup() == 0 && digitalRead(0) == 1);
  h618Close();
  assert(wiringPiSetupSys() == 0 && digitalRead(264) == 1);
  h618Close();
#endif
  unnamed = 1; /* Standard mainline controller without line names. */
  assert(h618Setup() == 0);
  assert(h618PinMode(264, OUTPUT) == 0);
  assert(h618Write(264, 1) == 0 && h618Read(264) == 1);
  h618Close();
  unnamed = 2; assert(h618Setup() == -1); /* reject unknown named layout */
  puts("H618 host tests passed");
}
