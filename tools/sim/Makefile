# Host preview build for the CYD screens. See preview_main.cpp.
#
#   make && ./build/preview ../../.tmp/preview
#
# LVGL comes from the PlatformIO dependency tree, so run `pio pkg install -e cyd`
# first if .pio/libdeps is missing.

HERE    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../..)
LVGL    := $(ROOT)/.pio/libdeps/cyd/lvgl
BUILD   := $(HERE)build

CPPFLAGS := -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DHELMET_BOARD_CYD=1 \
            -I$(ROOT)/include -I$(LVGL) -I$(HERE)shim
CFLAGS   := -O1 -w
CXXFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter

LVGL_SRC := $(shell find $(LVGL)/src -name '*.c')
APP_SRC  := $(ROOT)/src/ui_screens.cpp $(ROOT)/src/ui_theme.cpp $(HERE)preview_main.cpp
BRAND_SRC := $(wildcard $(ROOT)/src/brand/*.c)

LVGL_OBJ := $(patsubst $(LVGL)/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRC))
APP_OBJ  := $(addprefix $(BUILD)/app/,$(notdir $(APP_SRC:.cpp=.o))) \
            $(addprefix $(BUILD)/brand/,$(notdir $(BRAND_SRC:.c=.o)))

all: $(BUILD)/preview

$(BUILD)/preview: $(LVGL_OBJ) $(APP_OBJ)
	@mkdir -p $(@D)
	$(CXX) -o $@ $^ -lm

$(BUILD)/lvgl/%.o: $(LVGL)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/brand/%.o: $(ROOT)/src/brand/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/app/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/app/%.o: $(HERE)%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
