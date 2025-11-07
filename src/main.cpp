#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#ifdef GUPPY_CALIBRATE
#include "lv_tc.h"
#include "lv_tc_screen.h"
#endif
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
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
    Config *conf = Config::get_instance();
    auto config_path = fs::canonical("/proc/self/exe").parent_path() / "guppyscreen.json";
    if (fs::exists(config_path)) {
      conf->init(config_path.string());
    } else {
      spdlog::error(fmt::format("Config file {} not found", config_path.string()));
      return 1;
    }
    GuppyScreen::init(hal_init);
    GuppyScreen::loop();
    return 0;
}

static void hal_init(lv_color_t primary, lv_color_t secondary) {
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
    auto rotate = conf->get_json("/display_rotate");
    if (!rotate.is_null()) {
      auto rotate_value = rotate.template get<uint32_t>();
      if (rotate_value > 0 && rotate_value < 4) {
        disp_drv.sw_rotate = 1;
        disp_drv.rotated = rotate_value;
      }
    }

    spdlog::debug("resolution {} x {}", width, height);

    lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
#ifdef GUPPY_SMALL_SCREEN
    lv_theme_t * th = lv_theme_default_init(NULL, primary, secondary, true, &lv_font_montserrat_12);
#else
    lv_theme_t * th = lv_theme_default_init(NULL, primary, secondary, true, &lv_font_montserrat_20);
#endif
    lv_disp_set_theme(disp, th);

    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.read_cb = evdev_read; // no calibration
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

#ifdef GUPPY_CALIBRATE
    spdlog::info("using touch calibration");
    lv_tc_indev_drv_init(&indev_drv_1, evdev_read);
#endif
    lv_indev_drv_register(&indev_drv_1);
}
