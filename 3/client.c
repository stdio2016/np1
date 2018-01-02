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
#include "connection.h"

int isServer = 0;
#define MAXLINE 1000
#define STDIN_FD 0

int sockfd; // socket connected to server
char buf[MAXLINE]; // user input buffer

int maxint(int a, int b) {
  return a>b ? a : b;
}

char myname[256];

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
      Clients[0].closed = 881; // so I can quit
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
      queuePush(&Clients[0].sendQueue, qi);
      ClientFd[0].events |= POLLWRNORM;
    }
    else if (strcmp(d, "/sleep") == 0) {
      if (arg == NULL || strcmp(arg, "") == 0) {
        printf("Missing time\n");
        return ;
      }
      int len = 0, i;
      sscanf(arg, "%d", &len);
      printf("Client starts to sleep\n");
      for (i = 0; i < len; i++) {
        struct timespec req = {1, 0};
        printf("Sleep %d\n", i+1);
        nanosleep(&req, NULL);
      }
      puts("Client wakes up\n");
    }
    //else if (strcmp(d, "/cancel") == 0) {
    //  if (Clients[0].sendQueue.size == 0) {
    //    printf("No upload tasks to cancel\n");
    //    return ;
    //  }
    //  queuePop(&Clients[0].sendQueue);
    //  Clients[0].isSending = SendState_CANCEL;
    //  ClientFd[0].events |= POLLWRNORM;
    //}
  }
}

void sendNameToServer(char *nm) {
  size_t n = strlen(nm);
  if (n > 255) {
    printf("Username too long!\n");
    exit(-1);
  }
  strcpy(myname, nm);
  strcpy(Clients[0].send.buf+4, nm);
  setPacketHeader(&Clients[0].send, NAME, n);
  sendToClient(0);
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
  Clients = malloc(sizeof(Clients[0]));
  ClientFd = malloc(sizeof(ClientFd[0]) * 2);
  ClientFd[0].fd = sockfd;
  ClientFd[0].events = POLLRDNORM;
  ClientFd[1].fd = 0; // stdin
  ClientFd[1].events = POLLIN;
  union good_sockaddr unused;
  initClient(0, unused);
  sendNameToServer(argv[3]);
  printf("Welcome to the dropbox-like server! : %s\n", username);
  while (!Clients[0].closed) {
    int nready = poll(ClientFd, 2, -1);
    if (nready < 0) {
      if (errno == EINTR) continue;
      else {
        printf("poll error\n");
        exit(3);
      }
    }
    if (ClientFd[0].revents) {
      processClient(0, sockfd);
    }
    if (ClientFd[1].revents) {
      readUser();
    }
  }
  closeConnection();
  if (Clients[0].closed == 1) {
    printf("server closed\n");
  }
  if (Clients[0].closed == 881) {
    printf("Bye!\n");
  }
  return 0;
}
