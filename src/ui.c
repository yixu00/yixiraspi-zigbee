#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lvgl/lvgl.h"

static void append_log(app_ui_t *ui, const char *frame_text) {
    const size_t total = sizeof(ui->log_text);
    size_t current_len = strlen(ui->log_text);
    size_t entry_len = strlen(frame_text);
    const char *entry = frame_text;

    if (entry_len + 1 >= total) {
        entry = frame_text + (entry_len - (total / 2));
        entry_len = strlen(entry);
    }

    if (current_len == 0) {
        snprintf(ui->log_text, total, "%s", entry);
    } else {
        size_t required = current_len + 1 + entry_len + 1;
        if (required > total) {
            size_t overflow = required - total;
            if (overflow >= current_len) {
                ui->log_text[0] = '\0';
                current_len = 0;
            } else {
                memmove(ui->log_text, ui->log_text + overflow, current_len - overflow + 1);
                current_len -= overflow;
            }
        }
        if (current_len > 0) {
            snprintf(ui->log_text + current_len, total - current_len, "\n%s", entry);
        } else {
            snprintf(ui->log_text, total, "%s", entry);
        }
    }

    lv_label_set_text(ui->log_label, ui->log_text);
}

static void update_chart_range(app_ui_t *ui, double value) {
    int32_t min_value;
    int32_t max_value;

    if (value < ui->chart_min) {
        ui->chart_min = value;
    }
    if (value > ui->chart_max) {
        ui->chart_max = value;
    }

    min_value = (int32_t)floor(ui->chart_min);
    max_value = (int32_t)ceil(ui->chart_max);
    if (min_value == max_value) {
        min_value -= 1;
        max_value += 1;
    }

    lv_chart_set_range(ui->chart, LV_CHART_AXIS_PRIMARY_Y, min_value, max_value);
}

static void update_status(app_ui_t *ui) {
    unsigned long frame_count = 0;
    bool running = false;
    bool error = false;
    char error_text[128];
    const char *onenet_status;

    uart_reader_get_status(ui->reader, &frame_count, &running, &error, error_text, sizeof(error_text));
    onenet_status = onenet_uploader_status(&ui->uploader);

    if (error) {
        snprintf(
            ui->status_text,
            sizeof(ui->status_text),
            "Port: %s @ %d bps | Display: %s | Status: %s | Frames: %lu | OneNET: %s",
            ui->options.port,
            ui->options.baudrate,
            ui->fbdev_path,
            error_text,
            frame_count,
            onenet_status
        );
        lv_label_set_text(ui->status_label, ui->status_text);
        return;
    }

    snprintf(
        ui->status_text,
        sizeof(ui->status_text),
        "Port: %s @ %d bps | Display: %s | Status: %s | Frames: %lu | OneNET: %s",
        ui->options.port,
        ui->options.baudrate,
        ui->fbdev_path,
        running ? "listening" : "stopped",
        frame_count,
        onenet_status
    );
    lv_label_set_text(ui->status_label, ui->status_text);
}

void app_ui_init(app_ui_t *ui, frame_queue_t *queue, uart_reader_t *reader, const uart_options_t *options, const char *fbdev_path) {
    lv_obj_t *screen;
    lv_obj_t *container;
    lv_obj_t *title;
    lv_obj_t *chart_label;
    lv_obj_t *latest_title;
    lv_obj_t *log_title;

    memset(ui, 0, sizeof(*ui));
    ui->queue = queue;
    ui->reader = reader;
    ui->options = *options;
    ui->fbdev_path = fbdev_path;
    ui->chart_min = 0.0;
    ui->chart_max = 100.0;
    onenet_uploader_init(&ui->uploader, options);

    screen = lv_screen_active();
    container = lv_obj_create(screen);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(container, 8, 0);
    lv_obj_set_style_pad_row(container, 6, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    title = lv_label_create(container);
    lv_label_set_text(title, "ZigBee UART Data Visualizer");
    lv_obj_set_width(title, lv_pct(100));

    ui->status_label = lv_label_create(container);
    lv_label_set_long_mode(ui->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui->status_label, lv_pct(100));

    chart_label = lv_label_create(container);
    lv_label_set_text(chart_label, "Latest numeric trend");
    lv_obj_set_width(chart_label, lv_pct(100));

    ui->chart = lv_chart_create(container);
    lv_obj_set_width(ui->chart, lv_pct(100));
    lv_obj_set_height(ui->chart, lv_pct(30));
    lv_chart_set_type(ui->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ui->chart, 40);
    lv_chart_set_range(ui->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    ui->series = lv_chart_add_series(ui->chart, lv_color_hex(0x2E86DE), LV_CHART_AXIS_PRIMARY_Y);

    latest_title = lv_label_create(container);
    lv_label_set_text(latest_title, "Latest frame");
    lv_obj_set_width(latest_title, lv_pct(100));

    ui->latest_frame_label = lv_label_create(container);
    lv_obj_set_width(ui->latest_frame_label, lv_pct(100));
    lv_label_set_long_mode(ui->latest_frame_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui->latest_frame_label, "Waiting for UART data...");

    log_title = lv_label_create(container);
    lv_label_set_text(log_title, "Recent frames");
    lv_obj_set_width(log_title, lv_pct(100));

    ui->log_label = lv_label_create(container);
    lv_obj_set_width(ui->log_label, lv_pct(100));
    lv_obj_set_height(ui->log_label, lv_pct(20));
    lv_label_set_long_mode(ui->log_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui->log_label, "Waiting for UART data...");

    update_status(ui);
}

void app_ui_process(app_ui_t *ui) {
    frame_message_t message;

    update_status(ui);

    while (frame_queue_pop(ui->queue, &message)) {
        lv_label_set_text(ui->latest_frame_label, message.text);
        append_log(ui, message.text);
        onenet_uploader_publish_frame(&ui->uploader, message.text);

        if (message.has_numeric_value) {
            update_chart_range(ui, message.numeric_value);
            lv_chart_set_next_value(ui->chart, ui->series, (int32_t)lround(message.numeric_value));
            lv_chart_refresh(ui->chart);
        }
    }
}

void app_ui_cleanup(app_ui_t *ui) {
    onenet_uploader_cleanup(&ui->uploader);
}
