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
#define MAXLINE 1000
#define STDIN_FD 0

// connection status
enum connectionStatusEnum {
  NOT_READY, READY, CLOSED, TIMEOUT, USER_EXIT, ERROR
};
volatile sig_atomic_t status;

int sockfd; // socket connected to server
char buf[MAXLINE]; // user input buffer

int maxint(int a, int b) {
  return a>b ? a : b;
}

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

void sig_pipe(int signo) {
  status = CLOSED;
}

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

int main(int argc, char *argv[])
{
  signal(SIGPIPE, sig_pipe);
  status = NOT_READY;
  if (argc != 4) {
    fprintf( stderr, "usage: %s <ip> <port> <username>\n", argv[0]);
    return 1;
  }
  buildConnection(argv[1], argv[2]);
  
  closeConnection();
  return 0;
}
