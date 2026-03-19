#include "frame_queue.h"

#include <string.h>

int frame_queue_init(frame_queue_t *queue) {
    memset(queue, 0, sizeof(*queue));
    return pthread_mutex_init(&queue->mutex, NULL);
}

void frame_queue_destroy(frame_queue_t *queue) {
    pthread_mutex_destroy(&queue->mutex);
}

void frame_queue_push(frame_queue_t *queue, const frame_message_t *message) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->count == FRAME_QUEUE_CAPACITY) {
        queue->head = (queue->head + 1U) % FRAME_QUEUE_CAPACITY;
        queue->count--;
    }

    queue->items[queue->tail] = *message;
    queue->tail = (queue->tail + 1U) % FRAME_QUEUE_CAPACITY;
    queue->count++;

    pthread_mutex_unlock(&queue->mutex);
}

bool frame_queue_pop(frame_queue_t *queue, frame_message_t *message) {
    bool has_item = false;

    pthread_mutex_lock(&queue->mutex);

    if (queue->count > 0U) {
        *message = queue->items[queue->head];
        queue->head = (queue->head + 1U) % FRAME_QUEUE_CAPACITY;
        queue->count--;
        has_item = true;
    }

    pthread_mutex_unlock(&queue->mutex);
    return has_item;
}
