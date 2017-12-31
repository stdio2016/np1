#include <stdlib.h>
#include <string.h>
#include "queue.h"

void queueInit(struct Queue *q) {
  q->start = 0;
  q->size = 0;
  q->capacity = 5;
  q->data = malloc(sizeof(void*) * q->capacity);
}

void queueDestroy(struct Queue *q) {
  free(q->data);
}

void queuePush(struct Queue *q, void *item) {
  if (q->size >= q->capacity) {
    void **n = malloc(sizeof(void*) * q->capacity * 2);
    if (n == NULL) return;
    q->data = n;
    memcpy(n + q->capacity+q->start, n + q->start, (q->capacity - q->start) * sizeof(void *));
    q->start += q->capacity;
    q->capacity *= 2;
  }
  size_t end = q->start + q->size;
  if (end >= q->capacity) {
    q->data[end - q->capacity] = item;
  }
  else {
    q->data[end] = item;
  }
  q->size++;
}

void queuePop(struct Queue *q) {
  q->start++;
  q->size--;
  if (q->start >= q->capacity) {
    q->start -= q->capacity;
  }
}

void *queueFirst(struct Queue *q) {
  return q->data[q->start];
}
