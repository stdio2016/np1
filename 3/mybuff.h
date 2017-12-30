#pragma once
#ifndef MYBUFF_INCLUDED
#define MYBUFF_INCLUDED

#define MAXPACKETSIZE 4+65536

struct mybuff {
  int finished;
  int size;
  unsigned char buf[MAXPACKETSIZE];
};

void OutOfMemory();

void initBuffer(struct mybuff *buf);
int recvPacket(int sock, struct mybuff *buf);
#endif
