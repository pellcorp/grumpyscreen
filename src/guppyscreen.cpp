#include "guppyscreen.h"

#include "config.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "logger.h"
#include "state.h"
#include "theme.h"
#ifdef GUPPY_CALIBRATE
#include <fstream>
#endif
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

GuppyScreen *GuppyScreen::instance = NULL;
lv_style_t GuppyScreen::style_container;
lv_style_t GuppyScreen::style_imgbtn_default;
lv_style_t GuppyScreen::style_imgbtn_pressed;
lv_style_t GuppyScreen::style_imgbtn_disabled;
lv_theme_t GuppyScreen::th_new;

lv_obj_t *GuppyScreen::screen_saver = NULL;

KWebSocketClient GuppyScreen::ws(NULL);

std::mutex GuppyScreen::lv_lock;

GuppyScreen::GuppyScreen()
  : spoolman_panel(ws, lv_lock)
  , main_panel(ws, lv_lock, spoolman_panel)
  , init_panel(main_panel, lv_lock)
{
  main_panel.create_panel();
}

GuppyScreen *GuppyScreen::get() {
  if (instance == NULL) {
    instance = new GuppyScreen();
  }

  return instance;
}

GuppyScreen *GuppyScreen::init(std::function<void(lv_color_t, lv_color_t)> hal_init) {
  hlog_disable();

  // config
  Config *conf = Config::get_instance();
  auto ll = conf->get<std::string>("/log_level");
  set_log_level(ll);

  const std::string selected_theme = conf->get<std::string>("/theme");
  auto theme_config = fs::canonical(conf->get_path()).parent_path() / "themes" / (selected_theme + ".json");
  ThemeConfig *theme_conf = ThemeConfig::get_instance();
  theme_conf->init(theme_config);

  auto primary_color = lv_color_hex(std::stoul(theme_conf->get<std::string>("/primary_color"), nullptr, 16));
  auto secondary_color = lv_color_hex(std::stoul(theme_conf->get<std::string>("/secondary_color"), nullptr, 16));

  LOG_INFO("GrumpyScreen Version: {}-{}", GUPPYSCREEN_BRANCH, GUPPYSCREEN_VERSION);

  LOG_INFO("DPI: {}", LV_DPI_DEF);
  /*LittlevGL init*/
  lv_init();

  /*Linux frame buffer device init*/
  fbdev_init();
  fbdev_unblank();

  hal_init(primary_color, secondary_color);
  lv_png_init();

  lv_style_init(&style_container);
  lv_style_set_border_width(&style_container, 0);
  lv_style_set_radius(&style_container, 0);

  lv_style_init(&style_imgbtn_pressed);
  lv_style_set_img_recolor_opa(&style_imgbtn_pressed, LV_OPA_100);
  lv_style_set_img_recolor(&style_imgbtn_pressed, primary_color);

  lv_style_init(&style_imgbtn_disabled);
  lv_style_set_img_recolor_opa(&style_imgbtn_disabled, LV_OPA_100);
  lv_style_set_img_recolor(&style_imgbtn_disabled, lv_palette_darken(LV_PALETTE_GREY, 1));

  /*Initia1ize the new theme from the current theme*/

  lv_theme_t *th_act = lv_disp_get_theme(NULL);
  th_new = *th_act;

  /*Set the parent theme and the style apply callback for the new theme*/
  lv_theme_set_parent(&th_new, th_act);
  lv_theme_set_apply_cb(&th_new, &GuppyScreen::new_theme_apply_cb);

  /*Assign the new theme to the current display*/
  lv_disp_set_theme(NULL, &th_new);

  ws.register_notify_update(State::get_instance());

  GuppyScreen *gs = GuppyScreen::get();
  // start initializing all guppy components
  std::string ws_url = fmt::format("ws://{}:{}/websocket",
                                   conf->get<std::string>("/moonraker_host"),
                                   conf->get<uint32_t>("/moonraker_port"));

  LOG_INFO("connecting to printer at {}", ws_url);
  gs->connect_ws(ws_url);

  screen_saver = lv_obj_create(lv_scr_act());

  lv_obj_set_size(screen_saver, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(screen_saver, LV_OPA_100, 0);
  lv_obj_move_background(screen_saver);

#ifdef GUPPY_CALIBRATE
  lv_obj_t *main_screen = lv_disp_get_scr_act(NULL);
  std::vector<float> c = GuppyScreen::load_calibration_coeff();
  if (c.empty()) {
    lv_tc_register_coeff_save_cb(&GuppyScreen::save_calibration_coeff);
    lv_obj_t *touch_calibrate_scr = lv_tc_screen_create();
    lv_disp_load_scr(touch_calibrate_scr);
    lv_tc_screen_start(touch_calibrate_scr);
    lv_obj_add_event_cb(touch_calibrate_scr, &GuppyScreen::handle_calibrated, LV_EVENT_READY, main_screen);
    LOG_INFO("running touch calibration");
  } else {
    // load calibration data
    lv_tc_coeff_t coeff = {true, c[0], c[1], c[2], c[3], c[4], c[5]};
    lv_tc_set_coeff(coeff, false);
    LOG_INFO("loaded calibration coefficients");
  }
#endif
  return gs;
}

void GuppyScreen::loop() {
  /*Handle LitlevGL tasks (tickless mode)*/
  std::atomic_bool is_sleeping(false);
  Config *conf = Config::get_instance();
  int32_t display_sleep = conf->get<int32_t>("/display_sleep_sec") * 1000;

  while (1) {
    lv_lock.lock();
    lv_timer_handler();
    lv_lock.unlock();

    if (display_sleep != -1) {
      if (lv_disp_get_inactive_time(NULL) > display_sleep) {
        if (!is_sleeping.load()) {
          LOG_DEBUG("putting display to sleeping");
          fbdev_blank();
          lv_obj_move_foreground(screen_saver);
          // LOG_DEBUG("screen saver foreground");
          is_sleeping = true;
        }
      } else {
        if (is_sleeping.load()) {
          LOG_DEBUG("waking up display");
          fbdev_unblank();
          lv_obj_move_background(screen_saver);
          is_sleeping = false;
        }
      }
    }

    usleep(5000);
  }
}

std::mutex &GuppyScreen::get_lock() {
  return lv_lock;
}

void GuppyScreen::connect_ws(const std::string &url) {
  init_panel.set_message(LV_SYMBOL_WARNING " Waiting for printer to initialise...");
  ws.connect(url.c_str(),
   [this]() { init_panel.connected(ws); },
   [this]() { init_panel.disconnected(ws); });
}

void GuppyScreen::new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  if (lv_obj_check_type(obj, &lv_obj_class)) {
    lv_obj_add_style(obj, &style_container, 0);
  }

  if (lv_obj_check_type(obj, &lv_imgbtn_class)) {
    lv_obj_add_style(obj, &style_imgbtn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &style_imgbtn_disabled, LV_STATE_DISABLED);
  }
}

