#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include "connection.h"

void getSaferName(char *name) {
  size_t i, j = 0;
  for (i = 0; name[i]; i++) {
    if (name[i] == '/') {
      j = i+1;
    }
  }
  if (j > 0) {
    memmove(name, &name[j], i-j+1);
  }
}

void initClient(int clientId, union good_sockaddr addr) {
  strcpy(Clients[clientId].name, "");
  Clients[clientId].addr = addr;
  Clients[clientId].closed = 0;
  initPacket(&Clients[clientId].recv);
  initPacket(&Clients[clientId].send);
}

void destroyClient(int clientId) {
  ClientFd[clientId].fd = -1;
}

void sendToClient(int clientId) {
  struct MyPack *b = &Clients[clientId].send;
  int n = 0;
  n = sendPacket(ClientFd[clientId].fd, b);
  if (n > 0) return; // hooray!
  if (n == 0) { // connection closed
    Clients[clientId].closed = 1;
    return ;
  }
  if (n < 0) {
    n = 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ;
    }
    else { // error or connection closed
      printf("send to client %d error %d\n", clientId, errno);
      Clients[clientId].closed = 1;
      return ;
    }
  }
  ClientFd[clientId].events |= POLLWRNORM;
}

void trySendToClientAgain(int clientId) {
  int all = Clients[clientId].send.size;
  int finished = Clients[clientId].send.finished;
  char *msg = &Clients[clientId].send.buf[finished];
  int n;
  do {
    n = send(ClientFd[clientId].fd, msg, all - finished, MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR) ;
  if (n == 0) { // connection closed, I give up
    Clients[clientId].closed = 1;
    ClientFd[clientId].events &= ~POLLWRNORM;
    return ;
  }
  else if (n < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) { // some error that I don't know
      Clients[clientId].closed = 1;
      ClientFd[clientId].events &= ~POLLWRNORM;
      return ;
    }
  }
  else {
    Clients[clientId].send.finished += n;
    if (Clients[clientId].send.finished == all) { // completed
      Clients[clientId].send.finished = 0;
      Clients[clientId].send.size = 0;
      ClientFd[clientId].events &= ~POLLWRNORM;
    }
  }
}

void processMessage(int clientId, struct MyPack *msg) {
  int y = getPacketType(msg);
  if (y == CHECK) {
    if (isServer)
      printf("Client %d (%s) uploaded %s\n", clientId,  Clients[clientId].name , Clients[clientId].recvFilename);
    ClientFd[clientId].events |= POLLWRNORM;
    Clients[clientId].isRecving = RecvState_OK;
  }
  else if (y == DATA) {
    ;
  }
  else if (y == PUT) {
    int n = getPacketSize(msg);
    if (n > 255) {
      n = 255;
    }
    memcpy(Clients[clientId].recvFilename, msg->buf+4, n);
    Clients[clientId].recvFilename[n] = '\0';
    getSaferName(Clients[clientId].recvFilename);
    if (isServer)
      printf("Client %d uploads '%s'\n", clientId, Clients[clientId].recvFilename);
  }
  else if (y == NAME) {
    if (Clients[clientId].name[0] != '\0') return ; // already have name
    int n = getPacketSize(msg);
    if (n > 255) {
      n = 255;
    }
    memcpy(Clients[clientId].name, msg->buf+4, n);
    Clients[clientId].name[n] = '\0';
    if (isServer)
      printf("Client %d is now known as %s\n", clientId, Clients[clientId].name);
  }
}

void sendCheckResult(int clientId) {
  struct MyPack *p = &Clients[clientId].send;
  enum RecvState s = Clients[clientId].isRecving;
  if (s == RecvState_OK) {
    setPacketHeader(p, OK, 0);
    sendToClient(clientId);
  }
  if (s == RecvState_ERROR) {
    setPacketHeader(p, ERROR, 0);
    sendToClient(clientId);
  }
  Clients[clientId].isRecving = RecvState_NONE;
}

void sendQueuedData(int clientId) {
  if (Clients[clientId].sendQueue.size == 0) {
    Clients[clientId].isSending = SendState_NONE;
    ClientFd[clientId].events &= ~POLLWRNORM;
    return ;
  }
  struct MyPack *p = &Clients[clientId].send;
  struct QueueItem *qi = queueFirst(&Clients[clientId].sendQueue);
  if (Clients[clientId].isSending == SendState_SENDING) {
    int big = 1000;
    int y = fread(p->buf+4, 1, big, Clients[clientId].fileToSend);
    if (y == 0) {
      setPacketHeader(p, CHECK, 0);
      sendToClient(clientId);
      Clients[clientId].isSending = SendState_CHECKING;
      ClientFd[clientId].events &= ~POLLWRNORM;
    }
    else if (y < 0) {
      printf("error reading file '%s'!\n", qi->filename);
      Clients[clientId].isSending = SendState_STARTING;
    }
    else {
      setPacketHeader(p, DATA, y);
      sendToClient(clientId);
    }
  }
  else if (Clients[clientId].isSending == SendState_STARTING) {
    char numstr[25];
    sprintf(numstr, "%d", qi->fileId);
    Clients[clientId].fileToSend = fopen(numstr, "rb");
    if (Clients[clientId].fileToSend == NULL) {
      printf("error opening file '%s'!\n", qi->filename);
      Clients[clientId].isSending = SendState_STARTING;
    }
    else {
      int n = strlen(qi->filename);
      setPacketHeader(p, PUT, n);
      memcpy(p->buf+4, qi->filename, n);
      sendToClient(clientId);
      Clients[clientId].isSending = SendState_SENDING;
    }
  }
  else if (Clients[clientId].isSending == SendState_CHECKING) {
    ClientFd[clientId].events &= ~POLLWRNORM;
  }
  else if (Clients[clientId].isSending == SendState_CHECKING) {
    setPacketHeader(&Clients[clientId].send, OK, 0);
    sendToClient(clientId);
  }
  if (Clients[clientId].isSending == SendState_STARTING) {
    queuePop(&Clients[clientId].sendQueue);
    if (Clients[clientId].sendQueue.size == 0) {
      ClientFd[clientId].events &= ~POLLWRNORM;
      Clients[clientId].isSending = SendState_NONE;
    }
  }
}

void processClient(int clientId, int socketId) {
  // socketId is socket of client #clientId
  if (!Clients[clientId].closed) {
    int n = recvPacket(socketId, &Clients[clientId].recv);
    if (n < 0) {
      if (errno == ECONNRESET || errno == EPIPE) {
        Clients[clientId].closed = 1;
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ; // not finished
      }
      else {
        printf("recvPacket error\n");
        Clients[clientId].closed = 1;
      }
    }
    else if (n == 0) {
      Clients[clientId].closed = 1;
    }
    else {
      processMessage(clientId, &Clients[clientId].recv);
    }
  }
  if (!Clients[clientId].closed && (ClientFd[clientId].revents & POLLWRNORM)) {
    if (packetFinished(&Clients[clientId].send)) {
      if (Clients[clientId].isRecving == RecvState_OK
       || Clients[clientId].isRecving == RecvState_ERROR) {
        sendCheckResult(clientId);
      }
      else {
        sendQueuedData(clientId);
      }
    }
    else {
      trySendToClientAgain(clientId);
    }
  }
  if (Clients[clientId].closed) {
    if (isServer)
      printf("client %d closed connection\n", clientId);
    shutdown(socketId, SHUT_RDWR);
    destroyClient(clientId);
  }
}

