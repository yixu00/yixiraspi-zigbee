#ifndef ONENET_UPLOADER_H
#define ONENET_UPLOADER_H

#include <stdbool.h>
#include <stddef.h>

#include "uart_reader.h"

typedef struct {
    const char *product_id;
    const char *device_name;
    const char *access_key;
    const char *host;
    int port;
    int socket_fd;
    bool enabled;
    bool connected;
    unsigned int next_message_id;
    char topic[128];
    char last_error[160];
} onenet_uploader_t;

void onenet_uploader_init(onenet_uploader_t *uploader, const uart_options_t *options);
void onenet_uploader_cleanup(onenet_uploader_t *uploader);
bool onenet_uploader_publish_frame(onenet_uploader_t *uploader, const char *frame_text);
const char *onenet_uploader_status(const onenet_uploader_t *uploader);
bool onenet_extract_measurements(const char *text, double *temperature, double *humidity);

#endif
