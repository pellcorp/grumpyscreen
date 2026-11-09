#include "notification_banner.h"
#include "theme.h"
#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" uint32_t custom_tick_get() { return 0; }
namespace Theme {
int scale_r(int px) { return px; }
lv_color_t col(Colour colour) { return lv_color_hex(0x101010 * (int(colour) + 1)); }
}

int main() {
  lv_init();
  static lv_color_t pixels[800];
  lv_disp_draw_buf_t buffer;
  lv_disp_draw_buf_init(&buffer, pixels, nullptr, 800);
  lv_disp_drv_t driver;
  lv_disp_drv_init(&driver);
#ifdef GUPPY_SMALL_SCREEN
  driver.hor_res = 480;
  driver.ver_res = 272;
#else
  driver.hor_res = 800;
  driver.ver_res = 480;
#endif
  driver.draw_buf = &buffer;
  lv_disp_drv_register(&driver);

  using Source = NotificationBanner::Source;
  using Level = NotificationBanner::Level;
  auto &banner = NotificationBanner::instance();
  int acknowledgements = 0;
  banner.show(Source::Display, "Ready", Level::Info);
  lv_obj_t *overlay = lv_obj_get_child(lv_layer_top(), 0);
  lv_obj_t *text = lv_obj_get_child(overlay, 0);
  lv_obj_t *close = lv_obj_get_child(overlay, 1);
  assert(lv_obj_get_child_cnt(overlay) == 2); // message and close button only
  lv_obj_update_layout(overlay);
  const auto line_height = lv_font_get_line_height(lv_obj_get_style_text_font(text, LV_PART_MAIN));
  assert(lv_obj_get_height(overlay) == line_height + 16);
  assert(lv_obj_get_x(close) > lv_obj_get_x(text) + lv_obj_get_width(text));

  banner.show(Source::Mmu, "Filament jam", Level::Error,
              [&]() { ++acknowledgements; });
  assert(lv_obj_get_child_cnt(lv_layer_top()) == 1);
  assert(std::strcmp(lv_label_get_text(text), "Filament jam") == 0);
  assert(lv_obj_get_child_cnt(overlay) == 2);
  assert(acknowledgements == 0);
  lv_obj_update_layout(overlay);
  assert(lv_obj_get_height(overlay) == line_height + 16);

  // A later Klipper error takes the overlay; dismissal restores the MMU fault.
  banner.show(Source::Klipper, "Move out of range", Level::Error);
  assert(std::strcmp(lv_label_get_text(text), "Move out of range") == 0);
  lv_event_send(close, LV_EVENT_CLICKED, nullptr);
  assert(std::strcmp(lv_label_get_text(text), "Filament jam") == 0);
  lv_event_send(close, LV_EVENT_CLICKED, nullptr);
  assert(acknowledgements == 1);
  assert(std::strcmp(lv_label_get_text(text), "Ready") == 0);
  banner.show(Source::Mmu, "Filament jam", Level::Error);
  assert(!banner.visible(Source::Mmu)); // status updates don't reopen a fault
  banner.clear(Source::Mmu);
  banner.show(Source::Mmu, "Filament jam", Level::Error);
  assert(banner.visible(Source::Mmu)); // a cleared fault can recur

  // Information never replaces a fault, and long messages wrap beside X.
  banner.show(Source::Display, "Heating", Level::Info);
  assert(std::strcmp(lv_label_get_text(text), "Filament jam") == 0);
  banner.show(Source::Mmu,
              "A long MMU message that needs several lines to describe a filament loading failure. "
              "Check that the filament is free to move before retrying the load.", Level::Error);
  lv_obj_update_layout(overlay);
  assert(lv_obj_get_height(overlay) > line_height + 16);
  assert(lv_obj_get_x(close) >= lv_obj_get_x(text) + lv_obj_get_width(text));
  banner.clear(Source::Mmu);
  banner.clear(Source::Klipper);
  banner.clear(Source::Display);
  assert(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
  std::puts("notification banner tests passed");
}
