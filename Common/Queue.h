//
// Created by shangqi on 29/2/20.
//

#ifndef MEASUREMENT_QUEUE_H
#define MEASUREMENT_QUEUE_H

#include "Message.h"
#include "Spinlock.h"

#ifdef __cplusplus
extern "C" {
#endif

struct __attribute__((aligned(64))) queue_s {
    Message 	*top;
    Message 	*bottom;
    int nMsg;
    char lock;				///< HLE lock
};

typedef struct queue_s Queue;

#define ENOUGH_BAR 20

void queue_init(Queue *q);
int how_long(Queue *q);
int is_empty_queue(Queue *q);
int is_enough_queue(Queue *q);
void push_back(Queue *q, Message *new_msg);
void push_front(Queue *q, Message *new_msg);
Message *pop_back(Queue *q);
Message *pop_front(Queue *q);

#ifdef __cplusplus
}
#endif

#endif //MEASUREMENT_QUEUE_H
