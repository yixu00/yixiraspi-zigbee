#ifndef UART_READER_H
#define UART_READER_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

#include "frame_queue.h"

#define DEFAULT_PORT "/dev/ttyUSB0"
#define DEFAULT_BAUDRATE 9600
#define DEFAULT_IDLE_GAP 0.12
#define DEFAULT_READ_TIMEOUT 0.05
#define DEFAULT_FBDEV "/dev/fb0"
#define DEFAULT_ONENET_PRODUCT_ID "H9y2Eg83QV"
#define DEFAULT_ONENET_HOST "mqtts.heclouds.com"
#define DEFAULT_ONENET_PORT 1883
#define DEFAULT_ONENET_DEVICE_NAME "yixizigbee"
#define DEFAULT_ONENET_ACCESS_KEY "y++jD7G3Z7PLa7W1+2drtfeX2X+jgYtTH8tt65Vdhzs="

typedef struct {
    const char *port;
    int baudrate;
    double idle_gap;
    double read_timeout;
    const char *onenet_product_id;
    const char *onenet_device_name;
    const char *onenet_access_key;
    const char *onenet_host;
    int onenet_port;
} uart_options_t;

typedef struct {
    uart_options_t options;
    frame_queue_t *queue;
    volatile sig_atomic_t *stop_flag;
    pthread_t thread;
    pthread_mutex_t status_mutex;
    unsigned long frame_count;
    bool running;
    bool error;
    char last_error[128];
} uart_reader_t;

void uart_options_init(uart_options_t *options);
bool uart_parse_args(int argc, char **argv, uart_options_t *options);
void uart_print_usage(const char *program);
int uart_reader_init(uart_reader_t *reader, const uart_options_t *options, frame_queue_t *queue, volatile sig_atomic_t *stop_flag);
int uart_reader_start(uart_reader_t *reader);
void uart_reader_join(uart_reader_t *reader);
void uart_reader_get_status(uart_reader_t *reader, unsigned long *frame_count, bool *running, bool *error, char *error_text, size_t error_text_size);

#endif
