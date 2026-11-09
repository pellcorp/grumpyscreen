#ifndef __GUPPY_PLATFORM_H__
#define __GUPPY_PLATFORM_H__

// libhv's hplatform.h defines OS_ANDROID as well; keep the two compatible.
#if (defined(ANDROID) || defined(__ANDROID__)) && !defined(OS_ANDROID)
  #define OS_ANDROID
#endif

#ifdef OS_ANDROID
// Opens the app's printer list (android/app/src/main/cpp/grumpy_android.cpp).
void android_show_printer_picker();
// Restarts the app: the Android "Restart GUI".
void android_restart_app();
// Turns the backlight off (true) or back on, for the screen timeout.
void android_set_display_sleep(bool sleeping);
// The size to draw the nav bar's icons (nav_*) at, before any is drawn
// (android/app/src/main/cpp/grumpy_icons.c).
extern "C" void android_set_nav_icon_px(int px);
// The console log's monospace font at the size the UI's text has grown to
// (android/app/src/main/cpp/fonts, from android/tools/console-fonts.py).
struct _lv_font_t;
const struct _lv_font_t *android_console_font();
#endif

#endif // __GUPPY_PLATFORM_H__
