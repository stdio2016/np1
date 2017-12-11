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

enum ConnectionState {
  START, WAIT, FINISH
} state;

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

struct SentPacket {
  struct SentPacket *prev, *next;
  int size;
  unsigned int sn;
  int msg;
  int resent;
  struct timeval time;
};

int sockfd;
unsigned int initSN;
unsigned int currentSN; // data sent
unsigned int ackedSN; // data acked
unsigned int nackedBytes;
unsigned int windowSize = 536;
FILE *fileToSend;
unsigned int recvPackSize;
int dupAck = 0;
unsigned char sendbuf[MAX_PACK_SIZE], recvbuf[MAX_PACK_SIZE];
int rcvrVersion = 1; // 1: stop and wait, 2: selective ACK

unsigned int timeout = 100000; // time in microseconds

unsigned char filebuf[FILE_BUF_SIZE];
unsigned int checkbuf[FILE_BUF_SIZE] = {0};
struct SentPacket sendpack[MAX_SEND_PACKET];
struct SentPacket unacked = {&unacked, &unacked}, *resentPtr = &unacked;
int packetId = 1;

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

void buildConnection(char *ipStr, char *portStr) {
  union good_sockaddr servaddr;
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    fprintf( stderr, "unable to create socket\n" );
    exit(1);
  }
  memset(&servaddr, 0, sizeof (servaddr));
  servaddr.in.sin_family = AF_INET;
  int port;
  if (sscanf(portStr, "%d", &port) != 1) {
    fprintf( stderr, "cannot read port number\n" );
    exit(1);
  }
  servaddr.in.sin_port = htons(port);
  if (inet_pton(AF_INET, ipStr, &servaddr.in.sin_addr.s_addr) == 0) {
    fprintf( stderr, "Invalid IP format\n" );
    exit(1);
  }
  else if (connect(sockfd, &servaddr.sa, sizeof(servaddr)) < 0) {
    fprintf( stderr, "unable to connect to receiver\n" );
    exit(1);
  }
}

void sendPacket(int msgType, unsigned int sn, unsigned int datasize) {
  sendbuf[0] = msgType;
  if (rcvrVersion == 1 && msgType == SILLY_SEND) sendbuf[0] = 0;
  sendbuf[1] = 3;
  sendbuf[2] = 255;
  sendbuf[3] = 255;
  setu32(sn, &sendbuf[4]);
  setu32(0, &sendbuf[8]);
  unsigned int start = sn & FILE_BUF_WRAP;
  if (msgType == SILLY_SEND) {
    if (start + datasize >= FILE_BUF_SIZE) {
      memcpy(&sendbuf[12], &filebuf[start], FILE_BUF_SIZE - start);
      memcpy(&sendbuf[12 + FILE_BUF_SIZE - start], filebuf, start + datasize - FILE_BUF_SIZE);
    }
    else {
      memcpy(&sendbuf[12], &filebuf[start], datasize);
    }
  }
  send(sockfd, sendbuf, datasize + 12, 0);
}

void sendNewFilePart(unsigned int size) {
  unsigned int start = currentSN & FILE_BUF_WRAP;
  unsigned int n;
  if (start + size >= FILE_BUF_SIZE) {
    n = fread(filebuf + start, 1, FILE_BUF_SIZE - start, fileToSend);
    memset(checkbuf + start, 0, sizeof(int) * n);
    if (n == FILE_BUF_SIZE - start) {
      int n2 = fread(filebuf, 1, size - n, fileToSend);
      n += n2;
      memset(checkbuf, 0, sizeof(int) * n2);
    }
  }
  else {
    n = fread(filebuf + start, 1, size, fileToSend);
    memset(checkbuf + start, 0, sizeof(int) * n);
  }

  struct SentPacket *N = &sendpack[packetId];
  unacked.prev->next = N;
  N->prev = unacked.prev;
  N->next = &unacked;
  unacked.prev = N;
  N->size = n;
  N->sn = currentSN + n;
  N->msg = n == 0 ? SILLY_STOP : SILLY_SEND;
  gettimeofday(&N->time, NULL);
  N->resent = 0;

  //printf("send new part %u size %d\n", N->sn - initSN, n);
  sendPacket(N->msg, currentSN, N->size);
  checkbuf[currentSN & FILE_BUF_WRAP] = packetId;
  nackedBytes += n;
  currentSN += n;

  packetId++;
  if (packetId >= MAX_SEND_PACKET) {
    packetId = 1;
  }
}

void sendOldFilePart() {
  if (resentPtr == &unacked) resentPtr = unacked.next;
  if (resentPtr == &unacked) return ;
  //printf("resend part %u size %d\n", resentPtr->sn - resentPtr->size - initSN, resentPtr->size);
  resentPtr->resent = 1;
  sendPacket(resentPtr->msg, resentPtr->sn - resentPtr->size, resentPtr->size);
  resentPtr = resentPtr->next;
}

struct SentPacket *dsuFind(struct SentPacket *from) {
  if (from->msg != 0 || from == &unacked) return from;
  return from->next = dsuFind(from->next);
}

void dsuUnion(struct SentPacket *a, struct SentPacket *b) {
  a->next = dsuFind(b);
}

int recvAck(unsigned from, unsigned to) {
  unsigned f = from & FILE_BUF_WRAP;
  if (checkbuf[f] == 0) return 0; // this ack is not supported
  struct SentPacket *ptr = dsuFind(&sendpack[checkbuf[f]]);
  while (ptr != &unacked && to - ptr->sn < FILE_BUF_SIZE) {
    if (ptr->sn - ptr->size == ackedSN) ackedSN = ptr->sn;
    ptr->msg = 0;
    nackedBytes -= ptr->size;
    ptr->next->prev = ptr->prev;
    ptr->prev->next = ptr->next;
    if (ptr == resentPtr) resentPtr = ptr->next;
    if (ptr->next == &unacked) break;
    unsigned f = (ptr->next->sn - ptr->next->size) & FILE_BUF_WRAP;
    dsuUnion(ptr, &sendpack[checkbuf[f]]);
    ptr = ptr->next;
  }
  return 1;
}

