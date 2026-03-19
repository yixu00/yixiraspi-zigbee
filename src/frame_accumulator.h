#ifndef FRAME_ACCUMULATOR_H
#define FRAME_ACCUMULATOR_H

#include <stdbool.h>
#include <stddef.h>

#define FRAME_BUFFER_CAPACITY 1024

typedef struct {
    unsigned char data[FRAME_BUFFER_CAPACITY];
    size_t length;
    double last_data_at;
} frame_accumulator_t;

void frame_accumulator_init(frame_accumulator_t *accumulator);
void frame_accumulator_append(frame_accumulator_t *accumulator, const unsigned char *data, size_t length, double now);
bool frame_accumulator_should_flush(const frame_accumulator_t *accumulator, double now, double idle_gap);
bool frame_accumulator_take_text(frame_accumulator_t *accumulator, char *out_text, size_t out_size);
bool frame_extract_last_number(const char *text, double *out_value);

#endif
