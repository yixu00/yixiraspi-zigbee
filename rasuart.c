#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT "/dev/ttyUSB0"
#define DEFAULT_BAUDRATE 9600
#define DEFAULT_IDLE_GAP 0.12
#define DEFAULT_READ_TIMEOUT 0.05
#define BUFFER_CAPACITY 1024

static volatile sig_atomic_t g_stop = 0;

typedef struct {
    const char *port;
    int baudrate;
    double idle_gap;
    double read_timeout;
} options_t;

static void handle_sigint(int signum) {
    (void)signum;
    g_stop = 1;
}

static void print_usage(const char *program) {
    printf("Usage: %s [port] [--baudrate N] [--idle-gap SEC] [--read-timeout SEC]\n", program);
    printf("Receive and print ZigBee coordinator UART output over USB-TTL.\n");
}

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

static bool parse_args(int argc, char **argv, options_t *options) {
    int i;

    options->port = DEFAULT_PORT;
    options->baudrate = DEFAULT_BAUDRATE;
    options->idle_gap = DEFAULT_IDLE_GAP;
    options->read_timeout = DEFAULT_READ_TIMEOUT;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
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

static int open_serial_port(const options_t *options) {
    int fd;
    struct termios tty;
    speed_t speed = baudrate_to_constant(options->baudrate);

    if (speed == 0) {
        fprintf(stderr, "Unsupported baudrate: %d\n", options->baudrate);
        return -1;
    }

    fd = open(options->port, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "Serial error: %s\n", strerror(errno));
        return -1;
    }

    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "Serial error: %s\n", strerror(errno));
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
        fprintf(stderr, "Serial error: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void print_frame(const unsigned char *buffer, size_t length) {
    char timestamp[32];
    char text[BUFFER_CAPACITY + 1];
    size_t i;
    size_t out = 0;
    time_t now = time(NULL);
    struct tm tm_now;

    for (i = 0; i < length && out < BUFFER_CAPACITY; ++i) {
        unsigned char byte = buffer[i];
        if (byte == '\0' || byte == '\r' || byte == '\n') {
            continue;
        }
        text[out++] = (char)byte;
    }

    if (out == 0) {
        return;
    }

    text[out] = '\0';
    localtime_r(&now, &tm_now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
    printf("[%s] %s\n", timestamp, text);
    fflush(stdout);
}

int main(int argc, char **argv) {
    options_t options;
    int fd;
    unsigned char buffer[BUFFER_CAPACITY];
    size_t buffered = 0;
    double last_data_at = 0.0;
    struct sigaction sa;

    if (!parse_args(argc, argv, &options)) {
        return (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) ? 0 : 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    fd = open_serial_port(&options);
    if (fd < 0) {
        return 1;
    }

    printf("Listening on %s @ %d bps\n", options.port, options.baudrate);
    fflush(stdout);

    while (!g_stop) {
        fd_set readfds;
        struct timeval timeout;
        int ready;
        unsigned char chunk[256];
        ssize_t count;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        timeout.tv_sec = (time_t)options.read_timeout;
        timeout.tv_usec = (suseconds_t)((options.read_timeout - timeout.tv_sec) * 1000000.0);

        ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "Serial error: %s\n", strerror(errno));
            close(fd);
            return 1;
        }

        if (ready > 0 && FD_ISSET(fd, &readfds)) {
            count = read(fd, chunk, sizeof(chunk));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "Serial error: %s\n", strerror(errno));
                close(fd);
                return 1;
            }
            if (count > 0) {
                size_t to_copy = (size_t)count;
                if (to_copy > BUFFER_CAPACITY - buffered) {
                    to_copy = BUFFER_CAPACITY - buffered;
                }
                if (to_copy > 0) {
                    memcpy(buffer + buffered, chunk, to_copy);
                    buffered += to_copy;
                    last_data_at = monotonic_seconds();
                }
                continue;
            }
        }

        if (buffered > 0 && (monotonic_seconds() - last_data_at) >= options.idle_gap) {
            print_frame(buffer, buffered);
            buffered = 0;
        }
    }

    if (buffered > 0) {
        print_frame(buffer, buffered);
    }

    printf("\nStopped.\n");
    close(fd);
    return 0;
}
