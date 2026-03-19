#ifndef UI_H
#define UI_H

#include <stdint.h>

#include "lvgl/lvgl.h"

#include "frame_queue.h"
#include "onenet_uploader.h"
#include "uart_reader.h"

typedef struct {
    lv_obj_t *status_label;
    lv_obj_t *latest_frame_label;
    lv_obj_t *log_label;
    lv_obj_t *chart;
    lv_chart_series_t *series;
    frame_queue_t *queue;
    uart_reader_t *reader;
    uart_options_t options;
    onenet_uploader_t uploader;
    const char *fbdev_path;
    char status_text[448];
    char log_text[FRAME_BUFFER_CAPACITY * 6];
    double chart_min;
    double chart_max;
} app_ui_t;

void app_ui_init(app_ui_t *ui, frame_queue_t *queue, uart_reader_t *reader, const uart_options_t *options, const char *fbdev_path);
void app_ui_process(app_ui_t *ui);
void app_ui_cleanup(app_ui_t *ui);

#endif
