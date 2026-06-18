#
# Makefile
#
ifdef CROSS_COMPILE
CC 	= $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
CPP = $(CC) -E
AS 	= $(CROSS_COMPILE)as
LD	= $(CROSS_COMPILE)ld
AR	= $(CROSS_COMPILE)ar
NM	= $(CROSS_COMPILE)nm
STRIP 	= $(CROSS_COMPILE)strip
OBJCOPY 	= $(CROSS_COMPILE)objcopy
endif

LVGL_DIR_NAME 	?= lvgl
LVGL_DIR 		?= .

WARNINGS		:= -Wall -Wextra -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith \
					-fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess \
					-Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-error=pedantic \
					-Wno-sign-compare -Wdouble-promotion -Wclobbered -Wempty-body -Wtype-limits -Wshift-negative-value \
					-Wno-unused-value -Wno-unused-parameter -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized -Wall -Wextra -Wno-unused-parameter \
					-Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wpointer-arith -Wno-cast-qual \
					-Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-sign-compare
CFLAGS 			?= -O3 -g0 -MD -MP -I$(LVGL_DIR)/ $(WARNINGS)
LDFLAGS 		?= -static -lm -Llibhv/lib -l:libhv.a -latomic -lpthread -Lwpa_supplicant/wpa_supplicant/ -l:libwpa_client.a -lstdc++fs
BIN 			= guppyscreen
BUILD_DIR 		= ./build
BUILD_OBJ_DIR 	= $(BUILD_DIR)/obj
BUILD_BIN_DIR 	= $(BUILD_DIR)/bin

prefix 			?= /usr
bindir 			?= $(prefix)/bin

