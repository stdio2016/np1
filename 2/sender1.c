// use alarm() timeout
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
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
int currentSN;
FILE *fileToSend;

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
    fprintf( stderr, "unable to connect to server\n" );
    exit(1);
  }
}

int tryToGetAck(char type) {
  isTimeout = 0;
  while (isTimeout == 0) {
    unsigned char msg[MAX_PACK_SIZE];
    int n = recv(sockfd, msg, MAX_PACK_SIZE, 0);
    if (n < 0) {
      if (errno == ECONNREFUSED) {
        QQ("Connection refused");
      }
      return -1; // failed
    }
    if (msg[0] == type) {
      if (getu32(&msg[4]) == currentSN) return 0;
    }
  }
  return -1;
}

void send_file(char *filename) {
  fileToSend = fopen(filename, "rb");
  if (fileToSend == NULL) {
    fprintf(stderr, "Cannot open file %s\n", filename);
    exit(EXIT_FAILURE);
  }
  unsigned char msg[MAX_PACK_SIZE];
  msg[0] = SILLY_INIT;
  currentSN = rand();
  setu32(currentSN, &msg[4]);
  strcpy(&msg[12], filename);
  int n = strlen(filename);
  int i;
  for (i = 0; i < 10; i++) {
    alarm(1);
    printf("try to connect %d\n", i+1);
    send(sockfd, msg, n + 13, 0);
    int y = tryToGetAck(SILLY_INIT_ACK);
    if (y == 0) {
      break;
    }
  }
  if (i == 10) QQ("Connection timeout");
  while (!feof(fileToSend)) {
    msg[0] = SILLY_SEND;
    setu32(currentSN, &msg[4]);
    n = fread(&msg[12], 1, MAX_PACK_SIZE - 12, fileToSend);
    printf("sending part %d\n", currentSN);
    for (i = 0; i < 40; i++) {
      ualarm(waitTime[i], 0);
      send(sockfd, msg, n + 12, 0);
      currentSN += n;
      int y = tryToGetAck(SILLY_ACK);
      if (y == 0) {
        break;
      }
      currentSN -= n;
    }
    if (i == 40) QQ("Connection timeout");
  }
  fclose(fileToSend);
  printf("file ends\n");
  for (i = 0; i < 40; i++) {
    msg[0] = SILLY_STOP;
    setu32(currentSN, &msg[4]);
    ualarm(waitTime[i], 0);
    send(sockfd, msg, 12, 0);
    int y = tryToGetAck(SILLY_STOP_ACK);
    if (y == 0) {
      break;
    }
  }
  if (i == 40) QQ("Connection timeout");
  {
    msg[0] = SILLY_STOP_REALLY;
    setu32(currentSN, &msg[4]);
    ualarm(waitTime[i], 0);
    send(sockfd, msg, 12, 0);
  }
}

int main(int argc, char *argv[])
{
  if (argc < 4) {
    printf("Usage: %s <receiver IP> <receiver port> <file name>\n", argv[0]);
    return 1;
  }
  buildConnection(argv[1], argv[2]);
  srand(time(NULL));
  signal(SIGALRM, sig_alarm);
  siginterrupt(SIGALRM, 1);
  send_file(argv[3]);
  return 0;
}
