// Android entry point for grumpyscreen: stands in for src/main.cpp.
//
// The Java side owns a SurfaceView and forwards its surface and touches here.
// GuppyScreen runs on a thread of its own, started once for the life of the
// process; when Android takes the surface away (the app goes to the
// background) the loop keeps running and simply draws nowhere until a new
// surface arrives.

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <cstdarg>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <experimental/filesystem>

#include "lvgl/lvgl.h"
#include "config.h"
#include "guppyscreen.h"
#include "logger.h"
#include "theme.h"

// grumpy_icons.c
extern "C" void grumpy_icons_init(int hor_res, int ver_res);

namespace fs = std::experimental::filesystem;

namespace {

std::mutex window_lock;          // guards window and surface_w/h
ANativeWindow *window = nullptr;
int surface_w = 0;
int surface_h = 0;

// The LVGL display is fixed when the first surface arrives. A later surface
// of another size (multi-window, a resize) gets the same buffer geometry and
// the compositor scales it.
int disp_w = 0;
int disp_h = 0;
std::vector<uint32_t> frame;     // the whole screen, RGBA_8888

std::atomic<int32_t> touch_x{0};
std::atomic<int32_t> touch_y{0};
std::atomic<bool> touched{false};
// set on every press, cleared once LVGL has seen one: a tap shorter than
// LVGL's input poll period would otherwise go unnoticed
std::atomic<bool> press_pending{false};

// LVGL redraws, reads touch and steps animations on timers that lv_conf.h
// sets to 30ms (33fps), sized for printer panels. Here they follow the
// display: one display frame each.
std::atomic<int> frame_ms{16};
std::atomic<float> refresh_hz{60.0f};
lv_disp_t *lv_display = nullptr;
lv_indev_t *lv_touch = nullptr;

// the activity, to ask it for the printer list from the UI thread
JavaVM *java_vm = nullptr;
std::mutex activity_lock;
jobject activity = nullptr;  // global ref

std::atomic<bool> started{false};
std::atomic<bool> ready{false};

struct Paths {
  std::string config;
  std::string moonraker;
  std::string override_cfg;
};

// Call with the LVGL lock held (or before the UI loop starts).
void apply_frame_period() {
  const uint32_t p = static_cast<uint32_t>(frame_ms.load());
  if (lv_display != nullptr) lv_timer_set_period(lv_display->refr_timer, p);
  if (lv_touch != nullptr) lv_timer_set_period(lv_touch->driver->read_timer, p);
  if (lv_anim_get_timer() != nullptr) lv_timer_set_period(lv_anim_get_timer(), p);
}

// Tells the compositor the rate this window draws at, so a 90/120Hz panel
// is not held at 60. ANativeWindow_setFrameRate is API 30; look it up at run
// time since the app supports older releases.
void hint_frame_rate_locked() {
  using SetFrameRate = int32_t (*)(ANativeWindow *, float, int8_t);
  static SetFrameRate set_frame_rate = reinterpret_cast<SetFrameRate>(
      dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
  if (window != nullptr && set_frame_rate != nullptr) {
    set_frame_rate(window, refresh_hz.load(), 0 /* COMPATIBILITY_DEFAULT */);
  }
}

void present_locked() {
  if (window == nullptr) return;
  ANativeWindow_Buffer buffer;
  if (ANativeWindow_lock(window, &buffer, nullptr) != 0) return;
  const int rows = buffer.height < disp_h ? buffer.height : disp_h;
  const int cols = buffer.width < disp_w ? buffer.width : disp_w;
  auto *dst = static_cast<uint32_t *>(buffer.bits);
  const uint32_t *src = frame.data();
  for (int y = 0; y < rows; y++) {
    std::memcpy(dst, src, cols * sizeof(uint32_t));
    dst += buffer.stride;
    src += disp_w;
  }
  ANativeWindow_unlockAndPost(window);
}

void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  const int x1 = LV_MAX(area->x1, 0);
  const int y1 = LV_MAX(area->y1, 0);
  const int x2 = LV_MIN(area->x2, disp_w - 1);
  const int y2 = LV_MIN(area->y2, disp_h - 1);
  const int area_w = area->x2 - area->x1 + 1;

  // LVGL's 32 bit colour is B,G,R,A in memory; the window wants R,G,B,A.
  for (int y = y1; y <= y2; y++) {
    const lv_color_t *src = color_p + (y - area->y1) * area_w + (x1 - area->x1);
    uint32_t *dst = frame.data() + y * disp_w + x1;
    for (int x = x1; x <= x2; x++, src++, dst++) {
      *dst = 0xFF000000u | (uint32_t(src->ch.blue) << 16) |
             (uint32_t(src->ch.green) << 8) | uint32_t(src->ch.red);
    }
  }

  // one post per frame, not per strip
  if (lv_disp_flush_is_last(drv)) {
    std::lock_guard<std::mutex> g(window_lock);
    present_locked();
  }
  lv_disp_flush_ready(drv);
}

void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  data->point.x = touch_x.load();
  data->point.y = touch_y.load();
  const bool pending = press_pending.exchange(false);
  data->state = (touched.load() || pending) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

void hal_init(lv_color_t primary, lv_color_t secondary) {
  static std::vector<lv_color_t> buf;
  const uint32_t buf_px = static_cast<uint32_t>(disp_w) * disp_h / 4;
  buf.resize(buf_px);

  // single buffered: flush_cb copies the pixels before returning
  static lv_disp_draw_buf_t disp_buf;
  lv_disp_draw_buf_init(&disp_buf, buf.data(), nullptr, buf_px);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &disp_buf;
  disp_drv.flush_cb = flush_cb;
  disp_drv.hor_res = disp_w;
  disp_drv.ver_res = disp_h;
  disp_drv.dpi = LV_DPI_DEF * disp_w / 480;  // as main.cpp: scale with the screen
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  lv_display = disp;
  // before anything asks an icon its size
  grumpy_icons_init(disp_w, disp_h);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  lv_touch = lv_indev_drv_register(&indev_drv);

  apply_frame_period();

  const bool dark = lv_color_brightness(Theme::col(Theme::BG)) < 128;
  lv_theme_t *th = lv_theme_default_init(disp, primary, secondary, dark, Theme::scale_font(12));
  lv_disp_set_theme(disp, th);
}

bool load_config(const Paths &p) {
  Config *conf = Config::get_instance();
  if (!conf->load(p.config)) {
    LOG_ERROR("Failed to load {}", p.config);
    return false;
  }
  // The printer to talk to, written by the app from what the user entered.
  // It is loaded as an override so it can only replace keys the bundled
  // config already has.
  if (fs::exists(p.moonraker) && !conf->load_override(p.moonraker)) {
    LOG_ERROR("Failed to load {}", p.moonraker);
  }
  // Settings changed in the UI are written here.
  if (fs::exists(p.override_cfg) && !conf->load_override(p.override_cfg)) {
    LOG_ERROR("Failed to load override config {}", p.override_cfg);
  }
  conf->set_override_path(p.override_cfg);
  return true;
}

void ui_thread(Paths p) {
  if (!load_config(p)) return;
  GuppyScreen::init(hal_init);
  {
    // a refresh rate that arrived while init ran has been stored but not applied
    std::lock_guard<std::mutex> g(GuppyScreen::get()->get_lock());
    ready = true;
    apply_frame_period();
  }
  GuppyScreen::loop();
}

std::string jstr(JNIEnv *env, jstring s) {
  const char *c = env->GetStringUTFChars(s, nullptr);
  std::string out(c);
  env->ReleaseStringUTFChars(s, c);
  return out;
}

}  // namespace

