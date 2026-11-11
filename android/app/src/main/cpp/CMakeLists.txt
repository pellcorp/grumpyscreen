# Builds grumpyscreen as libgrumpyscreen.so for the Android app. The sources
# are the same ones the Makefile builds; only main.cpp is swapped for the JNI
# entry point in grumpy_android.cpp, which renders into the app's Surface.
cmake_minimum_required(VERSION 3.22.1)
project(grumpyscreen C CXX ASM)

if(NOT DEFINED GRUMPY_ROOT)
    get_filename_component(GRUMPY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ABSOLUTE)
endif()
if(NOT DEFINED GUPPYSCREEN_VERSION)
    set(GUPPYSCREEN_VERSION unknown)
endif()
if(NOT DEFINED GUPPYSCREEN_BRANCH)
    set(GUPPYSCREEN_BRANCH unknown)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    # LVGL software rendering is unusably slow unoptimised
    add_compile_options(-O2)
endif()

# --- libhv (websocket + http client) ---------------------------------------
# Same feature set as libhv/config.mk in the Makefile build.
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(WITH_EVPP ON CACHE BOOL "" FORCE)
set(WITH_HTTP ON CACHE BOOL "" FORCE)
set(WITH_HTTP_SERVER ON CACHE BOOL "" FORCE)
set(WITH_HTTP_CLIENT ON CACHE BOOL "" FORCE)
# libhv's CMakeLists writes hconfig.h and include/hv back into its source
# tree. Build a private copy so the NDK's answers never reach the checkout the
# Makefile builds use, and each ABI gets its own.
set(LIBHV_COPY ${CMAKE_CURRENT_BINARY_DIR}/libhv-src)
file(COPY ${GRUMPY_ROOT}/libhv/ DESTINATION ${LIBHV_COPY}
     PATTERN .git EXCLUDE PATTERN include EXCLUDE PATTERN lib EXCLUDE)
add_subdirectory(${LIBHV_COPY} ${CMAKE_CURRENT_BINARY_DIR}/libhv)

# --- LVGL --------------------------------------------------------------------
# lvgl.mk compiles everything under lvgl/src; lv_conf.h at the repo root is
# found the same way (lvgl/src/lv_conf_internal.h looks two levels up).
file(GLOB_RECURSE LVGL_SRC ${GRUMPY_ROOT}/lvgl/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC ${GRUMPY_ROOT} ${GRUMPY_ROOT}/lvgl)

# --- FreeType -----------------------------------------------------------------
# Draws the text at whatever size the screen needs (grumpy_fonts.cpp). Only
# what that takes: TrueType and WOFF (zlib), no HarfBuzz, PNG, bzip2 or Brotli.
# Use the vendored upstream archive so clean builds need no FreeType download.
include(FetchContent)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
    URL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype-2.13.3.tar.xz"
    URL_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289)
FetchContent_MakeAvailable(freetype)

# Montserrat Medium, the face of LVGL's built-in fonts, and the FontAwesome
# file their LV_SYMBOL_* glyphs come from
set(GRUMPY_FONT_TEXT ${GRUMPY_ROOT}/lvgl/scripts/built_in_font/Montserrat-Medium.ttf)
set(GRUMPY_FONT_SYMBOLS ${GRUMPY_ROOT}/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff)
set(FONTS_S ${CMAKE_CURRENT_BINARY_DIR}/grumpy_fonts_data.S)
configure_file(grumpy_fonts_data.S.in ${FONTS_S} @ONLY)
set_source_files_properties(${FONTS_S} PROPERTIES
    OBJECT_DEPENDS "${GRUMPY_FONT_TEXT};${GRUMPY_FONT_SYMBOLS}")

# --- grumpyscreen ------------------------------------------------------------
# Icons: instead of the fixed-size bitmaps in assets/material*, the SVGs in
# assets/material_svg, which grumpy_icons.c draws at the screen's size.
set(ICON_SVG_DIR ${GRUMPY_ROOT}/assets/material_svg)
file(GLOB ICON_SVGS ${ICON_SVG_DIR}/*.svg)
set(ICONS_C ${CMAKE_CURRENT_BINARY_DIR}/grumpy_icons_data.c)
add_custom_command(
    OUTPUT ${ICONS_C}
    COMMAND ${CMAKE_COMMAND} -DSVG_DIR=${ICON_SVG_DIR}
            -DOUT=${ICONS_C}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.cmake
    DEPENDS ${ICON_SVGS} ${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.cmake
    COMMENT "Embedding icons")

file(GLOB GRUMPY_SRC
    ${GRUMPY_ROOT}/src/*.cpp
    ${GRUMPY_ROOT}/assets/*.c)
list(REMOVE_ITEM GRUMPY_SRC
    ${GRUMPY_ROOT}/src/main.cpp
    # Android manages the device's wifi: no Network tab, no wpa_supplicant
    ${GRUMPY_ROOT}/src/wifi_panel.cpp
    ${GRUMPY_ROOT}/src/wpa_event.cpp
    # COSMOS only, as in the Makefile
    ${GRUMPY_ROOT}/src/update_manager_client.cpp)

# the console log's mono face in the sizes a phone needs (android/tools/console-fonts.py)
file(GLOB CONSOLE_FONTS ${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.c)
add_library(grumpyscreen SHARED ${GRUMPY_SRC} grumpy_android.cpp grumpy_icons.c ${ICONS_C}
    ${CONSOLE_FONTS} grumpy_fonts.cpp ${FONTS_S})

target_include_directories(grumpyscreen PRIVATE
    # experimental/filesystem shim for the NDK's libc++
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
    ${GRUMPY_ROOT}
    ${GRUMPY_ROOT}/src
    ${GRUMPY_ROOT}/fmt/include
    ${LIBHV_COPY}/include)

target_compile_definitions(grumpyscreen PRIVATE
    _GNU_SOURCE
    MMU_BACKEND_AFC
    MMU_BACKEND_HH
    GUPPYSCREEN_VERSION="${GUPPYSCREEN_VERSION}"
    GUPPYSCREEN_BRANCH="${GUPPYSCREEN_BRANCH}"
    FACTORY_RESET_BUTTON_TEXT="Factory\\nReset"
    FACTORY_RESET_BUTTON_TITLE="Factory Reset"
    FACTORY_RESET_BUTTON_PROMPT="Are you sure you want to factory reset?"
    FACTORY_RESET_BUTTON_FAILURE="Failed to initiate factory reset!"
    FACTORY_RESET_BUTTON_SUCCESS="Your printer will restart shortly!")
if(GUPPY_SMALL_SCREEN)
    target_compile_definitions(grumpyscreen PRIVATE GUPPY_SMALL_SCREEN)
endif()

target_compile_options(grumpyscreen PRIVATE
    -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>)

target_link_libraries(grumpyscreen
    lvgl
    hv_static
    freetype
    android
    log
    m)
