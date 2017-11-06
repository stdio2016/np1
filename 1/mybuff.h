#ifndef MYBUFF_INCLUDED
#define MYBUFF_INCLUDED
struct char_buffer_t {
  int start;
  int end;
  int capacity;
  int finished;
  char *buf;
};

void OutOfMemory(void);

void initBuffer(struct char_buffer_t *buf);

// return bytes received
// remember to check newline
// 0 means connection closed
// -1 means error
// received line is buf->buf[buf->start ~ buf->end-1]
int recvline(int socketId, struct char_buffer_t *buf);
#endif