#Collect the files to compile
MAINSRC = 		$(wildcard $(LVGL_DIR)/src/*.cpp)

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

CSRCS 			+= $(wildcard $(LVGL_DIR)/assets/*.c)
ifdef GUPPY_CALIBRATE
CSRCS			+= $(wildcard $(LVGL_DIR)/lv_touch_calibration/*.c)
DEFINES += -D GUPPY_CALIBRATE
DEFINES += -D EVDEV_CALIBRATE
endif

ASSET_DIR		= material
ifdef GUPPY_SMALL_SCREEN
ASSET_DIR		= material_46
DEFINES			+= -D GUPPY_SMALL_SCREEN
endif

CSRCS 			+= $(wildcard $(LVGL_DIR)/assets/$(ASSET_DIR)/*.c)

ifdef GUPPYSCREEN_VERSION
SHORT_GUPPYSCREEN_VERSION := $(shell printf "%s" "$(GUPPYSCREEN_VERSION)" | cut -c1-7)
DEFINES			+= -D GUPPYSCREEN_VERSION=\"$(SHORT_GUPPYSCREEN_VERSION)\"
else
DEFINES			+= -D GUPPYSCREEN_VERSION=\"unknown\"
endif

ifdef GUPPYSCREEN_BRANCH
DEFINES			+= -D GUPPYSCREEN_BRANCH=\"$(GUPPYSCREEN_BRANCH)\"
else
DEFINES			+= -D GUPPYSCREEN_BRANCH=\"unknown\"
endif

ifdef UPDATE_CMD
DEFINES	    += -D UPDATE_BUTTON_CMD='"$(UPDATE_CMD)"'
endif

ifdef UPDATE_TEXT
UPDATE_TITLE ?= $(subst \n, ,$(UPDATE_TEXT))
DEFINES     += -D UPDATE_BUTTON_TEXT='"$(UPDATE_TEXT)"'
DEFINES	    += -D UPDATE_BUTTON_TITLE='"$(UPDATE_TITLE)"'
endif

ifdef UPDATE_SUCCESS
DEFINES     += -D UPDATE_BUTTON_SUCCESS='"$(UPDATE_SUCCESS)"'
endif

ifdef UPDATE_FAILURE
DEFINES     += -D UPDATE_BUTTON_FAILURE='"$(UPDATE_FAILURE)"'
endif

ifdef UPDATE_PROMPT
DEFINES     += -D UPDATE_BUTTON_PROMPT='"$(UPDATE_PROMPT)"'
endif

SWITCH_TO_STOCK_TEXT    ?= Switch to\nStock
SWITCH_TO_STOCK_TITLE   ?= $(subst \n, ,$(SWITCH_TO_STOCK_TEXT))
SWITCH_TO_STOCK_PROMPT  ?= Are you sure you want to switch to stock?\n\nThis will temporarily switch the printer to stock creality firmware!
SWITCH_TO_STOCK_FAILURE	?= Failed to initiate switch to stock!
SWITCH_TO_STOCK_SUCCESS	?= Please power cycle your printer!\nPlease wait for the stock screen to appear!

DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_TEXT='"$(SWITCH_TO_STOCK_TEXT)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_TITLE='"$(SWITCH_TO_STOCK_TITLE)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_PROMPT='"$(SWITCH_TO_STOCK_PROMPT)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_FAILURE='"$(SWITCH_TO_STOCK_FAILURE)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_SUCCESS='"$(SWITCH_TO_STOCK_SUCCESS)"'

FACTORY_RESET_TEXT    ?= Factory\nReset
FACTORY_RESET_TITLE   ?= $(subst \n, ,$(FACTORY_RESET_TEXT))
FACTORY_RESET_PROMPT  ?= Are you sure you want to factory reset?\n\nThis will reset the printer to stock creality firmware!
FACTORY_RESET_FAILURE	?= Failed to initiate factory reset!
FACTORY_RESET_SUCCESS	?= Your printer will restart shortly!\nPlease wait for the stock screen to appear!

DEFINES	    += -D FACTORY_RESET_BUTTON_TEXT='"$(FACTORY_RESET_TEXT)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_TITLE='"$(FACTORY_RESET_TITLE)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_PROMPT='"$(FACTORY_RESET_PROMPT)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_FAILURE='"$(FACTORY_RESET_FAILURE)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_SUCCESS='"$(FACTORY_RESET_SUCCESS)"'

OBJEXT 			?= .o

AOBJS 			= $(ASRCS:.S=$(OBJEXT))
COBJS 			= $(CSRCS:.c=$(OBJEXT))

MAINOBJ 		= $(MAINSRC:.cpp=$(OBJEXT))
DEPS                    = $(addprefix $(BUILD_OBJ_DIR)/, $(patsubst %.o, %.d, $(MAINOBJ)))

OBJS 			= $(AOBJS) $(COBJS) $(MAINOBJ)
TARGET 			= $(addprefix $(BUILD_OBJ_DIR)/, $(patsubst ./%, %, $(OBJS)))

INC 				:= -I./ -I./lvgl/ -I./lv_touch_calibration -I./fmt/include -I./libhv/include -I./wpa_supplicant/src/common
LDLIBS	 			:= -lm

DEFINES				+= -D _GNU_SOURCE -DSPDLOG_COMPILED_LIB

COMPILE_CC				= $(CC) $(CFLAGS) $(INC) $(DEFINES)
COMPILE_CXX				= $(CC) $(CFLAGS) $(INC) $(DEFINES)

## MAINOBJ -> OBJFILES

all: default

libhv.a:
	$(MAKE) -C libhv -j$(nproc) libhv

wpaclient:
	$(MAKE) -C wpa_supplicant/wpa_supplicant -j$(nproc) libwpa_client.a

$(BUILD_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(COMPILE_CXX) -std=c++17 $(CFLAGS) -c $< -o $@
	@echo "CXX $<"

$(BUILD_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(COMPILE_CC)  $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: $(TARGET)
	@mkdir -p $(dir $(BUILD_BIN_DIR)/)
	$(CXX) -o $(BUILD_BIN_DIR)/$(BIN) $(TARGET) $(LDFLAGS) $(LDLIBS)
	@echo "CXX $<"
	@$(STRIP) $(BUILD_BIN_DIR)/guppyscreen

libhvclean:
	$(MAKE) -C libhv clean

wpaclean:
	$(MAKE) -C wpa_supplicant/wpa_supplicant clean

clean:
	rm -rf $(BUILD_DIR)

test:
	@mkdir -p $(BUILD_DIR)
	g++ -std=gnu++17 -O2 -I./src -Ilibhv/include/ tests/test_config.cpp -o $(BUILD_DIR)/test_config
	$(BUILD_DIR)/test_config

-include			$(DEPS)
