#ifndef __K_SELECTOR_H__
#define __K_SELECTOR_H__

#include "lvgl/lvgl.h"
#include <string>
#include <vector>

class Selector {
 public:
  Selector(lv_obj_t *parent,
	   const char *label_text,
	   std::vector<const char*> map,
	   uint32_t default_idx,
	   int32_t width_pct,
	   int32_t height_pct,
	   lv_event_cb_t cb,
	   void *cb_data);

  // no size of its own: fills the cell the parent stretches it into
  Selector(lv_obj_t *parent,
	   const char *label_text,
	   std::vector<const char*> map,
	   uint32_t default_idx,
	   lv_event_cb_t cb,
	   void *cb_data);
  
  ~Selector();
  lv_obj_t *get_container();
  lv_obj_t *get_selector();
  // Seat the selector on the bottom edge of a screen: it gives up the padding
  // under its keys, so they sit on the screen's own margin, and keeps a gap
  // above its caption, which would otherwise crowd the panel's top border in
  // the look that draws one. For a selector placed LV_GRID_ALIGN_END.
  void seat_at_row_bottom();
  lv_obj_t *get_label();
  uint32_t get_selected_idx();
  void set_selected_idx(uint32_t idx);

 private:
  lv_obj_t *cont;
  lv_obj_t *label;
  lv_obj_t *btnm;
  std::vector<const char*> map;
  uint32_t selector_idx;
};

#endif //  __K_SELECTOR_H__
