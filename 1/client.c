#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h> // select()
#include <sys/socket.h> // connect(), shutdown(), socket(), AF_INET, SOCK_STREAM, SHUT_WR
#include <netinet/in.h> // struct sockaddr_in, htons()
#include <arpa/inet.h> // inet_pton()
#include <unistd.h> // write()
#include <netdb.h> // getaddrinfo(), gai_strerror
#define MAXLINE 100
#define STDIN_FD 0

// connection status
enum connectionStatusEnum {
  NOT_READY, READY, CLOSED, TIMEOUT, USER_EXIT, ERROR
};
volatile sig_atomic_t status;

int sockfd; // socket connected to server
char *buf; // user input buffer
int buf_size;

char *servbuf;
int servbuf_size;
int servbuf_pos;

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
  }
  else if (connect(sockfd, &servaddr.sa, sizeof(servaddr)) < 0) {
    fprintf( stderr, "unable to connect to server\n" );
    exit(1);
  }

  struct timeval timeout;      
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;

  if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    fprintf( stderr, "failed to set timeout\n");
  }
  if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    fprintf( stderr, "failed to set timeout\n");
  }
}

void closeConnection(void) {
  shutdown(sockfd, SHUT_RDWR);
}

int readUserInput(FILE *f) {
    int ch = fgetc(f);
    size_t i = 0;
    while (ch != EOF && ch != '\n') {
        if (i == buf_size - 1) {
            char *newbuf = (char *)realloc(buf, buf_size * 2);
            if (newbuf == NULL) return -1;
            buf = newbuf;
            buf_size = buf_size * 2;
        }
        buf[i++] = ch;
        ch = fgetc(f);
    }
    if (i >= buf_size - 3) {
      char *newbuf = (char *)realloc(buf, buf_size * 2);
      if (newbuf == NULL) return -1;
      buf = newbuf;
      buf_size = buf_size * 2;
    }
    buf[i] = '\n';
    buf[i+1] = '\0';
    return (i == 0 && ch == EOF) ? 0 : 1;
}

void sendToServer(void) {
  int n = strlen(buf);
  char *p = buf;
  while (n > 0) {
    int r = write(sockfd, buf, strlen(buf));
    if (r == 0) {
      status = CLOSED;
      return ;
    }
    if (r < 0) {
      if (errno == EINTR) {
        r = 0;
      }
      else {
        // real error
        if (errno == EWOULDBLOCK) {
          status = TIMEOUT;
        }
        else if (status != CLOSED) {
          printf("error %d\n", errno);
          status = ERROR;
        }
        return ;
      }
    }
    n -= r;
    p += r;
  }
}

void getServerResponse(void) {
  int flag = 1;
  while (flag) {
    int n;
    char ch;
    n = recv(sockfd, &ch, 1, MSG_DONTWAIT);
    if (n == 0 || errno == ECONNRESET) {
      status = CLOSED;
      return ;
    }
    if (n == 1) {
      if (servbuf_pos + 1 >= servbuf_size) {
        char *newbuf = realloc(servbuf, servbuf_size * 2);
        if (newbuf == NULL) {
          printf("Out of memory\n");
          exit(2);
        }
        servbuf = newbuf;
        servbuf_size *= 2;
      }
      servbuf[servbuf_pos++] = ch;
      flag = ch != '\n';
    }
    else {
      if (errno == EINTR) {
        ;
      }
      else {
        // real error
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
          return ;
        }
        else if (status != CLOSED) {
          printf("error %d\n", errno);
          status = ERROR;
        }
        return ;
      }
    }
  }
}

void writeServerResponse(void) {
  getServerResponse();
  if (servbuf_pos > 0 && servbuf[servbuf_pos - 1] == '\n') {
    servbuf[servbuf_pos] = '\0';
    printf("%s", servbuf);
    servbuf_pos = 0;
  }
}

int main(int argc, char *argv[])
{
  signal(SIGPIPE, sig_pipe);
  status = NOT_READY;
  if (argc != 3) {
    fprintf( stderr, "usage: ./client [ip] [port]\n" );
    return 1;
  }
  buildConnection(argv[1], argv[2]);
  buf_size = 10;
  buf = malloc(buf_size * sizeof(char));
  servbuf_pos = 0;
  servbuf_size = 10;
  servbuf = malloc(servbuf_size * sizeof(char));
  status = READY;
  while (status == READY) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds); // stdin
    FD_SET(sockfd, &readfds);
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    int maxfd = maxint(sockfd + 1, STDIN_FD);
    select(maxfd, &readfds, NULL, NULL, &timeout);
    if (FD_ISSET(STDIN_FD, &readfds)) {
      int has = readUserInput(stdin);
      if (has == 1) {
        char *cmd = buf;
        while (*cmd == ' ') cmd++;
        if (cmd[0]=='e' && cmd[1]=='x' && cmd[2]=='i' && cmd[3]=='t'
          && (cmd[4]==' ' || cmd[4]=='\n')) {
          status = USER_EXIT;
        }
        else {
          sendToServer();
        }
      }
    }
    else if (FD_ISSET(sockfd, &readfds)) {
      writeServerResponse();
    }
  }
  switch (status) {
    case CLOSED:
      fputs("Server closed\n", stdout);
      break;
    case TIMEOUT:
      fputs("Server timeout\n", stdout);
      exit(3);
    case USER_EXIT:
      break;
    case ERROR:
      fputs("Socket error\n", stdout);
      exit(3);
    case NOT_READY:
    case READY:
    default:
      fputs("This shouldn't happen!\n", stdout);
      exit(-1);
      break;
  }
  closeConnection();
  return 0;
}
