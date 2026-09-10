#include "selector.h"
#include "theme.h"

using namespace Theme;
#include "logger.h"

#include <algorithm>
#include <limits>

namespace {
// the height a row of keys has always had: a share of the screen, floored so a
// short screen still gets a finger-sized key
int key_row_h() { return std::max(lv_disp_get_physical_ver_res(NULL) * 15 / 100, 50); }
}  // namespace

Selector::Selector(lv_obj_t *parent,
		   const char *label_text,
		   std::vector<const char*> m,
		   uint32_t default_idx,
		   int32_t width_pct,
		   int32_t height_pct,
		   lv_event_cb_t cb,
		   void *cb_data)
  : cont(lv_obj_create(parent))
  , label(lv_label_create(cont))
  , btnm(lv_btnmatrix_create(cont))
  , map(m)
  , selector_idx(default_idx)
{
  lv_obj_set_size(cont, LV_PCT(width_pct), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(cont, &styles().panel, 0);
  lv_obj_set_style_pad_row(cont, gap() / 2, 0);

  lv_label_set_text(label, label_text);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

  // keys sized by the caller as a share of the screen, or, with no share given,
  // filling whatever cell the parent stretches the selector into -- but never
  // taller than a row of keys has ever been, or they stretch into tall
  // rectangles instead of the roughly square buttons this control is made of
  lv_obj_set_width(btnm, LV_PCT(100));
  if (height_pct > 0) {
    lv_obj_set_height(btnm, scale_h(272 * height_pct / 100));
  } else {
    lv_obj_set_flex_grow(btnm, 1);
  }
  lv_obj_set_style_min_height(btnm, key_row_h(), 0);
  lv_obj_set_style_max_height(btnm, key_row_h(), 0);

  // The tray holds the keys off the edge of the group in both looks -- it is
  // what makes a key the size it is, so it is always added. Whether it is also
  // the visible box is the theme's business, not this widget's.
  lv_obj_add_style(btnm, &styles().key_tray, 0);
  lv_btnmatrix_set_map(btnm, &map[0]);
  // the key look (checked = the chosen value) comes from the theme callback
  lv_obj_add_event_cb(btnm, cb, LV_EVENT_VALUE_CHANGED, cb_data);

  // select one only
  lv_btnmatrix_set_btn_ctrl_all(btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(btnm, true);
  if (selector_idx != std::numeric_limits<uint32_t>::max()) {
    lv_btnmatrix_set_btn_ctrl(btnm, selector_idx, LV_BTNMATRIX_CTRL_CHECKED);
  }
}

Selector::Selector(lv_obj_t *parent,
		   const char *label_text,
		   std::vector<const char*> m,
		   uint32_t default_idx,
		   lv_event_cb_t cb,
		   void *cb_data)
  : Selector(parent, label_text, m, default_idx, 100, 0, cb, cb_data)
{
}

Selector::~Selector() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *Selector::get_container() {
  return cont;
}

void Selector::seat_at_row_bottom() {
  lv_obj_set_style_pad_bottom(cont, 0, 0);
  lv_obj_set_style_pad_top(cont, gap(), 0);
}

lv_obj_t *Selector::get_selector() {
  return btnm;
}

lv_obj_t *Selector::get_label() {
  return label;
}

uint32_t Selector::get_selected_idx() {
  return selector_idx;
}

void Selector::set_selected_idx(uint32_t idx) {
  selector_idx = idx;
}
