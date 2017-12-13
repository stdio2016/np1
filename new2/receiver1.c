// use alarm() timeout
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#define MAX_PACK_SIZE 548
#define SILLY_ACK 1
#define SILLY_INIT 2
#define SILLY_INIT_ACK 3
#define SILLY_SEND 8
#define SILLY_STOP 4
#define SILLY_STOP_ACK 5

#define MAX_WINDOW_SIZE  65535
#define MAX_SEND_PACKET  2048
#define FILE_BUF_SIZE  (MAX_WINDOW_SIZE+1)
#define FILE_BUF_WRAP  (FILE_BUF_SIZE-1)

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

int sockfd;
union good_sockaddr cliaddr;
unsigned int initSN;
unsigned int currentSN; // data sent
unsigned int ackedSN; // data acked
unsigned int recvSN; // received SN
unsigned int windowSize = 65535;
FILE *fileToRecv;
unsigned int recvPackSize, recvDataSize;
unsigned char sendbuf[MAX_PACK_SIZE], recvbuf[MAX_PACK_SIZE];
int senderVersion = 1; // 1: stop and wait, 2: selective ACK

unsigned int timeout = 100000; // time in microseconds

unsigned char filebuf[FILE_BUF_SIZE];
int checkbuf[FILE_BUF_SIZE * 2] = {0};

volatile sig_atomic_t isTimeout;
void sig_alarm(int a) {
  isTimeout = 1;
}

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

int min(int a, int b) {
  return a > b ? b : a;
}

