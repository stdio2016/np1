#include <errno.h>
#include <limits.h> // for OPEN_MAX
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h> // select()
#include <sys/socket.h> // shutdown(), socket(), AF_INET, SOCK_STREAM, SHUT_WR, accept(), setsockopt()
#include <sys/types.h> // portable setsockopt()
#include <netinet/in.h> // struct sockaddr_in, htons()
#include <arpa/inet.h> // htons()
#include <poll.h> // poll(), struct pollfd
#include <unistd.h> // STDIN_FILENO
#include <fcntl.h>
#include "connection.h"
int isServer = 1;

// define backlog
#define LISTENQ 5
// define 10 secs of timeout
#define SOME_TIME 10000
// define maximum ine length
#define MAXLINE 1000
// clientfd first entry
#define FIRST_CLIENTFD 1
int sockfd; // socket of this server
int open_max; // maximum file descriptor id

#define SERVER_HEAD "[Server] "
#define ERROR_HEAD  SERVER_HEAD "ERROR: "
#define SUCCESS_HEAD  SERVER_HEAD "SUCCESS: "

// all connected clients
int maxi = 1;

void initServer(char *portStr) {
  union good_sockaddr servaddr;
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    fprintf( stderr, "unable to create socket\n" );
    exit(2);
  }
  int yes[] = {1};
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, yes, sizeof yes) < 0) {
    fprintf( stderr, "failed to set reuse address\n");
    exit(2);
  };

  memset(&servaddr, 0, sizeof (servaddr));
  servaddr.in.sin_family = PF_INET;
  int port;
  if (sscanf(portStr, "%d", &port) != 1) {
    fprintf( stderr, "cannot read port number\n" );
    exit(1);
  }
  servaddr.in.sin_port = htons(port);
  servaddr.in.sin_addr.s_addr = INADDR_ANY;
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
  ret = listen(sockfd, LISTENQ);
  if (ret != 0) {
    fprintf( stderr, "listen error\n" );
    exit(2);
  }

  int flag=fcntl(sockfd,F_GETFL,0);
  fcntl(sockfd,F_SETFL,flag|O_NONBLOCK);
}

int main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf( stderr, "usage: ./server [port]\n" );
    return 1;
  }
  initServer(argv[1]);
  int i;
  open_max = FOPEN_MAX;
  ClientFd = malloc(sizeof(struct pollfd) * open_max);
  if (ClientFd == NULL) OutOfMemory();
  ClientFd[0].fd = sockfd;
  ClientFd[0].events = POLLRDNORM;
  for (i = FIRST_CLIENTFD; i < open_max; i++) {
    ClientFd[i].fd = -1;
  }
  Clients = malloc(sizeof(struct client_info) * open_max);
  if (Clients == NULL) OutOfMemory();
  initUserTable();
  signal(SIGPIPE, SIG_IGN);
  while (88487) { // an infinite loop
    int nready = poll(ClientFd, maxi+1, SOME_TIME);
    if (nready < 0) {
      if (errno == EINTR) continue;
      else {
        printf("poll error\n");
        exit(3);
      }
    }
    if (ClientFd[0].revents & POLLRDNORM) { // new client connection
      union good_sockaddr clientInfo;
      int addrlen = sizeof(clientInfo);
      int f = accept(sockfd, &clientInfo.sa, &addrlen);
      char ipstr[100];
      if (f < 0) {
        printf("connection error\n");
      }
      else {
        printf("connection from %s port %d. socket id = %d\n", 
          inet_ntop(AF_INET, &clientInfo.in.sin_addr, ipstr, sizeof (ipstr)),
          ntohs(clientInfo.in.sin_port), f);
        // find a place to store client descriptor
        for (i = FIRST_CLIENTFD; i < open_max; i++) {
          if (ClientFd[i].fd < 0) {
            ClientFd[i].fd = f;
            break;
          }
        }
        if (i == open_max) {
          printf("too many clients\n");
          close(f);
        }
        else {
          printf("Its client id is %d\n", i);
          ClientFd[i].events = POLLRDNORM;
          initClient(i, clientInfo);
          if (i > maxi)
            maxi = i;
          if (--nready <= 0)
            continue;
        }
      }
    }
    for (i = FIRST_CLIENTFD; i <= maxi; i++) {
      int f = ClientFd[i].fd;
      if (f < 0) continue; // unused socket slot
      if (ClientFd[i].revents & (POLLRDNORM | POLLERR | POLLWRNORM)) {
        processClient(i, f);
        if (--nready <= 0)
          break;
      }
    }
  }
  for (i = FIRST_CLIENTFD; i <= maxi; i++) {
    if (ClientFd[i].fd >= 0) {
      close(ClientFd[i].fd);
      destroyClient(i);
      ClientFd[i].fd = -1;
    }
  }
  free(ClientFd);
  free(Clients);
  return 0;
}