// Calls a void method on the activity from the grumpyscreen UI thread.
static void call_activity(const char *method, const char *sig, ...) {
  if (java_vm == nullptr) return;
  JNIEnv *env = nullptr;
  // the UI thread lives as long as the process, so it stays attached
  if (java_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK &&
      java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
    return;
  }
  std::lock_guard<std::mutex> g(activity_lock);
  if (activity == nullptr) return;
  jclass cls = env->GetObjectClass(activity);
  jmethodID m = env->GetMethodID(cls, method, sig);
  if (m != nullptr) {
    va_list args;
    va_start(args, sig);
    env->CallVoidMethodV(activity, m, args);
    va_end(args);
  }
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteLocalRef(cls);
}

// set by the app before the UI starts
static std::atomic<int> menu_height_px{0};

int android_menu_height() {
  return menu_height_px.load();
}

void android_place_menu(int width, int height) {
  call_activity("placeMenuFromNative", "(II)V", static_cast<jint>(width), static_cast<jint>(height));
}

void android_restart_app() {
  call_activity("restartFromNative", "()V");
}

LV_FONT_DECLARE(dejavusans_mono_14);
LV_FONT_DECLARE(console_mono_16);
LV_FONT_DECLARE(console_mono_18);
LV_FONT_DECLARE(console_mono_20);
LV_FONT_DECLARE(console_mono_22);
LV_FONT_DECLARE(console_mono_24);
LV_FONT_DECLARE(console_mono_26);
LV_FONT_DECLARE(console_mono_28);

