// SPDX-License-Identifier: LGPL-3.0-or-later
// SPI API tests with fake system calls: no GPIO device is opened or driven.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/spi/spidev.h>
#include "wiringPiSPI.h"

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); \
} } while (0)

static int nextFd = 100, lastClosed = -1, calls, failOpen;
static unsigned long failRequest;
static int opened, closed;
static char openedPath[64];
static unsigned int seenMode, seenSpeed, seenBits;

int wpi_test_open(const char *path, int flags, ...) {
  ++calls;
  CHECK(flags == (O_RDWR | O_CLOEXEC));
  snprintf(openedPath, sizeof(openedPath), "%s", path);
  if (failOpen) { errno = ENOENT; return -1; }
  ++opened;
  return nextFd++;
}

int wpi_test_close(int fd) {
  ++calls;
  ++closed;
  lastClosed = fd;
  return 0;
}

int wpi_test_ioctl(int fd, unsigned long request, ...) {
  va_list ap;
  va_start(ap, request);
  void *arg = va_arg(ap, void *);
  va_end(ap);
  ++calls;
  CHECK(fd >= 0);
  if (request == failRequest) { errno = EIO; return -1; }
  if (request == SPI_IOC_WR_MODE) seenMode = *(uint8_t *)arg;
  else if (request == SPI_IOC_WR_BITS_PER_WORD) seenBits = *(uint8_t *)arg;
  else if (request == SPI_IOC_WR_MAX_SPEED_HZ) seenSpeed = *(uint32_t *)arg;
  else {
    CHECK(request == SPI_IOC_MESSAGE(1));
    struct spi_ioc_transfer *transfer = arg;
    CHECK(transfer->bits_per_word == 8);
    CHECK(transfer->speed_hz == seenSpeed);
    CHECK(transfer->tx_buf == transfer->rx_buf);
    CHECK(transfer->len == 3);
    unsigned char *data = (unsigned char *)(uintptr_t)transfer->rx_buf;
    CHECK(data[0] == 0x12 && data[1] == 0x34 && data[2] == 0x56);
    data[0] = 0xa5;
    return (int)transfer->len;
  }
  return 0;
}

int wiringPiFailure(int fatal, const char *message, ...) {
  (void)fatal; (void)message;
  return -1;
}

