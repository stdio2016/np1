#pragma once
#ifndef MYPACK_INCLUDED
#define MYPACK_INCLUDED

#define MAXPACKETSIZE 4+65536

struct MyPack {
  int finished;
  int size;
  unsigned char buf[MAXPACKETSIZE];
};

void OutOfMemory();

void initPacket(struct MyPack *buf);
unsigned getPacketType(struct MyPack *p);
unsigned getPacketSize(struct MyPack *p);
int recvPacket(int sock, struct MyPack *buf);
void setPacketHeader(struct MyPack *p, unsigned type, unsigned size);
int sendPacket(int sock, struct MyPack *buf);

int packetFinished(struct MyPack *p);
#endif