#ifdef GUPPY_CALIBRATE
void GuppyScreen::handle_calibrated(lv_event_t *event) {
  LOG_INFO("finished calibration");
  lv_obj_t *main_screen = (lv_obj_t *)event->user_data;
  lv_disp_load_scr(main_screen);
}

std::vector<float> GuppyScreen::load_calibration_coeff() {
  std::string config_path = fs::canonical("/proc/self/exe").parent_path() / "calibration.json";
  std::ifstream f(config_path);
  if (!f.is_open()) {
    return {};
  }

  json j;
  f >> j;
  if (!j.is_array()) {
    return {};
  }

  std::vector<float> coeffs;
  coeffs.reserve(j.size());
  for (const auto& v : j) {
    coeffs.push_back(v.get<float>());
  }
  return coeffs;
}

void GuppyScreen::save_calibration_coeff(lv_tc_coeff_t coeff) {
  auto config_path = fs::canonical("/proc/self/exe").parent_path() / "calibration.json";
  json j = {coeff.a, coeff.b, coeff.c, coeff.d, coeff.e, coeff.f};
  std::ofstream f(config_path, std::ios::trunc);
  f << j.dump(2);
}
#endif

void GuppyScreen::refresh_theme() {
  lv_theme_t *th = lv_theme_default_get();
  ThemeConfig *theme_conf = ThemeConfig::get_instance();
  auto primary_color = lv_color_hex(std::stoul(theme_conf->get<std::string>("/primary_color"), nullptr, 16));
  auto secondary_color = lv_color_hex(std::stoul(theme_conf->get<std::string>("/secondary_color"), nullptr, 16));

  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t * new_theme =  lv_theme_default_init(disp, primary_color, secondary_color, true, th->font_normal);
  lv_disp_set_theme(disp, new_theme);
  lv_style_set_img_recolor(&style_imgbtn_pressed, primary_color);
}

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void) {
  static uint64_t start_ms = 0;
  if (start_ms == 0) {
    struct timeval tv_start;
    gettimeofday(&tv_start, NULL);
    start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
  }

  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  uint64_t now_ms;
  now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

  uint32_t time_ms = now_ms - start_ms;
  return time_ms;
}
