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
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <cstdlib>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

static void hal_init(lv_color_t p, lv_color_t s);

#include "guppyscreen.h"
#include "hv/hlog.h"
#include "config.h"

#include <algorithm>

using namespace hv;

#define DISP_BUF_SIZE (128 * 1024)

int main(void) {
    const char* config_file_env = std::getenv("CONFIG_FILE");
    fs::path config_path = config_file_env && config_file_env[0] != '\0'
        ? fs::path(config_file_env)
        : fs::canonical("/proc/self/exe").parent_path() / "grumpyscreen.cfg";

    Config *conf = Config::get_instance();
    if (fs::exists(config_path)) {
        if (!conf->load(config_path.string())) {
            LOG_ERROR("Failed to load {}", config_path.string());
            return 1;
        }
    } else {
        LOG_ERROR("Config file {} not found", config_path.string());
        return 1;
    }

    const char* config_override_env = std::getenv("CONFIG_OVERRIDE_FILE");
    if (config_override_env && config_override_env[0] != '\0') {
        fs::path config_override_path = fs::path(config_override_env);
        if (fs::exists(config_override_path)) {
            if (!conf->load_override(config_override_path.string())) {
                LOG_ERROR("Failed to load override config {}", config_override_path.string());
                return 1;
            }
        } else {
            LOG_INFO("Override config file {} not found, continuing without overrides", config_override_path.string());
        }
    }

    GuppyScreen::init(hal_init);
    GuppyScreen::loop();
    return 0;
}

static void hal_init(lv_color_t primary, lv_color_t secondary) {
    lv_disp_t * disp = nullptr;

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
        const_cast<char *>("GrumpyScreen"),
        nullptr);
    if (disp == nullptr) {
        LOG_ERROR("Failed to create Wayland window");
        std::exit(1);
    }
#else
    /*A small buffer for LittlevGL to draw the screen's content*/
    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];

    /*Initialize a descriptor for the buffer*/
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, buf2, DISP_BUF_SIZE);

    /*Initialize and register a display driver*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = fbdev_flush;

    uint32_t width;
    uint32_t height;
    uint32_t dpi;
    fbdev_get_sizes(&width, &height, &dpi);
    
    disp_drv.hor_res    = width;
    disp_drv.ver_res    = height;

    Config *conf = Config::get_instance();
    auto rotate_value = conf->get<std::uint32_t>("/ui/display_rotate");
    if (rotate_value > 0 && rotate_value < 4) {
      disp_drv.sw_rotate = 1;
      disp_drv.rotated = rotate_value;
    }

    disp = lv_disp_drv_register(&disp_drv);

    const char *path = std::getenv("LVGL_EVDEV_DEV");
    if (path != nullptr && path[0] != '\0') {
        LOG_INFO("Input Device is: {}", path);
        evdev_set_file(const_cast<char *>(path));
    } else { // k1 will continue to use this
        evdev_init();
    }
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.read_cb = evdev_read; // no calibration
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

#ifdef GUPPY_CALIBRATE
    lv_tc_indev_drv_init(&indev_drv_1, evdev_read);
#endif
    lv_indev_drv_register(&indev_drv_1);
#endif

#ifdef GUPPY_SMALL_SCREEN
    lv_theme_t * th = lv_theme_default_init(disp, primary, secondary, true, &lv_font_montserrat_12);
#else
    lv_theme_t * th = lv_theme_default_init(disp, primary, secondary, true, &lv_font_montserrat_20);
#endif
    lv_disp_set_theme(disp, th);
}
