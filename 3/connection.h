#pragma once
#ifndef CONNECTION_INCLUDED
#define CONNECTION_INCLUDED
#include <netinet/in.h>
#include "queue.h"
#include "mypack.h"

#define NAME ('N'<<8|'M')
#define PUT  ('P'<<8|'U')
#define DATA ('D'<<8|'A')
#define CHECK ('C'<<8|'H')
#define OK   ('O'<<8|'K')
#define ERROR ('E'<<8|'R')
#define CANCEL ('C'<<8|'A')

extern int isServer;

enum SendState {
  SendState_NONE, SendState_STARTING, SendState_SENDING, SendState_CHECKING
};

enum RecvState {
  RecvState_NONE, RecvState_RECEIVING, RecvState_CHECKING, RecvState_OK, RecvState_ERROR
};

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

struct QueueItem {
  int fileId;
  char *filename;
  long filesize;
};

// store client info
struct client_info {
  char name[256];
  union good_sockaddr addr; // store client's IP and port
  struct MyPack recv; // buffer for client message
  struct MyPack send; // buffer for server -> client
  struct Queue sendQueue;
  FILE *fileToSend;
  FILE *fileToRecv;
  enum SendState isSending;
  enum RecvState isRecving;
  int closed;
  char recvFilename[256];
} *Clients;
struct pollfd *ClientFd;

void initClient(int clientId, union good_sockaddr addr);
void processClient(int clientId, int socketId);
void destroyClient(int clientId);

#endif
