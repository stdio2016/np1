#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include "mypack.h"

void OutOfMemory() {
  printf("Out of memory!\n");
  exit(-1);
}

void initPacket(struct MyPack *buf) {
  buf->finished = 0;
  buf->size = 0;
}

unsigned getPacketType(struct MyPack *p) {
  return p->buf[0]<<8 | p->buf[1];
}

unsigned getPacketSize(struct MyPack *p) {
  return p->buf[2]<<8 | p->buf[3];
}

int recvPacket(int sock, struct MyPack *buf) {
  int n;
  if (buf->size > 0 && buf->finished == buf->size) {
    buf->finished = buf->size = 0;
  }
  if (buf->finished < 4) {
    do {
      n = recv(sock, buf->buf, 4 - buf->finished, MSG_DONTWAIT);
    } while (n < 0 && errno == EINTR) ;
    if (n == 0) return 0;
    if (n < 0) return -1;
    buf->finished += n;
  }
  if (buf->finished == 4 && buf->size == 0) {
    buf->size = getPacketSize(buf) + 4;
  }
  if (buf->size > 0) {
    do {
      n = recv(sock, buf->buf, buf->size - buf->finished, MSG_DONTWAIT);
    } while (n < 0 && (errno == EINTR) ) ;
    if (n == 0) return 0;
    if (n < 0) return -1;
    buf->finished += n;
    if (buf->finished == buf->size) return buf->size;
    errno = EWOULDBLOCK;
    return -1;
  }
}

void setPacketHeader(struct MyPack *p, unsigned type, unsigned size) {
  p->buf[0] = (type>>8) & 0xFF;
  p->buf[1] = type & 0xFF;
  p->buf[2] = (size>>8) & 0xFF;
  p->buf[3] = size & 0xFF;
  p->size = size + 4;
  p->finished = 0;
}

int sendPacket(int sock, struct MyPack *buf) {
  int n;
  do {
    n = send(sock, buf->buf, buf->size - buf->finished, MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR) ;
  if (n == 0) return 0;
  if (n < 0) return -1;
  buf->finished += n;
  if (buf->finished == buf->size) return buf->size;
  errno = EWOULDBLOCK;
  return -1;
}
