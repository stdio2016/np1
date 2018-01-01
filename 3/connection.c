#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "connection.h"
#include "MyHash.h"

int fileId = 0;
int userId = 0;

struct MyHash users;

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

char itoaBuf[25];
char *myitoa(int num) {
  if (isServer)
    sprintf(itoaBuf, "%d.dat", num);
  else
    sprintf(itoaBuf, "%d.tmp", num);
  return itoaBuf;
}

void initUserTable() {
  MyHash_init(&users, MyHash_strcmp, MyHash_strhash);
}

int getUserId(char name[]) {
  struct UserEntry *ue = MyHash_get(&users, name);
  if (ue == NULL) {
    ue = malloc(sizeof(*ue));
    strcpy(ue->name, name);
    ue->id = ++userId;
    MyHash_init(&ue->files, MyHash_strcmp, MyHash_strhash);
    MyHash_set(&users, ue->name, ue);
  }
  return ue->id;
}

int getFileId(char username[], char filename[]) {
  struct UserEntry *ue = MyHash_get(&users, username);
  if (ue == NULL) { // should not happen
    printf("getFileId: user does not exist\n");
    return 0;
  }
  struct FileEntry *fe = MyHash_get(&ue->files, filename);
  if (fe == NULL) {
    fe = malloc(sizeof(*fe));
    strcpy(fe->filename, filename);
    fe->fileId = ++fileId;
    MyHash_set(&ue->files, fe->filename, fe);
  }
  return fe->fileId;
}

void initClient(int clientId, union good_sockaddr addr) {
  strcpy(Clients[clientId].name, "");
  Clients[clientId].userId = 0;
  Clients[clientId].addr = addr;
  Clients[clientId].closed = 0;
  Clients[clientId].fileToRecv = NULL;
  Clients[clientId].fileToSend = NULL;
  Clients[clientId].isRecving = RecvState_NONE;
  Clients[clientId].isSending = SendState_STARTING;
  strcpy(Clients[clientId].recvFilename, "");
  queueInit(&Clients[clientId].sendQueue);
  initPacket(&Clients[clientId].recv);
  initPacket(&Clients[clientId].send);
}

void deleteReceived(struct client_info *me) {
  if (me->fileToRecv == NULL) return;
  fclose(me->fileToRecv);
  me->fileToRecv = NULL;
  remove(myitoa(me->recvFileId));
}

void renameReceived(struct client_info *me) {
  int yn;
  if (isServer) {
    char ff[25];
    strcpy(ff, myitoa(me->saveFileId));
    yn = remove(ff);
    yn = rename(myitoa(me->recvFileId), ff);
    if (yn != 0) {
      printf("Unable to rename temp file '%s' to '%s'\n", myitoa(me->recvFileId), ff);
    }
  }
  else {
    yn = remove(me->recvFilename);
    yn = rename(myitoa(me->recvFileId), me->recvFilename);
    if (yn != 0) {
      printf("Unable to save file '%s'\n", me->recvFilename);
    }
  }
}

