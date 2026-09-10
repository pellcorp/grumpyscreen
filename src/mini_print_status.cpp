#include "mini_print_status.h"
#include "logger.h"
#include "theme.h"

using namespace Theme;

MiniPrintStatus::MiniPrintStatus(lv_obj_t *parent,
				 lv_event_cb_t cb,
				 void* user_data)
  : cont(lv_obj_create(parent))
  , progress_bar(lv_arc_create(cont))
  , thumb(lv_img_create(cont))
  , status_label(lv_label_create(cont))
  , status("n/a")
  , eta("...")
{
  // progress arc | thumbnail | two-line status, in the lifted grey box it has
  // always had: a hair lighter than the page, outlined, rounded
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_style(cont, &styles().row_card, 0);
  lv_obj_set_style_bg_color(cont, col(RAISED), 0);
  lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_radius(cont, radius_md(), 0);
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(cont, gap(), 0);

  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, user_data);

  lv_label_set_text(status_label, fmt::format("ETA: {}\nStatus: {}", eta, status).c_str());
  // the label takes the slack; dots rather than a third line if it ever overflows
  lv_obj_set_flex_grow(status_label, 1);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);

  lv_arc_set_rotation(progress_bar, 270);
  lv_obj_set_size(progress_bar, scale_r(24), scale_r(24));
  lv_obj_set_style_arc_width(progress_bar, scale_r(6), LV_PART_MAIN);
  lv_obj_set_style_arc_width(progress_bar, scale_r(6), LV_PART_INDICATOR);
  lv_arc_set_bg_angles(progress_bar, 0, 360);
  lv_obj_remove_style(progress_bar, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);

  lv_img_set_size_mode(thumb, LV_IMG_SIZE_MODE_REAL);
}

MiniPrintStatus::~MiniPrintStatus() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}


void MiniPrintStatus::show() {
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
}

void MiniPrintStatus::hide() {
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *MiniPrintStatus::get_container() {
  return cont;
}

void MiniPrintStatus::update_eta(std::string &eta_str) {
  eta = eta_str;
  lv_label_set_text(status_label, fmt::format("ETA: {}\nStatus: {}", eta, status).c_str());
}

void MiniPrintStatus::update_status(std::string &status_str) {
  status = status_str;
  lv_label_set_text(status_label, fmt::format("ETA: {}\nStatus: {}", eta, status).c_str());
}

void MiniPrintStatus::update_progress(int p) {
  lv_arc_set_value(progress_bar, p);
}

void MiniPrintStatus::update_img(const std::string &img_path, size_t twidth) {
  // as tall as the two-line label beside it, so the row stays two lines high
  lv_obj_update_layout(status_label);
  const lv_coord_t h = lv_obj_get_height(status_label);
  lv_img_set_src(thumb, img_path.c_str());
  fit_img(thumb, h, h, 2 * LV_IMG_ZOOM_NONE);
}

void MiniPrintStatus::reset() {
  lv_arc_set_value(progress_bar, 0);

  // free src
  lv_img_set_src(thumb, NULL);
  // hack to color in empty space.
  ((lv_img_t*)thumb)->src_type = LV_IMG_SRC_SYMBOL;

  eta = "...";
  status = "n/a";
}
