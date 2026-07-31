#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#ifdef GUPPY_WAYLAND
#include "lv_drivers/wayland/wayland.h"
#endif
#ifdef GUPPY_CALIBRATE
#include "lv_tc.h"
#include "lv_tc_screen.h"
#endif
#include <unistd.h>
#include <cstdlib>
#include <experimental/filesystem>
#include <fstream>
#include <time.h>

#include "hv/json.hpp"
#include "wifi_panel.h"
#include "hv/hlog.h"
#include "logger.h"

using namespace hv;
namespace fs = std::experimental::filesystem;
using json = nlohmann::json;

#define DISP_BUF_SIZE (128 * 1024)

namespace {
constexpr double calibration_version = 2.0;

std::uint32_t get_display_rotate() {
    const char *rotate_env = std::getenv("DISPLAY_ROTATE");
    if (rotate_env == nullptr || rotate_env[0] == '\0') {
        return 0;
    }

    char *end = nullptr;
    const unsigned long rotate = std::strtoul(rotate_env, &end, 10);
    if (end == rotate_env || *end != '\0' || rotate > 3) {
        LOG_ERROR("Invalid DISPLAY_ROTATE='{}', expected 0-3", rotate_env);
        return 0;
    }

    return static_cast<std::uint32_t>(rotate);
}

#ifdef GUPPY_CALIBRATE
fs::path get_calibration_path() {
    return fs::canonical("/proc/self/exe").parent_path() / "calibration.json";
}

std::vector<float> load_calibration_coeff() {
    std::ifstream f(get_calibration_path());
    if (!f.is_open()) {
        return {};
    }

    json j;
    f >> j;
    if (!j.is_object()) {
        LOG_INFO("Discarding calibration data: legacy or invalid format");
        return {};
    }

    if (!j.contains("version") || !j["version"].is_number() ||
        j["version"].get<double>() != calibration_version) {
        LOG_INFO("Discarding calibration data: missing or unsupported version");
        return {};
    }

    if (!j.contains("display_rotate") || !j["display_rotate"].is_number_unsigned()) {
        LOG_INFO("Discarding calibration data: missing display_rotate");
        return {};
    }

    const auto saved_rotate = j["display_rotate"].get<std::uint32_t>();
    const auto current_rotate = get_display_rotate();
    if (saved_rotate != current_rotate) {
        LOG_INFO("Discarding calibration data: display_rotate changed from {} to {}", saved_rotate, current_rotate);
        return {};
    }

    if (!j.contains("calibrations") || !j["calibrations"].is_array()) {
        LOG_INFO("Discarding calibration data: missing calibrations");
        return {};
    }

    const auto &values = j["calibrations"];
    if (values.size() != 6) {
        LOG_INFO("Discarding calibration data: expected 6 coefficients, got {}", values.size());
        return {};
    }

    std::vector<float> coeffs;
    coeffs.reserve(values.size());
    for (const auto &v : values) {
        coeffs.push_back(v.get<float>());
    }
    return coeffs;
}

void save_calibration_coeff(lv_tc_coeff_t coeff) {
    json j = {
        {"version", calibration_version},
        {"display_rotate", get_display_rotate()},
        {"calibrations", {coeff.a, coeff.b, coeff.c, coeff.d, coeff.e, coeff.f}},
    };
    std::ofstream f(get_calibration_path(), std::ios::trunc);
    f << j.dump(2);
}

void handle_calibrated(lv_event_t *event) {
    LOG_INFO("Finished calibration");
    lv_obj_t *main_screen = static_cast<lv_obj_t *>(event->user_data);
    lv_disp_load_scr(main_screen);
}
#endif
}

uint32_t custom_tick_get(void) {
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timespec ts_start;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        start_ms = static_cast<uint64_t>(ts_start.tv_sec) * 1000ULL +
                   static_cast<uint64_t>(ts_start.tv_nsec) / 1000000ULL;
    }

    struct timespec ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    const uint64_t now_ms = static_cast<uint64_t>(ts_now.tv_sec) * 1000ULL +
                            static_cast<uint64_t>(ts_now.tv_nsec) / 1000000ULL;

    return static_cast<uint32_t>(now_ms - start_ms);
}

