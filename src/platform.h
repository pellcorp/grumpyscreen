#ifndef __GUPPY_PLATFORM_H__
#define __GUPPY_PLATFORM_H__

// libhv's hplatform.h defines OS_ANDROID as well; keep the two compatible.
#if (defined(ANDROID) || defined(__ANDROID__)) && !defined(OS_ANDROID)
  #define OS_ANDROID
#endif

#ifdef OS_ANDROID
// The size to draw the nav bar's icons (nav_*) at, before any is drawn
// (android/app/src/main/cpp/grumpy_icons.c).
extern "C" void android_set_nav_icon_px(int px);
// The console log's monospace font at the size the UI's text has grown to
// (android/app/src/main/cpp/fonts, from android/tools/console-fonts.py).
struct _lv_font_t;
const struct _lv_font_t *android_console_font();
// The UI's text, Montserrat, at exactly px pixels: past the 28px the built-in
// fonts stop at (android/app/src/main/cpp/grumpy_fonts.cpp).
const struct _lv_font_t *android_text_font(int px);
// The app's printer menu (its ☰) takes a touch target's height at the top of
// the nav strip: how tall that is, and where the strip has left it, so the app
// can put its button there.
int android_menu_height();
void android_place_menu(int width, int height);
void android_set_menu_visible(bool visible);
#endif

#endif // __GUPPY_PLATFORM_H__
