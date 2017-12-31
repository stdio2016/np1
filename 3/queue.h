#pragma once
#ifndef QUEUE_INCLUDED
#define QUEUE_INCLUDED

struct Queue {
  size_t start, size, capacity;
  void **data;
};

void queueInit(struct Queue *q);
void queueDestroy(struct Queue *q);
void queuePush(struct Queue *q, void *item);
void queuePop(struct Queue *q);
void *queueFirst(struct Queue *q);

#endif
