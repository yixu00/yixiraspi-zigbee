#include "frame_accumulator.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void frame_accumulator_init(frame_accumulator_t *accumulator) {
    accumulator->length = 0;
    accumulator->last_data_at = 0.0;
}

void frame_accumulator_append(frame_accumulator_t *accumulator, const unsigned char *data, size_t length, double now) {
    size_t free_space;
    size_t to_copy;

    if (length == 0) {
        return;
    }

    free_space = FRAME_BUFFER_CAPACITY - accumulator->length;
    to_copy = length < free_space ? length : free_space;
    if (to_copy > 0) {
        memcpy(accumulator->data + accumulator->length, data, to_copy);
        accumulator->length += to_copy;
        accumulator->last_data_at = now;
    }
}

bool frame_accumulator_should_flush(const frame_accumulator_t *accumulator, double now, double idle_gap) {
    if (accumulator->length == 0) {
        return false;
    }

    return (now - accumulator->last_data_at) >= idle_gap;
}

bool frame_accumulator_take_text(frame_accumulator_t *accumulator, char *out_text, size_t out_size) {
    size_t i;
    size_t out = 0;

    if (out_size == 0) {
        accumulator->length = 0;
        return false;
    }

    for (i = 0; i < accumulator->length && out + 1 < out_size; ++i) {
        unsigned char byte = accumulator->data[i];
        if (byte == '\0' || byte == '\r' || byte == '\n') {
            continue;
        }
        if (!isprint(byte) && byte != '\t' && byte != ' ') {
            continue;
        }
        out_text[out++] = (char)byte;
    }

    accumulator->length = 0;
    out_text[out] = '\0';
    return out > 0;
}

bool frame_extract_last_number(const char *text, double *out_value) {
    const char *cursor = text;
    char *end = NULL;
    bool found = false;
    double last_value = 0.0;

    while (*cursor != '\0') {
        double parsed = strtod(cursor, &end);
        if (end != cursor) {
            if (isfinite(parsed)) {
                last_value = parsed;
                found = true;
            }
            cursor = end;
            continue;
        }
        cursor++;
    }

    if (found) {
        *out_value = last_value;
    }

    return found;
}