static void hal_init(lv_color_t primary, lv_color_t secondary) {
    lv_disp_t *disp = nullptr;

#ifdef GUPPY_WAYLAND
    lv_wayland_init();

    disp = lv_wayland_create_window(
#ifdef GUPPY_SMALL_SCREEN
        static_cast<lv_coord_t>(480),
        static_cast<lv_coord_t>(272),
#else
        static_cast<lv_coord_t>(800),
        static_cast<lv_coord_t>(480),
#endif
        const_cast<char *>("GrumpyScreen Bootstrap"),
        nullptr);
    if (disp == nullptr) {
        LOG_ERROR("Failed to create Wayland window");
        std::exit(1);
    }
#else
    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, buf2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;

    uint32_t width;
    uint32_t height;
    uint32_t dpi;
    fbdev_get_sizes(&width, &height, &dpi);

    disp_drv.hor_res = width;
    disp_drv.ver_res = height;
    const auto rotate_value = get_display_rotate();
    if (rotate_value > 0 && rotate_value < 4) {
        disp_drv.sw_rotate = 1;
        disp_drv.rotated = rotate_value;
    }

    disp = lv_disp_drv_register(&disp_drv);

    const char *path = std::getenv("LVGL_EVDEV_DEV");
    if (path != nullptr && path[0] != '\0') {
        LOG_INFO("Input Device is: {}", path);
        evdev_set_file(const_cast<char *>(path));
    } else {
        evdev_init();
    }
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = evdev_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
#ifdef GUPPY_CALIBRATE
    lv_tc_indev_drv_init(&indev_drv, evdev_read);
#endif
    lv_indev_drv_register(&indev_drv);
#endif

#ifdef GUPPY_SMALL_SCREEN
    lv_theme_t *th = lv_theme_default_init(disp, primary, secondary, true, &lv_font_montserrat_12);
#else
    lv_theme_t *th = lv_theme_default_init(disp, primary, secondary, true, &lv_font_montserrat_20);
#endif
    lv_disp_set_theme(disp, th);
}

int main(void) {
    hlog_disable();
    set_log_level(LogLevel::INFO);

    lv_init();

#ifndef GUPPY_WAYLAND
    fbdev_init();
    fbdev_unblank();
#endif

    hal_init(lv_color_hex(0x2196F3), lv_color_hex(0xF44336));

    std::mutex lv_lock;
    const std::string footer_text = fmt::format("Build {}", GUPPYSCREEN_VERSION);
    WifiPanel wifi_panel(lv_lock, WifiPanelOptions{
        lv_scr_act(),
        false,
        footer_text.c_str(),
        {}
    });
    wifi_panel.foreground();

#ifdef GUPPY_CALIBRATE
    lv_obj_t *main_screen = lv_disp_get_scr_act(NULL);
    const auto coeffs = load_calibration_coeff();
    if (coeffs.empty()) {
        lv_tc_register_coeff_save_cb(&save_calibration_coeff);
        lv_obj_t *touch_calibrate_scr = lv_tc_screen_create();
        lv_disp_load_scr(touch_calibrate_scr);
        lv_tc_screen_start(touch_calibrate_scr);
        lv_obj_add_event_cb(touch_calibrate_scr, &handle_calibrated, LV_EVENT_READY, main_screen);
        LOG_INFO("Running touch calibration");
    } else {
        lv_tc_coeff_t coeff = {true, coeffs[0], coeffs[1], coeffs[2], coeffs[3], coeffs[4], coeffs[5]};
        lv_tc_set_coeff(coeff, false);
        LOG_INFO("Loaded calibration coefficients");
    }
#endif

    while (1) {
        lv_lock.lock();
        lv_timer_handler();

#ifdef GUPPY_WAYLAND
        if (!lv_wayland_window_is_open(NULL)) {
            lv_lock.unlock();
            lv_wayland_deinit();
            return 0;
        }
#endif

        lv_lock.unlock();
        usleep(5000);
    }
}
