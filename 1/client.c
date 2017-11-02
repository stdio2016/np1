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
#define MAXLINE 100
#define STDIN_FD 0

// connection status
enum connectionStatusEnum {
  NOT_READY, READY, CLOSED, TIMEOUT, USER_EXIT, ERROR
} status;

int sockfd; // socket connected to server
char *buf; // user input buffer
int buf_size;

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
    fprintf( stderr, "cannot read ip address\n" );
    exit(1);
  }
  if (connect(sockfd, &servaddr.sa, sizeof(servaddr)) < 0) {
    fprintf( stderr, "unable to connect to server\n" );
    exit(1);
  }

  struct timeval timeout;      
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;

  if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
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
    buf[i] = '\r';
    buf[i+1] = '\n';
    buf[i+2] = '\0';
    return (i == 0 && ch == EOF) ? 0 : 1;
}

void sendToServer(void) {
  int n = strlen(buf);
  char *p = buf;
  while (n > 0) {
    int r = write(sockfd, buf, strlen(buf));
    if (r <= 0) {
      if (errno == EINTR) {
        r = 0;
      }
      else {
        // real error
        if (errno == EWOULDBLOCK) {
          status = TIMEOUT;
        }
        else if (status != CLOSED) {
          status = ERROR;
        }
        return ;
      }
    }
    n -= r;
    p += r;
  }
}

void writeServerResponse(void) {
  int flag = 1;
  while (flag) {
    int n;
    char ch;
    n = read(sockfd, &ch, 1);
    if (n == 1) putchar(ch);
    else if (n == 0) {
      flag = 0;
    }
    else {
      if (errno == EINTR) {
        ;
      }
      else {
        // real error
        if (errno == EWOULDBLOCK) {
          status = TIMEOUT;
        }
        else if (status != CLOSED) {
          status = ERROR;
        }
        return ;
      }
    }
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
  status = READY;
  while (status == READY) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FD, &readfds); // stdin
    FD_SET(sockfd, &readfds);
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    int maxfd = maxint(sockfd + 1, STDIN_FD);
    select(maxfd, &readfds, NULL, NULL, &timeout);
    if (FD_ISSET(STDIN_FD, &readfds)) {
      int has = readUserInput(stdin);
      if (has == 1) sendToServer();
    }
    if (FD_ISSET(sockfd, &readfds)) {
      writeServerResponse();
    }
  }
  switch (status) {
    case CLOSED:
      fputs("Server closed\n", stdout);
      break;
    case TIMEOUT:
      fputs("Server timeout\n", stderr);
      exit(3);
    case USER_EXIT:
      break;
    case ERROR:
      fputs("Socket error\n", stderr);
      exit(3);
    case NOT_READY:
    case READY:
    default:
      fputs("This shouldn't happen!\n", stderr);
      exit(-1);
      break;
  }
  closeConnection();
  return 0;
}
