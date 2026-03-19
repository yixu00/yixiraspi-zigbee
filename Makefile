TARGET := rasuart_lvgl
CC := gcc
LVGL_REPO := https://github.com/lvgl/lvgl.git
LVGL_REF := v9.2.2
BUILD_DIR := build

APP_SRC := $(wildcard src/*.c)
LVGL_SRC := $(shell find lvgl/src -name '*.c' -print 2>/dev/null)
APP_OBJ := $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(APP_SRC))
LVGL_OBJ := $(patsubst lvgl/src/%.c,$(BUILD_DIR)/lvgl/%.o,$(LVGL_SRC))
OBJ := $(APP_OBJ) $(LVGL_OBJ)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)
CFLAGS := -O2 -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -DLV_CONF_INCLUDE_SIMPLE -I. -Ilvgl $(SDL_CFLAGS)
LDFLAGS := -pthread -lm -lrt -ldl $(SDL_LIBS)

.PHONY: all bootstrap-lvgl clean distclean

all: $(TARGET)

bootstrap-lvgl:
	@if [ ! -f "lvgl/lvgl.h" ]; then \
		echo "Fetching LVGL $(LVGL_REF)..."; \
		rm -rf lvgl; \
		git clone --depth 1 --branch $(LVGL_REF) $(LVGL_REPO) lvgl; \
	fi

$(TARGET): bootstrap-lvgl $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR)/src/%.o: src/%.c lv_conf.h | bootstrap-lvgl
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lvgl/%.o: lvgl/src/%.c lv_conf.h | bootstrap-lvgl
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(TARGET) $(BUILD_DIR)

distclean: clean
	rm -rf lvgl
