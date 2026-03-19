#include "uart_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "frame_accumulator.h"

static bool parse_int(const char *text, int *value) {
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || end == NULL || *end != '\0') {
        return false;
    }
    if (parsed <= 0 || parsed > 4000000L) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_double(const char *text, double *value) {
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (text[0] == '\0' || end == NULL || *end != '\0') {
        return false;
    }
    if (parsed <= 0.0) {
        return false;
    }
    *value = parsed;
    return true;
}

static speed_t baudrate_to_constant(int baudrate) {
    switch (baudrate) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        default:
            return 0;
    }
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static int open_serial_port(const uart_options_t *options) {
    int fd;
    struct termios tty;
    speed_t speed = baudrate_to_constant(options->baudrate);

    if (speed == 0) {
        errno = EINVAL;
        return -1;
    }

    fd = open(options->port, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void set_status(uart_reader_t *reader, bool running, bool error, const char *message) {
    pthread_mutex_lock(&reader->status_mutex);
    reader->running = running;
    reader->error = error;
    if (message != NULL) {
        snprintf(reader->last_error, sizeof(reader->last_error), "%s", message);
    } else {
        reader->last_error[0] = '\0';
    }
    pthread_mutex_unlock(&reader->status_mutex);
}

static void publish_frame(uart_reader_t *reader, frame_accumulator_t *accumulator) {
    frame_message_t message;
    double numeric_value;

    if (!frame_accumulator_take_text(accumulator, message.text, sizeof(message.text))) {
        return;
    }

    message.has_numeric_value = frame_extract_last_number(message.text, &numeric_value);
    message.numeric_value = numeric_value;
    frame_queue_push(reader->queue, &message);

    pthread_mutex_lock(&reader->status_mutex);
    reader->frame_count++;
    pthread_mutex_unlock(&reader->status_mutex);
}

static void *uart_reader_thread(void *arg) {
    uart_reader_t *reader = (uart_reader_t *)arg;
    frame_accumulator_t accumulator;
    int fd;

    frame_accumulator_init(&accumulator);
    fd = open_serial_port(&reader->options);
    if (fd < 0) {
        char message[128];
        snprintf(message, sizeof(message), "Serial error: %s", strerror(errno));
        set_status(reader, false, true, message);
        return NULL;
    }

    set_status(reader, true, false, NULL);

    while (!(*reader->stop_flag)) {
        fd_set readfds;
        struct timeval timeout;
        int ready;
        unsigned char chunk[256];
        ssize_t count;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        timeout.tv_sec = (time_t)reader->options.read_timeout;
        timeout.tv_usec = (suseconds_t)((reader->options.read_timeout - (double)timeout.tv_sec) * 1000000.0);

        ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            {
                char message[128];
                snprintf(message, sizeof(message), "Serial error: %s", strerror(errno));
                set_status(reader, false, true, message);
            }
            close(fd);
            return NULL;
        }

        if (ready > 0 && FD_ISSET(fd, &readfds)) {
            count = read(fd, chunk, sizeof(chunk));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                {
                    char message[128];
                    snprintf(message, sizeof(message), "Serial error: %s", strerror(errno));
                    set_status(reader, false, true, message);
                }
                close(fd);
                return NULL;
            }
            if (count > 0) {
                frame_accumulator_append(&accumulator, chunk, (size_t)count, monotonic_seconds());
                continue;
            }
        }

        if (frame_accumulator_should_flush(&accumulator, monotonic_seconds(), reader->options.idle_gap)) {
            publish_frame(reader, &accumulator);
        }
    }

    if (accumulator.length > 0) {
        publish_frame(reader, &accumulator);
    }

    close(fd);
    set_status(reader, false, false, NULL);
    return NULL;
}

void uart_options_init(uart_options_t *options) {
    options->port = DEFAULT_PORT;
    options->baudrate = DEFAULT_BAUDRATE;
    options->idle_gap = DEFAULT_IDLE_GAP;
    options->read_timeout = DEFAULT_READ_TIMEOUT;
}

void uart_print_usage(const char *program) {
    printf("Usage: %s [port] [--baudrate N] [--idle-gap SEC] [--read-timeout SEC]\n", program);
    printf("Receive ZigBee UART data and visualize it with LVGL.\n");
}

bool uart_parse_args(int argc, char **argv, uart_options_t *options) {
    int i;

    uart_options_init(options);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            uart_print_usage(argv[0]);
            return false;
        }
        if (strcmp(argv[i], "--baudrate") == 0) {
            if (i + 1 >= argc || !parse_int(argv[++i], &options->baudrate)) {
                fprintf(stderr, "Invalid --baudrate value\n");
                return false;
            }
            continue;
        }
        if (strcmp(argv[i], "--idle-gap") == 0) {
            if (i + 1 >= argc || !parse_double(argv[++i], &options->idle_gap)) {
                fprintf(stderr, "Invalid --idle-gap value\n");
                return false;
            }
            continue;
        }
        if (strcmp(argv[i], "--read-timeout") == 0) {
            if (i + 1 >= argc || !parse_double(argv[++i], &options->read_timeout)) {
                fprintf(stderr, "Invalid --read-timeout value\n");
                return false;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
        options->port = argv[i];
    }

    return true;
}

int uart_reader_init(uart_reader_t *reader, const uart_options_t *options, frame_queue_t *queue, volatile sig_atomic_t *stop_flag) {
    memset(reader, 0, sizeof(*reader));
    reader->options = *options;
    reader->queue = queue;
    reader->stop_flag = stop_flag;
    return pthread_mutex_init(&reader->status_mutex, NULL);
}

int uart_reader_start(uart_reader_t *reader) {
    return pthread_create(&reader->thread, NULL, uart_reader_thread, reader);
}

void uart_reader_join(uart_reader_t *reader) {
    pthread_join(reader->thread, NULL);
    pthread_mutex_destroy(&reader->status_mutex);
}

void uart_reader_get_status(uart_reader_t *reader, unsigned long *frame_count, bool *running, bool *error, char *error_text, size_t error_text_size) {
    pthread_mutex_lock(&reader->status_mutex);

    if (frame_count != NULL) {
        *frame_count = reader->frame_count;
    }
    if (running != NULL) {
        *running = reader->running;
    }
    if (error != NULL) {
        *error = reader->error;
    }
    if (error_text != NULL && error_text_size > 0) {
        snprintf(error_text, error_text_size, "%s", reader->last_error);
    }

    pthread_mutex_unlock(&reader->status_mutex);
}
