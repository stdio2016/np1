#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/poll.h> // poll()
#include <sys/socket.h> // connect(), shutdown(), socket(), AF_INET, SOCK_STREAM, SHUT_WR
#include <netinet/in.h> // struct sockaddr_in, htons()
#include <arpa/inet.h> // inet_pton()
#include <netdb.h> // getaddrinfo(), gai_strerror
#include <fcntl.h> // fcntl()
#include "mypack.h"
#include "queue.h"
#define MAXLINE 1000
#define STDIN_FD 0

#define NAME ('N'<<8|'M')
#define PUT  ('P'<<8|'U')
#define DATA ('D'<<8|'A')
#define CHECK ('C'<<8|'H')
#define OK   ('O'<<8|'K')
#define ERROR ('E'<<8|'R')

int sockfd; // socket connected to server
char buf[MAXLINE]; // user input buffer

int maxint(int a, int b) {
  return a>b ? a : b;
}

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

struct pollfd ServerFd[2];
struct MyPack sendServ, recvServ;
struct Queue sendQueue;
struct QueueItem {
  char *filename;
  long filesize;
};
enum SendState {
  STARTING, SENDING, CHECKING
} isSending;
FILE *fileToSend;

int serverClosed = 0;

void buildConnection(char *ipStr, char *portStr) {
  union good_sockaddr servaddr;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf( stderr, "unable to create socket\n" );
    exit(1);
  }
  memset(&servaddr, 0, sizeof (servaddr));
  servaddr.in.sin_family = PF_INET;
  int port;
  if (sscanf(portStr, "%d", &port) != 1) {
    fprintf( stderr, "cannot read port number\n" );
    exit(1);
  }
  servaddr.in.sin_port = htons(port);
  if (inet_pton(AF_INET, ipStr, &servaddr.in.sin_addr.s_addr) == 0) {
    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rv = getaddrinfo(ipStr, portStr, &hints, &servinfo);
    if (rv != 0) {
      fprintf( stderr, "%s\n", gai_strerror(rv));
      exit(1);
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
      union good_sockaddr ipaddr;
      ipaddr.sa = *p->ai_addr;
      servaddr.in.sin_addr.s_addr = ipaddr.in.sin_addr.s_addr;
      int ret = connect(sockfd, &servaddr.sa, sizeof(servaddr));
      if (ret == 0) break;
    }
    if (p == NULL) {
      fprintf( stderr, "unable to connect to server\n" );
      exit(1);
    }
    freeaddrinfo(servinfo);
  }
  else if (connect(sockfd, &servaddr.sa, sizeof(servaddr)) < 0) {
    fprintf( stderr, "unable to connect to server\n" );
    exit(1);
  }

  int flag=fcntl(sockfd,F_GETFL,0);
  fcntl(sockfd,F_SETFL,flag|O_NONBLOCK);
}

void closeConnection(void) {
  shutdown(sockfd, SHUT_RDWR);
}


void sendToServer() {
  int n = 0;
  n = sendPacket(sockfd, &sendServ);
  if (n > 0) return; // hooray!
  if (n == 0) { // connection closed
    serverClosed = 1;
    return ;
  }
  if (n < 0) {
    n = 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ;
    }
    else { // error or connection closed
      printf("send error %d\n", errno);
      serverClosed = 1;
      return ;
    }
  }
  ServerFd[0].events |= POLLWRNORM;
}

void trySendToServerAgain() {
  int all = sendServ.size;
  int finished = sendServ.finished;
  char *msg = &sendServ.buf[finished];
  int n;
  do {
    n = send(sockfd, msg, all - finished, MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR) ;
  if (n == 0) { // connection closed, I give up
    serverClosed = 1;
    ServerFd[0].events &= ~POLLWRNORM;
    return ;
  }
  else if (n < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) { // some error that I don't know
      serverClosed = 1;
      ServerFd[0].events &= ~POLLWRNORM;
      return ;
    }
  }
  else {
    sendServ.finished += n;
    if (sendServ.finished == all) { // completed
      sendServ.finished = 0;
      sendServ.size = 0;
      ServerFd[0].events &= ~POLLWRNORM;
    }
  }
}

void processMessage() {
  int y = getPacketType(&recvServ);
  if (y == OK) {
    if (isSending == CHECKING) {
      struct QueueItem *qi = queueFirst(&sendQueue);
      int i;
      printf("\rProgress : [");
      for (i = 0; i < 32; i++) putchar('#');
      printf("]"); fflush(stdout);
      printf("\nUpload %s complete!\n", qi->filename);
      free(qi);
      queuePop(&sendQueue);
      isSending = STARTING;
      if (sendQueue.size == 0) {
        ServerFd[0].events &= ~POLLWRNORM;
      }
    }
  }
}

