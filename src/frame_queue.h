#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include <stdbool.h>
#include <pthread.h>

#include "frame_accumulator.h"

#define FRAME_QUEUE_CAPACITY 32

typedef struct {
    char text[FRAME_BUFFER_CAPACITY + 1];
    double numeric_value;
    bool has_numeric_value;
} frame_message_t;

typedef struct {
    frame_message_t items[FRAME_QUEUE_CAPACITY];
    unsigned int head;
    unsigned int tail;
    unsigned int count;
    pthread_mutex_t mutex;
} frame_queue_t;

int frame_queue_init(frame_queue_t *queue);
void frame_queue_destroy(frame_queue_t *queue);
void frame_queue_push(frame_queue_t *queue, const frame_message_t *message);
bool frame_queue_pop(frame_queue_t *queue, frame_message_t *message);

#endif
