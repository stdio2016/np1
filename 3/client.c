#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h> // poll()
#include <sys/socket.h> // connect(), shutdown(), socket(), AF_INET, SOCK_STREAM, SHUT_WR
#include <netinet/in.h> // struct sockaddr_in, htons()
#include <arpa/inet.h> // inet_pton()
#include <netdb.h> // getaddrinfo(), gai_strerror
#include <fcntl.h> // fcntl()
#include "mypack.h"
#define MAXLINE 1000
#define STDIN_FD 0
#define PUT  ('P'<<8|'U')

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

void readServer() {

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
      readServer();
    }
  }
  if (!serverClosed && (ServerFd[0].revents & POLLWRNORM)) {
    trySendToServerAgain();
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
      setPacketHeader(&sendServ, PUT, n);
      memcpy(sendServ.buf+4, arg, n);
      sendToServer();
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
