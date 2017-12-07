// use select() timeout
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define MAX_PACK_SIZE 548
#define SILLY_ACK 1
#define SILLY_INIT 2
#define SILLY_INIT_ACK 3
#define SILLY_SEND 0
#define SILLY_STOP 4
#define SILLY_STOP_ACK 5
#define SILLY_STOP_REALLY 6

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

int waitTime[40] = {
  1000,   1000,   1000,   1000,   2000,
  2000,   2000,   2000,   4000,   4000,
  4000,   4000,   8000,   8000,   8000,
  8000,  16000,  16000,  16000,  16000,
 31000,  31000,  31000,  31000,  63000,
 63000,  63000,  63000, 125000, 125000,
125000, 125000, 250000, 250000, 250000,
250000, 500000, 500000, 500000, 500000
};

int sockfd;
union good_sockaddr cliaddr;
int currentSN;

void QQ(const char *x) {
  fprintf( stderr, "%s\n", x);
  exit(EXIT_FAILURE);
}

unsigned int getu32(const unsigned char *a) {
  return a[0] | a[1]<<8 | a[2]<<16 | a[3]<<24;
}

void setu32(unsigned int n, char *b) {
  b[0] = n & 0xff;
  b[1] = n>>8 & 0xff;
  b[2] = n>>16 & 0xff;
  b[3] = n>>24 & 0xff;
}

char *safename(char *filename) {
  int y = 0, slash = 0;
  while (filename[y]) {
    if (filename[y] == '/') {
      slash = y+1;
    }
    y++;
  }
  return &filename[slash];
}

fd_set myFdSet;
struct timeval tv;
int readIsPossible(int lv) {
  FD_ZERO(&myFdSet);
  FD_SET(sockfd, &myFdSet);
  int n = sockfd+1;
  tv.tv_sec = 0;
  tv.tv_usec = waitTime[lv];
  return select(n, &myFdSet, NULL, NULL, &tv) == 1;
}

int writeIsPossible(int lv) {
  FD_ZERO(&myFdSet);
  FD_SET(sockfd, &myFdSet);
  int n = sockfd+1;
  tv.tv_sec = 0;
  tv.tv_usec = waitTime[lv];
  return select(n, NULL, &myFdSet, NULL, &tv) == 1;
}

void initReceiver(char *portStr) {
  union good_sockaddr servaddr;
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    fprintf( stderr, "unable to create socket\n" );
    exit(2);
  }

  memset(&servaddr, 0, sizeof (servaddr));
  servaddr.in.sin_family = AF_INET;
  int port;
  if (sscanf(portStr, "%d", &port) != 1) {
    fprintf( stderr, "cannot read port number\n" );
    exit(1);
  }
  servaddr.in.sin_port = htons(port);
  servaddr.in.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(sockfd, &servaddr.sa, sizeof(servaddr));
  if (ret != 0) {
    if (errno == EADDRINUSE) {
      fprintf( stderr, "port %d already in use\n", port );
    }
    else {
      fprintf( stderr, "bind error\n" );
    }
    exit(2);
  }
}

unsigned char msg[MAX_PACK_SIZE];

int tryToGetFile(int lv) {
  while (1) {
    if (!readIsPossible(lv)) return -1;
    int n = recv(sockfd, msg, MAX_PACK_SIZE, 0);
    if (n < 0) {
      if (errno == ECONNREFUSED) {
        QQ("Connection refused");
      }
      return -1; // failed
    }
    if (msg[0] == SILLY_SEND || msg[0] == SILLY_STOP) {
      if (getu32(&msg[4]) == currentSN) {
        currentSN += n-12;
        return n-12;
      }
    }
  }
  return -1;
}

int tryToStop(int lv) {
  while (1) {
    if (!readIsPossible(lv)) return -1;
    int n = recv(sockfd, msg, MAX_PACK_SIZE, 0);
    if (n < 0) {
      if (errno == ECONNREFUSED) {
        QQ("Connection refused");
      }
      return -1; // failed
    }
    if (msg[0] == SILLY_STOP_REALLY && getu32(&msg[4]) == currentSN) return 0;
  }
  return -1;
}

void sendAck(int waitLv, int type) {
  if (!writeIsPossible(waitLv)) return ;
  int len = sizeof(cliaddr.sa);
  char buf[12];
  setu32(currentSN, &buf[4]);
  buf[0] = type;
  sendto(sockfd, buf, 12, 0, &cliaddr.sa, len);
}

void recv_file(char *filename) {
  printf("receiving file %s\n", filename);
  FILE *F = fopen(safename(filename), "wb");
  int r, i;
  while (msg[0] != SILLY_STOP) {
    for (i = 10; i < 40; i++) {
      r = tryToGetFile(i);
      if (r >= 0) break;
      sendAck(i, SILLY_ACK);
    }
    if (i == 40) {
      printf("connection timeout QQ\n");
      return ;
    }
    if (msg[0] == SILLY_STOP) {
      printf("file end!\n");
    }
    else {
      fwrite(&msg[12], 1, r, F);
      sendAck(i, SILLY_ACK);
    }
  }
  for (i = 10; i < 32; i++) {
    sendAck(i, SILLY_STOP_ACK);
    r = tryToStop(i);
    if (r == 0) break;
  }
  fclose(F);
}

int main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("Usage: %s <port>\n", argv[0]);
    return 1;
  }
  initReceiver(argv[1]);

  int n;
  socklen_t len;
  for (;;) {
    len = sizeof(cliaddr);
    n = recvfrom(sockfd, msg, MAX_PACK_SIZE, 0, &cliaddr.sa, &len);
    if (n < 0) {
      ;
    }
    else {
      if (n < 12) continue; // format error
      if (msg[0] != SILLY_INIT) continue;
      currentSN = getu32(&msg[4]);
      msg[0] = SILLY_INIT_ACK;
      n = sendto(sockfd, msg, 12, 0, &cliaddr.sa, len);
      recv_file(&msg[12]);
    }
  }
  return 0;
}