int recvMaybeAck() {
  if (recvPackSize < 12) {
    return 0;
  }
  unsigned char msg = recvbuf[0];
  unsigned int offset = recvbuf[1];
  if (offset < 3) offset = 3;
  if (!(msg & SILLY_ACK)) return 0; // not an ack
  unsigned recvSN = getu32(&recvbuf[4]);
  if (recvSN == ackedSN) dupAck++;
  else dupAck = 1;
  if (recvSN - ackedSN > currentSN - ackedSN) { // out of range
    return 0;
  }
  if (msg == SILLY_STOP_ACK && recvSN == ackedSN) {
    return SILLY_STOP_ACK;
  }

  int c = recvAck(ackedSN, recvSN);
  //printf("receiver acked %d dup %d\n", ackedSN - initSN, dupAck);
  if (!c) {
    return 0;
  }
  // selective ack
  int i;
  for (i = offset * 4; i <= recvPackSize - 8; i += 8) {
    unsigned a = getu32(&recvbuf[i]);
    if (a - ackedSN > currentSN - ackedSN) { // out of range
      continue;
    }
    unsigned b = getu32(&recvbuf[i+4]);
    if (b - ackedSN > currentSN - ackedSN) { // out of range
      continue;
    }
    recvAck(a, b);
  }
  return SILLY_ACK;
}

int waitInitAck() {
  isTimeout = 0;
  while (isTimeout == 0) {
    int n = recv(sockfd, recvbuf, MAX_PACK_SIZE, 0);
    if (n < 0) {
      if (errno == ECONNREFUSED) {
        QQ("Connection refused");
      }
      return -1; // failed
    }
    recvPackSize = n;
    if (recvbuf[0] == SILLY_INIT_ACK) {
      if (getu32(&recvbuf[4]) == currentSN) return 0;
    }
  }
  return -1;
}

void initsendfile(char *name) {
  int i;
  initSN = currentSN = rand();
  char *safe = safename(name);
  strcpy(sendbuf + 12, safe);
  int n = strlen(safe) + 1;
  for (i = 0; i < 10; i++) {
    alarm(1);
    printf("try to connect %d\n", i+1);
    sendPacket(SILLY_INIT, currentSN, n);
    int y = waitInitAck(SILLY_INIT_ACK);
    if (y == 0) {
      break;
    }
  }
  if (i == 10) QQ("Connection timeout QQ\n");
  if (recvPackSize == 16) {
    // check receiver version
    if (getu32(&recvbuf[12]) == 0x20169487) {
      rcvrVersion = 2;
      windowSize = 65535;
    }
  }
  ackedSN = currentSN;
  nackedBytes = 0;
}

void sendfile(char *name) {
  initsendfile(name);
  printf("connected\n");
  do {
    if (!feof(fileToSend) && currentSN - ackedSN < FILE_BUF_SIZE - 1 && nackedBytes < windowSize - 1) {
      int can = MAX_PACK_SIZE - 12;
      can = min(can, windowSize - nackedBytes);
      can = min(can, FILE_BUF_SIZE - (currentSN - ackedSN));
      sendNewFilePart(can);
    }
    else {
      ualarm(timeout, 0);
      isTimeout = 0;
      recvPackSize = recv(sockfd, recvbuf, MAX_PACK_SIZE, 0);
      if (recvPackSize == -1) {
        if (errno == EINTR) {
          resentPtr = unacked.next;
          sendOldFilePart();
        }
        else if (errno == ECONNREFUSED) {
          QQ("connection refused");
        }
      }
      else {
        ualarm(0,0);
        int r = recvMaybeAck();
        if (r) {
          if (dupAck >= 3) {
            resentPtr = unacked.next;
          }
          else {
            printf("\x1B[Fsent %d\n", ackedSN - initSN);
          }
          sendOldFilePart();
        }
      }
    }
  } while (!feof(fileToSend) || currentSN != ackedSN);
  int i;
  for (i = 0; i < 10; i++) {
    sendPacket(SILLY_STOP, currentSN, 0);
    ualarm(timeout, 0);
    isTimeout = 0;
    recvPackSize = recv(sockfd, recvbuf, MAX_PACK_SIZE, 0);
    if (recvPackSize == -1) {
      if (errno == EINTR) {
        ;
      }
      else if (errno == ECONNREFUSED) {
        QQ("connection refused");
      }
      else {
        printf("Unknown error %d\n", errno);
        QQ("I give up.");
      }
    }
  }
}

int main(int argc, char *argv[])
{
  if (argc < 4) {
    printf("Usage: %s <receiver IP> <receiver port> <file name>\n", argv[0]);
    return 1;
  }
  fileToSend = fopen(argv[3], "rb");
  if (fileToSend == NULL) {
    fprintf( stderr, "cannot open file %s\n", argv[3] );
    exit(1);
  }
  buildConnection(argv[1], argv[2]);
  srand(time(NULL));
  signal(SIGALRM, sig_alarm);
  siginterrupt(SIGALRM, 1);

  struct timeval start, end;
  gettimeofday(&start, NULL);
  sendfile(argv[3]);
  gettimeofday(&end, NULL);
  printf("It took %f seconds\n", (end.tv_sec - start.tv_sec) + 1e-6 * (end.tv_usec - start.tv_usec));
  return 0;
}
