#include <ctype.h>
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
#include "mybuff.h"
#define SERVER_HEAD "\x1B[33m[Server]\x1B[0m "
#define ERROR_HEAD  SERVER_HEAD "\x1B[91;1mERROR\x1B[0m: "
#define SUCCESS_HEAD  SERVER_HEAD "\x1B[92;1mSUCCESS\x1B[0m: "

// define backlog
#define LISTENQ 5
// define 10 secs of timeout
#define SOME_TIME 10000
// define maximum ine length
#define MAXLINE 1000
int sockfd; // socket of this server
int open_max; // maximum file descriptor id

union good_sockaddr {
  struct sockaddr sa;
  struct sockaddr_in in;
};

// store client info
struct client_info {
  char name[16]; // name must be 2~12 English letter
  union good_sockaddr addr; // store client's IP and port
  struct char_buffer_t recv; // buffer for client message
  struct char_buffer_t send; // buffer for server -> client
} *Clients;
struct pollfd *ClientFd;
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
}

void initClient(int clientId, union good_sockaddr addr) {
  strcpy(Clients[clientId].name, "anonymous");
  Clients[clientId].addr = addr;
  initBuffer(&Clients[clientId].recv);
  initBuffer(&Clients[clientId].send);
}

void destroyClient(int clientId) {
  ClientFd[clientId].fd = -1;
  free(Clients[clientId].recv.buf);
  free(Clients[clientId].send.buf);
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

void sendToClient(int clientId, const char *msg) {
  send(ClientFd[clientId].fd, msg, strlen(msg), MSG_DONTWAIT);
}

void sendNameToClient(int clientId, const char *name) {
  sendToClient(clientId, "\x1B[92m");
  sendToClient(clientId, name);
  sendToClient(clientId, "\x1B[0m");
}

void sendMessageToClient(int clientId, const char *name) {
  sendToClient(clientId, "\x1B[93m");
  sendToClient(clientId, name);
  sendToClient(clientId, "\x1B[0m");
}

void errorCommand(int clientId) {
  sendToClient(clientId, ERROR_HEAD "Error command.\n");
}
 
void whoCommand(int clientId, char *rem) {
  char *no = getArgFromString(rem, &rem);
  {
    int i;
    for (i = 2; i <= maxi; i++) {
      if (ClientFd[i].fd < 0) continue; // not used
      sendToClient(clientId, SERVER_HEAD);
      sendNameToClient(clientId, Clients[i].name);
      sendToClient(clientId, " ");
      // show IP
      char ipstr[100];
      inet_ntop(AF_INET, &Clients[i].addr.in.sin_addr, ipstr, sizeof (ipstr));
      sendToClient(clientId, ipstr);
      // show port
      int port = ntohs(Clients[i].addr.in.sin_port);
      snprintf(ipstr, 100, "/%d", port);
      sendToClient(clientId, ipstr);
      if (i == clientId) {
        sendToClient(clientId, " ->me");
      }
      sendToClient(clientId, "\n");
    }
  }
}

void nameCommand(int clientId, char *rem) {
  char Anonymous[] = ERROR_HEAD "Username cannot be anonymous.\n";
  char ErrorName[] = ERROR_HEAD "Username can only consists of 2~12 English letters.\n";
  char *arg1 = getArgFromString(rem, &rem), *no;
  if (arg1 == NULL) {
    sendToClient(clientId, Anonymous);
    return ;
  }
  no = getArgFromString(rem, &rem);
  {
    // check name type
    int len = strlen(arg1), i;
    for (i = 0; arg1[i]; i++) {
      if (!isalpha(arg1[i])) break;
    }
    if (len < 2 || len > 12 || i != len) {
      sendToClient(clientId, ErrorName);
      return ;
    }
    // check if the name is anonymous
    if (strcmp(arg1, "anonymous") == 0) {
      sendToClient(clientId, Anonymous);
      return ;
    }
    // check if the name is unique
    for (i = 2; i <= maxi; i++) {
      if (ClientFd[i].fd < 0) continue; // not used
      if (i == clientId) continue; // a user can be renamed to itself
      if (strcmp(Clients[i].name, arg1) == 0) {
        break; // same name
      }
    }
    if (i != maxi + 1) {
      sendToClient(clientId, ERROR_HEAD);
      sendNameToClient(clientId, arg1);
      sendToClient(clientId, " has been used by others.\n");
      return ;
    }
    char oldname[16];
    strcpy(oldname, Clients[clientId].name);
    strcpy(Clients[clientId].name, arg1);
    sendToClient(clientId, SERVER_HEAD "You're now known as ");
    sendNameToClient(clientId, arg1);
    sendToClient(clientId, ".\n");
    for (i = 2; i <= maxi; i++) {
      if (ClientFd[i].fd < 0) continue; // not used
      if (i == clientId) continue;
      sendToClient(i, SERVER_HEAD);
      sendNameToClient(i, oldname);
      sendToClient(i, " is now known as ");
      sendNameToClient(i, arg1);
      sendToClient(i, ".\n");
    }
  }
}

void tellCommand(int clientId, char *rem) {
  char Anonymous[] = ERROR_HEAD "You are anonymous.\n";
  char AnonymousRcvr[] = ERROR_HEAD "The client to which you sent is anonymous.\n";
  char NotExist[] = ERROR_HEAD "The receiver doesn't exist.\n";
  char EmptyMsg[] = ERROR_HEAD "Message is empty.\n";
  char NoRcvr[] = ERROR_HEAD "You didn't specify the receiver.\n";
  char Success[] = SUCCESS_HEAD "Your message has been sent.\n";
  if (strcmp(Clients[clientId].name, "anonymous") == 0) {
    sendToClient(clientId, Anonymous);
    return ;
  }
  char *username = getArgFromString(rem, &rem), *no, *message;
  if (username == NULL) {
    sendToClient(clientId, NoRcvr);
    return ;
  }
  message = rem;
  if (message == NULL || *message == '\0') {
    sendToClient(clientId, EmptyMsg);
    return ;
  }
  if (strcmp(username, "anonymous") == 0) {
    sendToClient(clientId, AnonymousRcvr);
    return ;
  }
  int i;
  for (i = 2; i <= maxi; i++) {
    if (ClientFd[i].fd < 0) continue;
    if (strcmp(Clients[i].name, username) == 0) break;
  }
  if (i <= maxi) {
    sendToClient(i, SERVER_HEAD);
    sendNameToClient(i, Clients[clientId].name);
    sendToClient(i, " tell you ");
    sendMessageToClient(i, message);
    sendToClient(i, "\n");
    sendToClient(clientId, Success);
  }
  else {
    sendToClient(clientId, NotExist);
  }
}

void yellCommand(int clientId, char *message) {
  char EmptyMsg[] = ERROR_HEAD "Message is empty.\n";
  if (message == NULL || *message == '\0') {
    sendToClient(clientId, EmptyMsg);
    return ;
  }
  int i;
  for (i = 2; i <= maxi; i++) {
    if (ClientFd[i].fd < 0) continue;
    sendToClient(i, SERVER_HEAD);
    sendNameToClient(i, Clients[clientId].name);
    sendToClient(i, " yell ");
    sendMessageToClient(i, message);
    sendToClient(i, "\n");
  }
}

void processMessage(int clientId, char *msg) {
  char *rem = NULL;
  char *cmd = getArgFromString(msg, &rem);
  if (cmd == NULL) return;
  if (strcmp(cmd, "who") == 0) {
    whoCommand(clientId, rem);
  }
  else if (strcmp(cmd, "name") == 0) {
    nameCommand(clientId, rem);
  }
  else if (strcmp(cmd, "tell") == 0) {
    tellCommand(clientId, rem);
  }
  else if (strcmp(cmd, "yell") == 0) {
    yellCommand(clientId, rem);
  }
  else {
    errorCommand(clientId);
  }
}

void hello(int clientId) {
  sendToClient(clientId, SERVER_HEAD "Hello, ");
  sendNameToClient(clientId, Clients[clientId].name);
  sendToClient(clientId, "! From: ");
  // show IP
  char ipstr[100];
  inet_ntop(AF_INET, &Clients[clientId].addr.in.sin_addr, ipstr, sizeof (ipstr));
  sendToClient(clientId, ipstr);
  // show port
  int port = ntohs(Clients[clientId].addr.in.sin_port);
  snprintf(ipstr, 100, "/%d\n", port);
  sendToClient(clientId, ipstr);
  int i;
  for (i = 2; i <= maxi; i++) {
    if (ClientFd[i].fd < 0) continue; // not used
    if (i == clientId) continue;
    sendToClient(i, SERVER_HEAD "Someone is coming!\n");
  }
}

void goodbye(int clientId) {
  int i;
  for (i = 2; i <= maxi; i++) {
    if (ClientFd[i].fd < 0) continue; // not used
    if (i == clientId) continue; // don't send message to him
    sendToClient(i, SERVER_HEAD);
    sendNameToClient(i, Clients[clientId].name);
    sendToClient(i, " is offline.\n");
  }
}

void processClient(int clientId, int socketId) {
  // socketId is socket of client #clientId
  int still = 1;
  while (still) {
    still = 0;
    int n = recvline(socketId, &Clients[clientId].recv);
    if (n < 0) {
      if (errno == ECONNRESET || errno == EPIPE) {
        printf("client %d aborted connection\n", clientId);
        close(socketId);
        goodbye(clientId);
        destroyClient(clientId);
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ; // malicious client!
      }
      else {
        printf("recvline error\n");
      }
    }
    else if (n == 0) {
      printf("client %d closed connection\n", clientId);
      close(socketId);
      goodbye(clientId);
      destroyClient(clientId);
    }
    else { // check if the line is finished
      char *buf = Clients[clientId].recv.buf + Clients[clientId].recv.start;
      if (buf[n-1] == '\n') {
        buf[n-1] = '\0';
        int rn = n >= 2 && buf[n-2] == '\r';
        if (rn) buf[n-2] = '\0';
        printf("client %d says %s\n", clientId, buf);
        processMessage(clientId, buf);
        if (rn) buf[n-2] = '\r';
        buf[n-1] = '\n';
        still = 1;
      }
    }
  }
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
  ClientFd[0].fd = STDIN_FILENO;
  ClientFd[0].events = POLLIN; // according to my experiment, using POLLRDNORM on stdin doesn't work
  ClientFd[1].fd = sockfd;
  ClientFd[1].events = POLLRDNORM;
  for (i = 2; i < open_max; i++) {
    ClientFd[i].fd = -1;
  }
  Clients = malloc(sizeof(struct client_info) * open_max);
  if (Clients == NULL) OutOfMemory();
  signal(SIGPIPE, SIG_IGN);
  while (88487) {
    int nready = poll(ClientFd, maxi+1, SOME_TIME);
    if (ClientFd[0].revents & POLLIN) { // input from stdin: manually send message to client
      char buf[100];
      int f = 0;
      scanf("%d,", &f);
      fgets(buf, 100, stdin);
      for (i = 0; buf[i] && buf[i] != '\n'; i++) {
        buf[i] ^= 0x20;
      }
      if (f <= maxi && f > 1 && ClientFd[f].fd > 0) {
        send(ClientFd[f].fd, buf, strlen(buf), MSG_DONTWAIT);
      }
    }
    if (ClientFd[1].revents & POLLRDNORM) { // new client connection
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
        for (i = 2; i < open_max; i++) {
          if (ClientFd[i].fd < 0) {
            ClientFd[i].fd = f;
            break;
          }
        }
        if (i == open_max) {
          printf("too many clients\n");
          char msg[] = SERVER_HEAD "Too many clients connected.\n";
          send(f, msg, strlen(msg), MSG_DONTWAIT);
          close(f);
        }
        else {
          printf("Its client id is %d\n", i);
          ClientFd[i].events = POLLRDNORM;
          initClient(i, clientInfo);
          hello(i);
          if (i > maxi)
            maxi = i;
          if (--nready <= 0)
            continue;
        }
      }
    }
    for (i = 2; i <= maxi; i++) {
      int f = ClientFd[i].fd;
      if (f < 0) continue; // unused socket slot
      if (ClientFd[i].revents & (POLLRDNORM | POLLERR)) {
        processClient(i, f);
        if (--nready <= 0)
          break;
      }
    }
  }
  free(ClientFd);
  free(Clients);
  return 0;
}
