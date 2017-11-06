#include <ctype.h>
#include <errno.h>
#include <limits.h> // for OPEN_MAX
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h> // select()
#include <sys/socket.h> // shutdown(), socket(), AF_INET, SOCK_STREAM, SHUT_WR, accept(), setsockopt()
#include <sys/types.h> // portable setsockopt()
#include <netinet/in.h> // struct sockaddr_in, htons()
#include <arpa/inet.h> // htons()
#include <unistd.h> // write()
#include <poll.h> // poll(), struct pollfd

#define SERVER_HEAD "\x1B[33m[Server]\x1B[0m "
#define ERROR_HEAD  SERVER_HEAD "\x1B[91mERROR\x1B[0m: "

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

struct char_buffer_t {
  int size;
  int capacity;
  int finished;
  char *buf;
};

// store client info
struct client_info {
  char name[16]; // name must be 2~12 English letter
  struct char_buffer_t recv; // buffer for client message
  struct char_buffer_t send; // buffer for server -> client
} *Clients;
struct pollfd *ClientFd;
// all connected clients
int maxi = 1;

void OutOfMemory(void) {
  fprintf(stderr, "Out of memory!\n");
  exit(2);
}

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

void initBuffer(struct char_buffer_t *buf) {
  buf->size = 0;
  buf->capacity = 10;
  buf->buf = malloc(sizeof(char) * buf->capacity);
  if (buf->buf == NULL) OutOfMemory();
}

void initClient(int clientId) {
  strcpy(Clients[clientId].name, "anonymous");
  initBuffer(&Clients[clientId].recv);
  initBuffer(&Clients[clientId].send);
}

void destroyClient(int clientId) {
  ClientFd[clientId].fd = -1;
  free(Clients[clientId].recv.buf);
  free(Clients[clientId].send.buf);
}

// 0 means connection closed
// -1 means error
int recvline(int socketId, struct char_buffer_t *buf) {
  int i, firstrecv = 1;
  if (buf->size > 0 && buf->buf[buf->size-1] == '\n') {
    // already read a line
    for (i = buf->size; i < buf->finished; i++) {
      buf->buf[i - buf->size] = buf->buf[i];
    }
    buf->finished -= buf->size;
    // try to find '\n'
    for (i = 0; i < buf->finished; i++) {
      if (buf->buf[i] == '\n') break;
    }
    if (i < buf->finished) {
      buf->size = i+1;
      return i+1;
    }
    buf->size = buf->finished;
  }
  // receive a '\n'
  while (1) {
    int n = recv(socketId, buf->buf + buf->finished, buf->capacity - buf->finished, MSG_DONTWAIT);
    if (n > 0) {
      buf->finished += n;
      for (i = buf->size; i < buf->finished; i++) {
        if (buf->buf[i] == '\n') break;
      }
      if (i < buf->finished) {
        buf->size = i+1;
        return i+1; // found a newline
      }
      buf->size = buf->finished;
      if (buf->finished >= buf->capacity) {
        char *newbuf = realloc(buf->buf, buf->capacity * 2);
        if (newbuf == NULL) OutOfMemory();
        buf->buf = newbuf;
        buf->capacity *= 2;
      }
      else {
        return buf->size; // not finished receiving
      }
    }
    else if (n == 0) {
      if (firstrecv) return 0; // connection closed
      return buf->size;
    }
    else { // error
      return -1;
    }
    firstrecv = 0;
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

void sendToClient(int clientId, const char *msg) {
  send(ClientFd[clientId].fd, msg, strlen(msg), MSG_DONTWAIT);
}

void errorCommand(int clientId) {
  sendToClient(clientId, ERROR_HEAD "Error command.\n");
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
  if (no != NULL) errorCommand(clientId);
  else {
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
      sendToClient(clientId, ERROR_HEAD "\x1B[92m");
      sendToClient(clientId, arg1);
      sendToClient(clientId, "\x1B[0m has been used by others.\n");
      return ;
    }
    char oldname[16];
    strcpy(oldname, Clients[clientId].name);
    strcpy(Clients[clientId].name, arg1);
    sendToClient(clientId, SERVER_HEAD "You're now known as \x1B[92m");
    sendToClient(clientId, arg1);
    sendToClient(clientId, "\x1B[0m.\n");
    for (i = 2; i <= maxi; i++) {
      if (i == clientId) continue;
      sendToClient(i, SERVER_HEAD "\x1B[92m");
      sendToClient(i, oldname);
      sendToClient(i, "\x1B[0m is now known as \x1B[92m");
      sendToClient(i, arg1);
      sendToClient(i, "\x1B[0m.\n");
    }
  }
}

void processMessage(int clientId, char *msg) {
  char *rem = NULL;
  char *cmd = getArgFromString(msg, &rem), *no;
  if (strcmp(cmd, "who") == 0) {
    no = getArgFromString(rem, &rem);
    if (no != NULL) errorCommand(clientId);
    else {
      
    }
  }
  else if (strcmp(cmd, "name") == 0) {
    nameCommand(clientId, rem);
  }
  else if (strcmp(cmd, "tell") == 0) {
    printf("tell\n");
  }
  else if (strcmp(cmd, "yell") == 0) {
    printf("yell\n");
  }
}

void processClient(int clientId, int socketId) {
  // socketId is socket of client #clientId
  int still = 1;
  while (still) {
    still = 0;
    int n = recvline(socketId, &Clients[clientId].recv);
    if (n < 0) {
      if (errno == ECONNRESET) {
        printf("client %d aborted connection\n", clientId);
        close(socketId);
        destroyClient(clientId);
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ; // malicious client!
      }
      else {
        printf("readline error\n");
      }
    }
    else if (n == 0) {
      printf("client %d closed connection\n", clientId);
      close(socketId);
      destroyClient(clientId);
    }
    else { // check if the line is finished
      if (Clients[clientId].recv.buf[n-1] == '\n') {
        Clients[clientId].recv.buf[n-1] = '\0';
        printf("client %d says %s\n", clientId, Clients[clientId].recv.buf);
        processMessage(clientId, Clients[clientId].recv.buf);
        Clients[clientId].recv.buf[n-1] = '\n';
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
          close(f);
        }
        else {
          printf("Its client id is %d\n", i);
          ClientFd[i].events = POLLRDNORM;
          initClient(i);
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
