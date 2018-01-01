#pragma once
#ifndef CONNECTION_INCLUDED
#define CONNECTION_INCLUDED
#include <netinet/in.h>
#include "queue.h"
#include "mypack.h"
#include "MyHash.h"

#define NAME ('N'<<8|'M')
#define PUT  ('P'<<8|'U')
#define DATA ('D'<<8|'A')
#define CHECK ('C'<<8|'H')
#define OK   ('O'<<8|'K')
#define ERROR ('E'<<8|'R')
#define CANCEL ('C'<<8|'A')

extern int isServer;

enum SendState {
  SendState_STARTING, SendState_SENDING, SendState_CHECKING, SendState_RESEND, SendState_CANCEL
};

enum RecvState {
  RecvState_NONE, RecvState_RECEIVING, RecvState_CHECKING, RecvState_OK, RecvState_ERROR
};

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

struct QueueItem {
  char *filename;
  int fileId;
};

// store client info
struct client_info {
  char name[256];
  int userId;
  union good_sockaddr addr; // store client's IP and port
  struct MyPack recv; // buffer for client message
  struct MyPack send; // buffer for server -> client
  struct Queue sendQueue;
  FILE *fileToSend;
  FILE *fileToRecv;
  off_t sendFilesize;
  off_t recvFilesize;
  enum SendState isSending;
  enum RecvState isRecving;
  int closed;
  char recvFilename[256];
  int recvFileId; // temporary file number
  int saveFileId; // server only file number
} *Clients;
struct pollfd *ClientFd;
int fileId;

struct FileEntry {
  char filename[256];
  int fileId;
};

struct UserEntry{
  char name[256];
  int id;
  struct MyHash files;
};

extern struct MyHash users; // name -> UserEntry

void initClient(int clientId, union good_sockaddr addr);
void processClient(int clientId, int socketId);
void destroyClient(int clientId);
void sendToClient(int clientId);

void initUserTable();

#endif
