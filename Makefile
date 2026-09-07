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

STRIP ?= strip

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
BIN 			= grumpyscreen
BUILD_DIR 		= ./build
BUILD_OBJ_DIR 	= $(BUILD_DIR)/obj
BUILD_BIN_DIR 	= $(BUILD_DIR)/bin

prefix 			?= /usr
bindir 			?= $(prefix)/bin

#Collect the files to compile
MAINSRC = 		$(wildcard $(LVGL_DIR)/src/*.cpp)

# Vendor drivers for the MMU panel, one file each. A build carries only the
# ones named here; grumpyscreen.cfg's [mmu] backend then picks between them at
# runtime. Leave it empty for a build with the panel but no vendor support:
#   make MMU_BACKENDS="afc"     just AFC (the default)
#   make MMU_BACKENDS=""        no vendor driver at all
MMU_BACKENDS 	?= afc
MMU_BACKEND_SRC = $(foreach b,$(MMU_BACKENDS),$(LVGL_DIR)/src/$(b)_backend.cpp)
MAINSRC 		:= $(filter-out $(filter %_backend.cpp,$(MAINSRC)),$(MAINSRC)) $(MMU_BACKEND_SRC)
DEFINES 		+= $(foreach b,$(MMU_BACKENDS),-D MMU_BACKEND_$(shell echo $(b) | tr 'a-z' 'A-Z'))

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

ifdef GUPPY_WAYLAND
WAYLAND_SCANNER := $(shell command -v wayland-scanner 2>/dev/null)
WAYLAND_PROTOCOLS_BASE := $(shell pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null)
WAYLAND_CFLAGS := $(shell pkg-config --cflags wayland-client wayland-cursor xkbcommon 2>/dev/null)
WAYLAND_LIBS := $(shell pkg-config --libs wayland-client wayland-cursor xkbcommon 2>/dev/null)
ifeq ($(strip $(WAYLAND_PROTOCOLS_BASE)),)
WAYLAND_PROTOCOLS_BASE := $(shell test -d /usr/share/wayland-protocols && printf %s /usr/share/wayland-protocols)
endif
ifeq ($(strip $(WAYLAND_LIBS)),)
WAYLAND_LIBS := -lwayland-client -lwayland-cursor -lxkbcommon
endif
WAYLAND_XDG_PROTOCOL := $(WAYLAND_PROTOCOLS_BASE)/stable/xdg-shell/xdg-shell.xml
WAYLAND_PROTOCOL_GEN_DIR := $(BUILD_DIR)/wayland-src/protocols
WAYLAND_PROTOCOL_GEN_C := $(WAYLAND_PROTOCOL_GEN_DIR)/wayland-xdg-shell-client-protocol.c
WAYLAND_PROTOCOL_GEN_H := $(WAYLAND_PROTOCOL_GEN_DIR)/wayland-xdg-shell-client-protocol.h
WAYLAND_PROTOCOL_OBJ := $(BUILD_OBJ_DIR)/$(patsubst ./%,%,$(WAYLAND_PROTOCOL_GEN_C:.c=.o))
WAYLAND_SRC_OBJ := $(BUILD_OBJ_DIR)/lv_drivers/wayland/wayland.o

CSRCS			+= $(WAYLAND_PROTOCOL_GEN_C)
endif

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

ifdef COSMOS
  DEFINES	    += -D COSMOS='"$(COSMOS)"'

  UPDATE_TEXT    ?= Update\nCOSMOS
  UPDATE_TITLE   ?= $(subst \n, ,$(UPDATE_TEXT))
  UPDATE_PROMPT  ?= Are you sure you want to update COSMOS?\n\nThis will download and update to the latest version of COSMOS!
  UPDATE_FAILURE	?= Failed to initiate update COSMOS!
  UPDATE_SUCCESS	?= Your printer will restart shortly!

  DEFINES	    += -D UPDATE_BUTTON_TEXT='"$(UPDATE_TEXT)"'
  DEFINES	    += -D UPDATE_BUTTON_TITLE='"$(UPDATE_TITLE)"'
  DEFINES	    += -D UPDATE_BUTTON_PROMPT='"$(UPDATE_PROMPT)"'
  DEFINES	    += -D UPDATE_BUTTON_FAILURE='"$(UPDATE_FAILURE)"'
  DEFINES	    += -D UPDATE_BUTTON_SUCCESS='"$(UPDATE_SUCCESS)"'

  SWITCH_TO_STOCK_TEXT    ?= Switch to OC\nPatched
  SWITCH_TO_STOCK_TITLE   ?= $(subst \n, ,$(SWITCH_TO_STOCK_TEXT))
  SWITCH_TO_STOCK_PROMPT  ?= Are you sure you want to switch to OpenCentauri patched firmware?\n\nThis will take some time, **DO NOT TURN OFF YOUR PRINTER**, just wait for it to reboot.
  SWITCH_TO_STOCK_FAILURE	?= Failed to initiate switch to OC Patched!
  SWITCH_TO_STOCK_SUCCESS	?= Your printer will restart shortly!

  FACTORY_RESET_TEXT    ?= Factory\nReset
  FACTORY_RESET_TITLE   ?= $(subst \n, ,$(FACTORY_RESET_TEXT))
  FACTORY_RESET_PROMPT  ?= Are you sure you want factory reset?\n\nThis will reset all printer setting but it will stay using COSMOS, it will not switch back to stock.
  FACTORY_RESET_FAILURE	?= Failed to initiate factory reset!
  FACTORY_RESET_SUCCESS	?= Your printer will restart shortly!
else
  SWITCH_TO_STOCK_TEXT    ?= Switch to\nStock
  SWITCH_TO_STOCK_TITLE   ?= $(subst \n, ,$(SWITCH_TO_STOCK_TEXT))
  SWITCH_TO_STOCK_PROMPT  ?= Are you sure you want to switch to stock?\n\nThis will temporarily switch the printer to stock creality firmware!
  SWITCH_TO_STOCK_FAILURE	?= Failed to initiate switch to stock!
  SWITCH_TO_STOCK_SUCCESS	?= Please power cycle your printer!\nPlease wait for the stock screen to appear!

  FACTORY_RESET_TEXT    ?= Factory\nReset
  FACTORY_RESET_TITLE   ?= $(subst \n, ,$(FACTORY_RESET_TEXT))
  FACTORY_RESET_PROMPT  ?= Are you sure you want to factory reset?\n\nThis will reset the printer to stock creality firmware!
  FACTORY_RESET_FAILURE	?= Failed to initiate factory reset!
  FACTORY_RESET_SUCCESS	?= Your printer will restart shortly!\nPlease wait for the stock screen to appear!
endif

DEFINES	    += -D FACTORY_RESET_BUTTON_TEXT='"$(FACTORY_RESET_TEXT)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_TITLE='"$(FACTORY_RESET_TITLE)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_PROMPT='"$(FACTORY_RESET_PROMPT)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_FAILURE='"$(FACTORY_RESET_FAILURE)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_SUCCESS='"$(FACTORY_RESET_SUCCESS)"'

DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_TEXT='"$(SWITCH_TO_STOCK_TEXT)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_TITLE='"$(SWITCH_TO_STOCK_TITLE)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_PROMPT='"$(SWITCH_TO_STOCK_PROMPT)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_FAILURE='"$(SWITCH_TO_STOCK_FAILURE)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_SUCCESS='"$(SWITCH_TO_STOCK_SUCCESS)"'

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

ifdef GUPPY_WAYLAND
ifeq ($(strip $(WAYLAND_SCANNER)),)
$(error GUPPY_WAYLAND=1 requires wayland-scanner to be installed)
endif
ifeq ($(strip $(WAYLAND_PROTOCOLS_BASE)),)
$(error GUPPY_WAYLAND=1 requires wayland-protocols; could not locate pkgdatadir)
endif
ifeq ($(wildcard $(WAYLAND_XDG_PROTOCOL)),)
$(error GUPPY_WAYLAND=1 requires xdg-shell.xml at $(WAYLAND_XDG_PROTOCOL))
endif
LDFLAGS				:= $(filter-out -static,$(LDFLAGS))
INC					+= -I./lv_drivers/wayland -I./$(BUILD_DIR)/wayland-src $(WAYLAND_CFLAGS)
LDFLAGS				+= $(WAYLAND_LIBS)
DEFINES				+= -D GUPPY_WAYLAND -D USE_WAYLAND=1 -D LV_WAYLAND_XDG_SHELL=1 -D LV_WAYLAND_WL_SHELL=1
endif

COMPILE_CC				= $(CC) $(CFLAGS) $(INC) $(DEFINES)
COMPILE_CXX				= $(CC) $(CFLAGS) $(INC) $(DEFINES)

## MAINOBJ -> OBJFILES

all: default

libhv.a:
	$(MAKE) -C libhv -j$(nproc) libhv

wpaclient:
	$(MAKE) -C wpa_supplicant/wpa_supplicant -j$(nproc) libwpa_client.a

ifdef GUPPY_WAYLAND
$(WAYLAND_PROTOCOL_GEN_C) $(WAYLAND_PROTOCOL_GEN_H): $(WAYLAND_XDG_PROTOCOL)
	@mkdir -p $(WAYLAND_PROTOCOL_GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $(WAYLAND_PROTOCOL_GEN_H)
	$(WAYLAND_SCANNER) private-code $< $(WAYLAND_PROTOCOL_GEN_C)

$(WAYLAND_SRC_OBJ): $(WAYLAND_PROTOCOL_GEN_H)
$(WAYLAND_PROTOCOL_OBJ): $(WAYLAND_PROTOCOL_GEN_H)
endif

$(BUILD_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(COMPILE_CXX) -std=c++17 $(CFLAGS) -c $< -o $@
	@echo "CXX $<"

$(BUILD_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(COMPILE_CC)  $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: libhv.a wpaclient $(TARGET)
	@mkdir -p $(dir $(BUILD_BIN_DIR)/)
	$(CXX) -o $(BUILD_BIN_DIR)/$(BIN) $(TARGET) $(LDFLAGS) $(LDLIBS)
	@echo "CXX $<"
	@$(STRIP) $(BUILD_BIN_DIR)/grumpyscreen

libhvclean:
	$(MAKE) -C libhv clean

wpaclean:
	$(MAKE) -C wpa_supplicant/wpa_supplicant clean

clean:
	rm -rf $(BUILD_DIR)

test: libhv.a
	@mkdir -p $(BUILD_DIR)
	g++ -std=gnu++17 -O2 -I./src -Ilibhv/include/ tests/test_config.cpp -o $(BUILD_DIR)/test_config
	$(BUILD_DIR)/test_config
# the backend fixture stubs State and the websocket client, so it links
# only the backend under test (libhv for the client's base class)
	g++ -std=gnu++17 -O2 -I./src -I./fmt/include -Ilibhv/include/ $(MMU_BACKEND_SRC) \
		tests/test_backends.cpp src/notify_consumer.cpp -o $(BUILD_DIR)/test_backends \
		-Llibhv/lib -l:libhv.a -lpthread
	$(BUILD_DIR)/test_backends

-include			$(DEPS)
