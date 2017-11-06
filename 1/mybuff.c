#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h> // send(), recv()
#include "mybuff.h"

void OutOfMemory(void) {
  fprintf(stderr, "Out of memory!\n");
  exit(2);
}

void initBuffer(struct char_buffer_t *buf) {
  buf->start = 0;
  buf->end = 0;
  buf->finished = 0;
  buf->capacity = 10;
  buf->buf = malloc(sizeof(char) * buf->capacity);
  if (buf->buf == NULL) OutOfMemory();
}

// return bytes received
// remember to check newline
// 0 means connection closed
// -1 means error
// received line is buf->buf[buf->start ~ buf->end-1]
int recvline(int socketId, struct char_buffer_t *buf) {
  int i, firstrecv = 1;
  if (buf->end > 0 && buf->buf[buf->end-1] == '\n') {
    // already read a line
    buf->start = buf->end;
    // try to find next '\n'
    for (i = buf->start; i < buf->finished; i++) {
      if (buf->buf[i] == '\n') break;
    }
    if (i < buf->finished) { // there is '\n'
      buf->end = i+1;
      return buf->end - buf->start;
    }
    // no '\n' => move data
    for (i = buf->start; i < buf->finished; i++) {
      buf->buf[i - buf->start] = buf->buf[i];
    }
    buf->end = buf->finished - buf->start;
    buf->start = 0;
    buf->finished = buf->end;
  }
  // receive a '\n'
  while (1) {
    int n = recv(socketId, &buf->buf[buf->finished], buf->capacity - buf->finished, MSG_DONTWAIT);
    if (n > 0) {
      buf->finished += n;
      for (i = buf->end; i < buf->finished; i++) {
        if (buf->buf[i] == '\n') break;
      }
      if (i < buf->finished) {
        buf->end = i+1;
        return i+1; // found a newline
      }
      buf->end = buf->finished;
      if (buf->finished >= buf->capacity) {
        char *newbuf = realloc(buf->buf, buf->capacity * 2);
        if (newbuf == NULL) OutOfMemory();
        buf->buf = newbuf;
        buf->capacity *= 2;
      }
      else {
        return buf->finished; // not finished receiving
      }
    }
    else if (n == 0) {
      if (firstrecv) return 0; // connection closed
      return buf->finished;
    }
    else { // error
      return -1;
    }
    firstrecv = 0;
  }
}