void destroyClient(int clientId) {
  ClientFd[clientId].fd = -1;
  if (Clients[clientId].fileToRecv != NULL) {
    deleteReceived(&Clients[clientId]);
  }
  if (Clients[clientId].fileToSend != NULL) {
    fclose(Clients[clientId].fileToSend);
    Clients[clientId].fileToSend = NULL;
  }
  while (Clients[clientId].sendQueue.size > 0) {
    struct QueueItem *qi = queueFirst(&Clients[clientId].sendQueue);
    free(qi->filename);
    free(qi);
    queuePop(&Clients[clientId].sendQueue);
  }
  queueDestroy(&Clients[clientId].sendQueue);
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
  struct client_info *me = &Clients[clientId];
  int y = getPacketType(msg);
  if (y == CHECK) {
    if (me->fileToRecv == NULL) return ;
    if (isServer)
      printf("Client %d (%s) uploaded %s\n", clientId,  me->name , me->recvFilename);
    ClientFd[clientId].events |= POLLWRNORM;
    int ok = 1;
    if (ok) {
      me->isRecving = RecvState_OK;
      fclose(me->fileToRecv);
      me->fileToRecv = NULL;
      renameReceived(me);
    }
    else {
      me->isRecving = RecvState_ERROR;
      deleteReceived(me);
    }
  }
  else if (y == DATA) {
    if (me->fileToRecv == NULL) return ;
    fwrite(msg->buf+4, 1, getPacketSize(msg), me->fileToRecv);
  }
  else if (y == PUT) {
    if (me->isRecving != RecvState_NONE) {
      deleteReceived(me);
    }
    int n = getPacketSize(msg);
    if (n > 255) {
      n = 255;
    }
    memcpy(me->recvFilename, msg->buf+4, n);
    me->recvFilename[n] = '\0';
    getSaferName(me->recvFilename);
    me->isRecving = RecvState_RECEIVING;
    me->recvFileId = ++fileId;
    if (isServer) {
      int fileId = getFileId(me->name, me->recvFilename);
      printf("Client %d uploads '%s' (file id=%d)\n", clientId, me->recvFilename, fileId);
      me->saveFileId = fileId;
    }
    else {
      printf("Downloading file : %s\n", me->recvFilename);
    }
    me->fileToRecv = fopen(myitoa(me->recvFileId), "wb");
  }
  else if (y == NAME) {
    if (me->name[0] != '\0' || !isServer) return ; // already have name
    int n = getPacketSize(msg);
    if (n > 255) {
      n = 255;
    }
    memcpy(me->name, msg->buf+4, n);
    me->name[n] = '\0';
    me->userId = getUserId(me->name);
    printf("Client %d is now known as %s (uid=%d)\n", clientId, me->name, me->userId);
    // send files to client
    struct MyHashIterator it;
    struct UserEntry *ue = MyHash_get(&users, me->name);
    MyHash_iterate(&ue->files, &it);
    while (it.it != NULL) {
      int n = strlen(it.it->key);
      struct QueueItem *qi = calloc(1, sizeof(*qi));
      qi->filename = malloc(n+1);
      qi->fileId = ((struct FileEntry *)(it.it->value))->fileId;
      strcpy(qi->filename, it.it->key);
      queuePush(&me->sendQueue, qi);
      MyHash_next(&it);
      ClientFd[clientId].events |= POLLWRNORM;
    }
  }
  else if (y == OK) {
    if (me->isSending == SendState_CHECKING) {
      struct QueueItem *qi = queueFirst(&me->sendQueue);
      int i;
      if (isServer) {
        printf("Client %d received %s\n", clientId, qi->filename);
      }
      else {
        printf("\x1B[A\x1B[GProgress : [");
        for (i = 0; i < 32; i++) putchar('#');
        printf("]\n");
        printf("Upload %s complete!\n", qi->filename);
      }
      free(qi);
      fclose(me->fileToSend);
      me->fileToSend = NULL;
      queuePop(&me->sendQueue);
      me->isSending = SendState_STARTING;
      if (me->sendQueue.size > 0) { // can send next file
        ClientFd[clientId].events |= POLLWRNORM;
      }
    }
  }
  else if (y == ERROR) {
    if (me->isSending == SendState_CHECKING) {
      struct QueueItem *qi = queueFirst(&me->sendQueue);
      fclose(me->fileToSend);
      me->fileToSend = NULL;
      me->isSending = SendState_RESEND;
      ClientFd[0].events |= POLLWRNORM;
    }
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
  struct client_info *me = &Clients[clientId];
  if (me->sendQueue.size == 0) {
    me->isSending = SendState_STARTING;
    ClientFd[clientId].events &= ~POLLWRNORM;
    return ;
  }
  struct MyPack *p = &me->send;
  struct QueueItem *qi = queueFirst(&me->sendQueue);
  if (me->isSending == SendState_SENDING) {
    int big = 1000;
    int y = fread(p->buf+4, 1, big, me->fileToSend);
    if (y == 0) {
      setPacketHeader(p, CHECK, 0);
      sendToClient(clientId);
      me->isSending = SendState_CHECKING;
      ClientFd[clientId].events &= ~POLLWRNORM;
    }
    else if (y < 0) {
      printf("error reading file '%s'!\n", qi->filename);
      me->isSending = SendState_STARTING;
    }
    else {
      if (!isServer) {
        printf("\x1B[A\x1B[GProgress : [");
        float pa = me->sendFilesize;
        pa = ftell(me->fileToSend) / pa * 30;
        int i;
        for (i = 0; i < pa; i++) putchar('#');
        for (; i < 32; i++) putchar(' ');
        printf("]\n"); fflush(stdout);
      }
      setPacketHeader(p, DATA, y);
      sendToClient(clientId);
    }
  }
  else if (me->isSending == SendState_STARTING || me->isSending == SendState_RESEND) {
    if (isServer) {
      me->fileToSend = fopen(myitoa(qi->fileId), "rb");
    }
    else {
      me->fileToSend = fopen(qi->filename, "rb");
    }
    if (me->fileToSend == NULL) {
      printf("error opening file '%s'!\n", qi->filename);
      me->isSending = SendState_STARTING;
    }
    else {
      if (isServer) {
        printf("Sending file '%s' to client %d\n", qi->filename, clientId);
      }
      else {
        if (me->isSending != SendState_RESEND)
        printf("Uploading file : %s\n", qi->filename);
        fseek(me->fileToSend, 0, SEEK_END);
        me->sendFilesize = ftell(me->fileToSend);
        rewind(me->fileToSend);
      }
      int n = strlen(qi->filename);
      setPacketHeader(p, PUT, n);
      memcpy(p->buf+4, qi->filename, n);
      sendToClient(clientId);
      me->isSending = SendState_SENDING;
      ClientFd[clientId].events |= POLLWRNORM;
    }
  }
  else if (me->isSending == SendState_CHECKING) {
    ClientFd[clientId].events &= ~POLLWRNORM;
  }
  if (me->isSending == SendState_STARTING) {
    queuePop(&me->sendQueue);
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
    close(socketId);
    destroyClient(clientId);
  }
}