// is test inside [a,b)?
int insideRange(unsigned test, unsigned a, unsigned b) {
  return test-a < b-a && b-test <= b-a;
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

void sendPacket(int msgType, unsigned int sn, unsigned int datasize) {
  sendbuf[0] = msgType;
  sendbuf[1] = 3;
  sendbuf[2] = 255;
  sendbuf[3] = 255;
  setu32(sn, &sendbuf[4]);
  setu32(0, &sendbuf[8]);
  socklen_t len = sizeof cliaddr;
  sendto(sockfd, sendbuf, datasize + 12, 0, &cliaddr.sa, len);
}

int sacked;

void storeFile() {
  if (!insideRange(recvSN, ackedSN, ackedSN+windowSize)
   || !insideRange(recvSN + recvDataSize, ackedSN, ackedSN+windowSize)) {
    return ;
  }
  if (!insideRange(recvSN + recvDataSize, ackedSN, currentSN)) {
    currentSN = recvSN + recvDataSize;
  }
  // store into buffer
  int i;
  for (i = 0; i < recvDataSize; i++) {
    checkbuf[(recvSN+i) & FILE_BUF_WRAP] = 1;
  }

  unsigned j = recvSN & FILE_BUF_WRAP;
  unsigned k = (recvSN + recvDataSize) & FILE_BUF_WRAP;
  char *dat = recvbuf + recvPackSize - recvDataSize;
  if (j <= k) {
    memcpy(&filebuf[j], dat, recvDataSize);
  }
  else {
    int slice = FILE_BUF_SIZE-j;
    memcpy(&filebuf[j], dat, slice);
    memcpy(filebuf, &dat[slice], recvDataSize-slice);
  }

  // write to file
  i = 0;
  j = ackedSN & FILE_BUF_WRAP;
  while (checkbuf[ackedSN & FILE_BUF_WRAP] == 1) {
    checkbuf[ackedSN & FILE_BUF_WRAP] = 0;
    ackedSN++; i++;
  }
  k = ackedSN & FILE_BUF_WRAP;
  if (j > k) {
    int slice = FILE_BUF_SIZE-j;
    fwrite(&filebuf[j], 1, slice, fileToRecv);
    fwrite(filebuf, 1, i - slice, fileToRecv);
  }
  else {
    fwrite(&filebuf[j], 1, i, fileToRecv);
  }
}

int mayRecvFile() {
  union good_sockaddr a;
  socklen_t len = sizeof a;
  recvPackSize = recvfrom(sockfd, recvbuf, MAX_PACK_SIZE, 0, &a.sa, &len);
  if (recvPackSize == -1 || isTimeout) {
    return -1;
  }
  if (recvPackSize < 12) {
    return 0;
  }
  if (a.in.sin_addr.s_addr != cliaddr.in.sin_addr.s_addr) { // different source
    return 0;
  }
  if (a.in.sin_port != cliaddr.in.sin_port) { // different port
    return 0;
  }

  unsigned char msg = recvbuf[0];
  unsigned int offset = recvbuf[1];
  if (offset < 3 || senderVersion == 1) offset = 3;
  recvDataSize = recvPackSize - offset * 4;
  if (recvDataSize < 0) return 0;
  recvSN = getu32(&recvbuf[4]);
  // check if in range [current-window, acked+window)
  if (!insideRange(recvSN, currentSN-windowSize, ackedSN+windowSize)
   || !insideRange(recvSN + recvDataSize, currentSN-windowSize, ackedSN+windowSize)) {
    return 0;
  }

  if (msg == SILLY_STOP) {
    return SILLY_STOP;
  }
  if (msg == SILLY_SEND || senderVersion == 1 && msg == 0)
    return SILLY_SEND;
  if (msg == SILLY_INIT)
    return SILLY_INIT;
  return 0;
}

void recv_file(char *name) {
  printf("receiving file %s\n", name);
  int y = 0;
  int death = 0;
  isTimeout = 0;
  ualarm(timeout, 0);
  memset(checkbuf, 0, sizeof checkbuf);
  while (y != SILLY_STOP || currentSN != ackedSN) {
    y = mayRecvFile();
    if (y == SILLY_SEND) {
      storeFile();
      printf("received %u ack %u cur %u\n", recvSN - initSN, ackedSN - initSN, currentSN - initSN);
      sendPacket(SILLY_ACK, ackedSN, sacked * 8);
    }
    else if (y == SILLY_INIT) {
      setu32(0x20169487, &sendbuf[12]);
      sendPacket(SILLY_INIT_ACK, ackedSN, senderVersion == 3 ? 4 : 0);
    }
    else if (y < 0) {
      death++;
      sendPacket(SILLY_ACK, ackedSN, 0);
      if (death == 30) {
        printf("sender timeout QQ\n");
        return;
      }
    }
    if (y != 0) {
      isTimeout = 0;
      ualarm(timeout, 0);
    }
    if (y > 0) death = 0;
  }
  if (y == SILLY_STOP) {
    int i;
    for (i = 0; i < 30; i++) {
      errno = 0;
      sendPacket(SILLY_STOP_ACK, ackedSN, 0);
      ualarm(timeout, 0);
      y = mayRecvFile();
      if (recvbuf[0] == 6 || recvbuf[0] == SILLY_STOP_ACK)
        break;
    }
  }
}

int main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("Usage: %s <port>\n", argv[0]);
    return 1;
  }
  initReceiver(argv[1]);
  signal(SIGALRM, sig_alarm);
  siginterrupt(SIGALRM, 1);

  int n;
  socklen_t len;
  for (;;) {
    len = sizeof(cliaddr);
    n = recvfrom(sockfd, recvbuf, MAX_PACK_SIZE, 0, &cliaddr.sa, &len);
    if (n < 0) {
      ;
    }
    else {
      struct timeval start, end;
      gettimeofday(&start, NULL);
      if (n < 12) continue; // format error
      if (recvbuf[0] != SILLY_INIT) continue;
      initSN = currentSN = getu32(&recvbuf[4]);
      ackedSN = initSN;
      recvbuf[0] = SILLY_INIT_ACK;
      if (n >= 16 && getu32(&recvbuf[n - 4]) == 0x20169487) {
        setu32(0x20169487, &sendbuf[12]);
        n = 4;
        senderVersion = 2;
      }
      else {
        n = 0;
        senderVersion = 1;
      }
      printf("sender version is %d\n", senderVersion);
      sendPacket(SILLY_INIT_ACK, currentSN, n);
      char *name = safename(&recvbuf[12]);
      fileToRecv = fopen(name, "wb");
      if (fileToRecv == NULL) {
        printf("cannot write to %s", name);
      }
      else {
        recv_file(name);
        fclose(fileToRecv);
      }
      gettimeofday(&end, NULL);
      printf("It took %f seconds\n", (end.tv_sec - start.tv_sec) + 1e-6 * (end.tv_usec - start.tv_usec));
    }
  }
  return 0;
}
