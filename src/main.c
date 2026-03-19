#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mousewheel.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"

#include "frame_queue.h"
#include "uart_reader.h"
#include "ui.h"

static volatile sig_atomic_t g_stop = 0;

static void handle_sigint(int signum) {
    (void)signum;
    g_stop = 1;
}

static void handle_display_delete(lv_event_t *event) {
    (void)event;
    g_stop = 1;
}

int main(int argc, char **argv) {
    struct sigaction sa;
    uart_options_t options;
    frame_queue_t queue;
    uart_reader_t reader;
    app_ui_t ui;
    lv_display_t *display;

    if (!uart_parse_args(argc, argv, &options)) {
        return (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) ? 0 : 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    if (frame_queue_init(&queue) != 0) {
        perror("frame_queue_init");
        return 1;
    }

    if (uart_reader_init(&reader, &options, &queue, &g_stop) != 0) {
        perror("uart_reader_init");
        frame_queue_destroy(&queue);
        return 1;
    }

    lv_init();
    fprintf(stderr, "Initializing LVGL...\n");
    display = lv_sdl_window_create(320, 240);
    if (display == NULL) {
        fprintf(stderr, "Failed to create LVGL SDL window\n");
        frame_queue_destroy(&queue);
        return 1;
    }

    lv_sdl_window_set_title(display, "ZigBee UART Data Visualizer");
    lv_display_add_event_cb(display, handle_display_delete, LV_EVENT_DELETE, NULL);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();

    app_ui_init(&ui, &queue, &reader, &options, "SDL window");
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);
    fprintf(stderr, "UI initialized in desktop window mode.\n");

    if (uart_reader_start(&reader) != 0) {
        perror("uart_reader_start");
        frame_queue_destroy(&queue);
        return 1;
    }

    fprintf(stderr, "UART thread started on %s @ %d bps\n", options.port, options.baudrate);

    while (!g_stop) {
        uint32_t sleep_ms;

        app_ui_process(&ui);
        sleep_ms = lv_timer_handler();
        if (sleep_ms == LV_NO_TIMER_READY || sleep_ms > 50U) {
            sleep_ms = 5U;
        }
        usleep((useconds_t)(sleep_ms * 1000U));
    }

    uart_reader_join(&reader);
    app_ui_process(&ui);
    frame_queue_destroy(&queue);
    return 0;
}