// As Theme::scale_font picks Montserrat: the first size at least the 14px the
// log has at 800x480, scaled, up to the largest there is.
const lv_font_t *android_console_font() {
  static const struct { int size; const lv_font_t *font; } fonts[] = {
    {16, &console_mono_16}, {18, &console_mono_18}, {20, &console_mono_20},
    {22, &console_mono_22}, {24, &console_mono_24}, {26, &console_mono_26},
    {28, &console_mono_28},
  };
  const int target = Theme::scale_r(14);
  if (target <= 14) return &dejavusans_mono_14;
  for (const auto &f : fonts) {
    if (f.size >= target) return f.font;
  }
  return fonts[sizeof(fonts) / sizeof(fonts[0]) - 1].font;
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  java_vm = vm;
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_io_github_pellcorp_grumpyscreen_GrumpyNative_setActivity(JNIEnv *env, jclass, jobject a) {
  std::lock_guard<std::mutex> g(activity_lock);
  if (activity != nullptr) env->DeleteGlobalRef(activity);
  activity = a != nullptr ? env->NewGlobalRef(a) : nullptr;
}

JNIEXPORT void JNICALL
Java_io_github_pellcorp_grumpyscreen_GrumpyNative_surfaceChanged(
    JNIEnv *env, jclass, jobject surface, jint width, jint height,
    jstring config, jstring moonraker, jstring override_cfg) {
  {
    std::lock_guard<std::mutex> g(window_lock);
    if (window != nullptr) ANativeWindow_release(window);
    window = ANativeWindow_fromSurface(env, surface);
    surface_w = width;
    surface_h = height;

    if (!started) {
      // native resolution: any scaling by the compositor visibly blurs text
      disp_w = width;
      disp_h = height;
      frame.assign(static_cast<size_t>(disp_w) * disp_h, 0xFF000000u);
    }
    if (window != nullptr) {
      ANativeWindow_setBuffersGeometry(window, disp_w, disp_h, WINDOW_FORMAT_RGBX_8888);
      hint_frame_rate_locked();
      present_locked();  // whatever was last drawn, until LVGL redraws
    }
  }

  if (!started.exchange(true)) {
    Paths p{jstr(env, config), jstr(env, moonraker), jstr(env, override_cfg)};
    std::thread(ui_thread, p).detach();
  } else if (ready) {
    std::lock_guard<std::mutex> g(GuppyScreen::get()->get_lock());
    lv_obj_invalidate(lv_scr_act());
  }
}

JNIEXPORT void JNICALL
Java_io_github_pellcorp_grumpyscreen_GrumpyNative_setMenuHeight(JNIEnv *, jclass, jint px) {
  menu_height_px = px;
}

JNIEXPORT void JNICALL
Java_io_github_pellcorp_grumpyscreen_GrumpyNative_setRefreshRate(JNIEnv *, jclass, jfloat hz) {
  if (!(hz >= 1.0f && hz <= 1000.0f)) hz = 60.0f;
  // round down: an occasional extra render is cheaper than skipping vsyncs,
  // and posting a buffer waits for a free one anyway
  const int ms = LV_MAX(1, static_cast<int>(1000.0f / hz));
  refresh_hz = hz;
  frame_ms = ms;
  LOG_INFO("display refresh {:.1f}Hz, LVGL frame period {}ms", hz, ms);
  {
    std::lock_guard<std::mutex> g(window_lock);
    hint_frame_rate_locked();
  }
  if (ready) {
    std::lock_guard<std::mutex> g(GuppyScreen::get()->get_lock());
    apply_frame_period();
  }
}

JNIEXPORT void JNICALL
Java_io_github_pellcorp_grumpyscreen_GrumpyNative_surfaceDestroyed(JNIEnv *, jclass) {
  std::lock_guard<std::mutex> g(window_lock);
  if (window != nullptr) {
    ANativeWindow_release(window);
    window = nullptr;
  }
}

JNIEXPORT void JNICALL
Java_io_github_pellcorp_grumpyscreen_GrumpyNative_touch(JNIEnv *, jclass, jint x, jint y,
                                                        jboolean down) {
  int sw, sh;
  {
    std::lock_guard<std::mutex> g(window_lock);
    sw = surface_w;
    sh = surface_h;
  }
  // map view pixels onto the LVGL display when the two differ in size
  if (sw > 0 && sh > 0 && (sw != disp_w || sh != disp_h)) {
    x = x * disp_w / sw;
    y = y * disp_h / sh;
  }
  touch_x = LV_CLAMP(0, x, disp_w - 1);
  touch_y = LV_CLAMP(0, y, disp_h - 1);
  if (down) press_pending = true;
  touched = down;
}

}  // extern "C"
