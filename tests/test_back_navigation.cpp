#include "back_navigation.h"
#include "button_container.h"
#include "simple_dialog.h"

#include <cassert>
#include <cstdio>
#include <memory>

extern "C" uint32_t custom_tick_get() { return 0; }

// Navigation uses real LVGL widgets and hit testing; these fixed metrics keep
// the test independent of printer configuration and Android's font renderer.
namespace Theme {
int scale_w(int px) { return px; }
int scale_r(int px) { return px; }
int gap() { return 6; }
int touch_h() { return 44; }
int popout_w() { return 700; }
int popout_max_h() { return 400; }
int popout_pad() { return 10; }
lv_color_t col(Colour) { return lv_color_black(); }
lv_color_t theme_primary() { return lv_color_black(); }
const lv_font_t *scale_font(int) { return LV_FONT_DEFAULT; }
Styles &styles() {
  static Styles s{};
  return s;
}
void style_icon(lv_obj_t *) {}
int fit_img(lv_obj_t *, int, int, int) { return LV_IMG_ZOOM_NONE; }
}

static lv_obj_t *panel(lv_obj_t *parent = nullptr) {
  lv_obj_t *obj = lv_obj_create(parent != nullptr ? parent : lv_scr_act());
  lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

static std::unique_ptr<ButtonContainer> back_button(lv_obj_t *parent, int &clicks) {
  auto button = std::make_unique<ButtonContainer>(parent, nullptr, "Back",
      [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, &clicks);
  button->use_as_back();
  lv_obj_set_size(button->get_container(), 100, 60);
  lv_obj_align(button->get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  return button;
}

int main() {
  assert(!BackNavigation::handle()); // native UI not initialized yet
  lv_init();
  static lv_color_t pixels[800];
  lv_disp_draw_buf_t buffer;
  lv_disp_draw_buf_init(&buffer, pixels, nullptr, 800);
  lv_disp_drv_t driver;
  lv_disp_drv_init(&driver);
  driver.hor_res = 800;
  driver.ver_res = 480;
  driver.draw_buf = &buffer;
  driver.flush_cb = [](lv_disp_drv_t *d, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(d); };
  lv_disp_drv_register(&driver);

  int first_clicks = 0, second_clicks = 0;
  lv_obj_t *first = panel();
  auto first_back = back_button(first, first_clicks);
  lv_obj_t *second = panel();
  auto second_back = back_button(second, second_clicks);
  assert(BackNavigation::handle());
  assert(first_clicks == 0 && second_clicks == 1);

  // Covered panels remain alive and are not marked hidden by the application.
  lv_obj_move_background(second);
  assert(BackNavigation::handle());
  assert(first_clicks == 1 && second_clicks == 1);
  lv_obj_t *home = panel(); // a foreground page without a Back action
  assert(!BackNavigation::handle());
  assert(first_clicks == 1 && second_clicks == 1);
  lv_obj_del(home);
  first_back->hide();
  assert(!BackNavigation::handle());
  first_back->show();
  first_back->disable();
  assert(!BackNavigation::handle());
  first_back->enable();

  // An unregistered command tile with a Back caption is never executed.
  int commands = 0;
  ButtonContainer command(first, nullptr, "Back", [](lv_event_t *e) {
    ++*static_cast<int *>(lv_event_get_user_data(e));
  }, &commands);
  lv_obj_set_size(command.get_container(), 100, 60);
  assert(BackNavigation::handle());
  assert(commands == 0 && first_clicks == 2);

  // A nested panel must have its own Back button; that button may delete it.
  lv_obj_t *editor = panel(first);
  assert(!BackNavigation::handle());
  auto editor_back = back_button(editor, first_clicks);
  lv_obj_add_event_cb(editor_back->get_container(), [](lv_event_t *e) {
    lv_obj_del(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
  }, LV_EVENT_CLICKED, editor);
  assert(BackNavigation::handle());
  assert(first_clicks == 3);
  assert(BackNavigation::handle());
  assert(first_clicks == 4);

  // Dialog buttons are not panel Back buttons, even when labelled "Back".
  int choice = -1;
  static const char *buttons[] = {"Back", "Delete", ""};
  SimpleDialogOptions opts;
  opts.buttons = buttons;
  opts.highlighted_button_idx = 1;
  opts.result_cb = [](lv_obj_t *, uint32_t idx, void *data) { *static_cast<int *>(data) = idx; };
  opts.user_data = &choice;
  lv_obj_t *dialog = create_configurable_dialog(lv_layer_top(), "Delete file", "Delete this file?", opts);
  assert(!BackNavigation::handle());
  assert(choice == -1 && first_clicks == 4);
  simple_dialog_close(dialog);
  lv_timer_handler(); // finish the dialog's asynchronous deletion

  // Inactive tabs can be clipped off screen without being marked hidden.
  lv_obj_t *tabs = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 36);
  lv_obj_set_size(tabs, LV_PCT(100), LV_PCT(100));
  lv_obj_t *tab_one = lv_tabview_add_tab(tabs, "First");
  lv_tabview_add_tab(tabs, "Second");
  auto tab_back = back_button(tab_one, first_clicks);
  lv_tabview_set_act(tabs, 1, LV_ANIM_OFF);
  assert(!BackNavigation::handle());
  lv_tabview_set_act(tabs, 0, LV_ANIM_OFF);
  assert(BackNavigation::handle());
  assert(first_clicks == 5);

  lv_obj_clean(lv_scr_act());
  assert(!BackNavigation::handle());
  std::puts("back navigation tests passed");
}