int main(void) {
  // Every supported bus/CS pair must have independent storage.
  int descriptors[7][5];
  for (int bus = 0; bus < 7; ++bus) {
    for (int cs = 0; cs < 5; ++cs) {
      CHECK(wiringPiSPIxGetFd(bus, cs) == -1);
      descriptors[bus][cs] = wiringPiSPIxSetup(bus, cs, 500000);
      CHECK(descriptors[bus][cs] >= 0);
      char expected[64];
      snprintf(expected, sizeof(expected), "/dev/spidev%d.%d", bus, cs);
      CHECK(strcmp(openedPath, expected) == 0);
    }
  }
  for (int bus = 0; bus < 7; ++bus) {
    for (int cs = 0; cs < 5; ++cs) {
      CHECK(wiringPiSPIxGetFd(bus, cs) == descriptors[bus][cs]);
      CHECK(wiringPiSPIxClose(bus, cs) == 0);
      CHECK(lastClosed == descriptors[bus][cs]);
      CHECK(wiringPiSPIxGetFd(bus, cs) == -1);
    }
  }
  // Test all SPI modes and the receive buffer/transfer length contract.
  for (int mode = 0; mode < 4; ++mode) {
    CHECK(wiringPiSPIxSetupMode(1, 4, 125000 + mode, mode) >= 0);
    CHECK(seenMode == (unsigned)mode && seenSpeed == (unsigned)(125000 + mode));
    CHECK(seenBits == 8);
    unsigned char data[] = {0x12, 0x34, 0x56};
    failRequest = SPI_IOC_MESSAGE(1);
    CHECK(wiringPiSPIxDataRW(1, 4, data, 3) == -1);
    CHECK(errno == EIO && data[0] == 0x12);
    failRequest = 0;
    CHECK(wiringPiSPIxDataRW(1, 4, data, 3) == 3);
    CHECK(data[0] == 0xa5);
    CHECK(wiringPiSPIxClose(1, 4) == 0);
  }
  // Legacy wrappers select bus zero, including a valid descriptor of zero.
  nextFd = 0;
  CHECK(wiringPiSPISetup(0, 1000000) == 0);
  CHECK(wiringPiSPIGetFd(0) == 0);
  CHECK(wiringPiSPIClose(0) == 0);
  CHECK(lastClosed == 0);
  CHECK(wiringPiSPISetupMode(1, 250000, 3) == 1);
  CHECK(strcmp(openedPath, "/dev/spidev0.1") == 0);
  unsigned char data[] = {0x12, 0x34, 0x56};
  CHECK(wiringPiSPIDataRW(1, data, 3) == 3);
  CHECK(wiringPiSPIClose(1) == 0);
  int before = calls;
  CHECK(wiringPiSPIxDataRW(1, 4, data, 3) == -EBADF);
  CHECK(wiringPiSPIxClose(1, 4) == 0);
  CHECK(wiringPiSPIxSetupMode(1, 0, 500000, -1) == -EINVAL);
  CHECK(wiringPiSPIxSetupMode(1, 0, 500000, 4) == -EINVAL);
  const int invalid[][2] = {{-1, 0}, {7, 0}, {0, -1}, {0, 5}};
  for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
    int bus = invalid[i][0], cs = invalid[i][1];
    CHECK(wiringPiSPIxGetFd(bus, cs) == -1);
    CHECK(wiringPiSPIxSetup(bus, cs, 500000) == -EINVAL);
    CHECK(wiringPiSPIxDataRW(bus, cs, data, 3) == -EINVAL);
    CHECK(wiringPiSPIxClose(bus, cs) == -EINVAL);
  }
  CHECK(calls == before);
  failOpen = 1;
  CHECK(wiringPiSPIxSetup(1, 0, 500000) == -1);
  CHECK(wiringPiSPIxGetFd(1, 0) == -1);
  failOpen = 0;
  CHECK(opened == closed);
  /* Each failed setup stage closes the new descriptor, leaving no stale cache. */
  const unsigned long stages[] = {SPI_IOC_WR_MODE, SPI_IOC_WR_BITS_PER_WORD, SPI_IOC_WR_MAX_SPEED_HZ};
  for (unsigned i=0; i<3; ++i) {
    failRequest = stages[i];
    CHECK(wiringPiSPIxSetupMode(1,0,1000000,0) == -1 && errno == EIO);
    CHECK(wiringPiSPIxGetFd(1,0) == -1 && opened == closed);
  }
  failRequest=0;
  int first=wiringPiSPIxSetupMode(1,0,1000000,0);
  int second=wiringPiSPIxSetupMode(1,0,500000,1);
  CHECK(first >= 0 && second > first && lastClosed == first);
  CHECK(opened == closed+1 && wiringPiSPIxGetFd(1,0) == second);
  before=calls;
  CHECK(wiringPiSPIxDataRW(1,0,NULL,3) == -EINVAL);
  CHECK(wiringPiSPIxDataRW(1,0,data,-1) == -EINVAL);
  CHECK(wiringPiSPIxDataRW(1,0,data,0) == -EINVAL);
  CHECK(wiringPiSPIxSetupMode(1,0,0,0) == -EINVAL);
  CHECK(wiringPiSPIxSetupMode(1,0,-100,0) == -EINVAL);
  CHECK(calls == before && wiringPiSPIxGetFd(1,0) == second);
  failOpen=1;
  CHECK(wiringPiSPIxSetupMode(1,0,1000000,0) == -1);
  CHECK(wiringPiSPIxGetFd(1,0) == second && opened == closed+1);
  failOpen=0; failRequest=SPI_IOC_WR_MAX_SPEED_HZ;
  CHECK(wiringPiSPIxSetupMode(1,0,1000000,0) == -1 && errno == EIO);
  CHECK(wiringPiSPIxGetFd(1,0) == -1 && opened == closed);
  puts("SPI host tests passed: all 10 public SPI entry points; no hardware accessed.");
  return 0;
}