void sendQueuedData() {
  struct QueueItem *qi = queueFirst(&sendQueue);
  if (isSending == SENDING) {
    int big = 65535;
    int y = fread(sendServ.buf+4, 1, big, fileToSend);
    if (y == 0) {
      setPacketHeader(&sendServ, CHECK, 0);
      sendToServer();
      isSending = CHECKING;
      ServerFd[0].events &= ~POLLWRNORM;
    }
    else if (y < 0) {
      printf("error reading file '%s'!\n", qi->filename);
      isSending = STARTING;
    }
    else {
      printf("\rProgress : [");
      float pa = qi->filesize;
      pa = ftell(fileToSend) / pa * 30;
      int i;
      for (i = 0; i < pa; i++) putchar('#');
      for (; i < 32; i++) putchar(' ');
      printf("]"); fflush(stdout);
      setPacketHeader(&sendServ, DATA, y);
      sendToServer();
    }
  }
  else if (isSending == STARTING) {
    fileToSend = fopen(qi->filename, "rb");
    if (fileToSend == NULL) {
      printf("error opening file '%s'!\n", qi->filename);
      isSending = STARTING;
    }
    else {
      printf("Uploading file : %s\n", qi->filename);
      fseek(fileToSend, 0, SEEK_END);
      qi->filesize = ftell(fileToSend);
      rewind(fileToSend);
      int n = strlen(qi->filename);
      setPacketHeader(&sendServ, PUT, n);
      memcpy(sendServ.buf+4, qi->filename, n);
      sendToServer();
      isSending = SENDING;
    }
  }
  else if (isSending == CHECKING) {
    ServerFd[0].events &= ~POLLWRNORM;
  }
  if (isSending == STARTING) {
    queuePop(&sendQueue);
    if (sendQueue.size == 0) {
      ServerFd[0].events &= ~POLLWRNORM;
    }
  }
}

void processServer() {
  int still = 1;
  while (!serverClosed && still) {
    still = 0;
    int n = recvPacket(sockfd, &recvServ);
    if (n < 0) {
      if (errno == ECONNRESET || errno == EPIPE) {
        serverClosed = 1;
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ; // not finished
      }
      else {
        printf("recvPacket error\n");
        serverClosed = 1;
      }
    }
    else if (n == 0) {
      serverClosed = 1;
    }
    else {
      processMessage();
    }
  }
  if (!serverClosed && ((ServerFd[0].revents & POLLWRNORM) || sendQueue.size > 0)) {
    if (packetFinished(&recvServ)) {
      sendQueuedData();
    }
    else {
      trySendToServerAgain();
    }
  }
}

char *getArgFromString(char *msg, char **remaining) {
  if (msg == NULL) {
    *remaining = NULL;
    return NULL;
  }
  while (*msg == ' ') msg++; // skip space
  char *d = msg;
  while (*d != ' ' && *d != '\0') {
    d++; // skip argument
  }
  if (*d == '\0') { // end of string
    *remaining = NULL;
  }
  else { // next space
    *d = '\0';
    *remaining = d + 1;
  }
  if (*msg == '\0') return NULL;
  return msg;
}

void readUser() {
  char buf[MAXLINE];
  if (fgets(buf, MAXLINE, stdin) != NULL) {
    int n = strlen(buf);
    buf[n-1] = '\0';
    char *arg;
    char *d = getArgFromString(buf, &arg);
    if (d == NULL) return;
    if (strcmp(d, "/exit") == 0) {
      serverClosed = 881; // so I can quit
    }
    else if (strcmp(d, "/put") == 0) {
      if (arg == NULL || strcmp(arg, "") == 0) {
        printf("Missing filename\n");
        return ;
      }
      int n = strlen(arg);
      if (n > 255) {
        printf("Filename too long\n");
        return ;
      }
      struct QueueItem *qi = calloc(1, sizeof(*qi));
      qi->filename = malloc(n+1);
      strcpy(qi->filename, arg);
      queuePush(&sendQueue, qi);
      ServerFd[0].events |= POLLWRNORM;
    }
    else if (strcmp(d, "/sleep") == 0) {
      if (arg == NULL || strcmp(arg, "") == 0) {
        printf("Missing time\n");
        return ;
      }
      int len = 0, i;
      sscanf(arg, "%d", &len);
      for (i = 0; i < len; i++) {
        struct timespec req = {1, 0};
        printf("Sleep %d\n", i+1);
        nanosleep(&req, NULL);
      }
    }
  }
}

int main(int argc, char *argv[])
{
  char *username;
  signal(SIGPIPE, SIG_IGN);
  if (argc != 4) {
    fprintf( stderr, "usage: %s <ip> <port> <username>\n", argv[0]);
    return 1;
  }
  buildConnection(argv[1], argv[2]);
  username = argv[3];
  ServerFd[0].fd = sockfd;
  ServerFd[0].events = POLLRDNORM;
  ServerFd[1].fd = 0; // stdin
  ServerFd[1].events = POLLIN;
  initPacket(&sendServ);
  initPacket(&recvServ);
  queueInit(&sendQueue);
  printf("Welcome to the dropbox-like server! : %s\n", username);
  while (!serverClosed) {
    int nready = poll(ServerFd, 2, -1);
    if (nready < 0) {
      if (errno == EINTR) continue;
      else {
        printf("poll error\n");
        exit(3);
      }
    }
    if (ServerFd[0].revents) {
      processServer();
    }
    if (ServerFd[1].revents) {
      readUser();
    }
  }
  closeConnection();
  if (serverClosed == 1) {
    printf("server closed\n");
  }
  if (serverClosed == 881) {
    printf("Bye!\n");
  }
  return 0;
}
