#ifndef __NUMPAD_H__
#define __NUMPAD_H__

#include "lvgl/lvgl.h"
#include <functional>

class Numpad {
 public:
  Numpad(lv_obj_t *parent);
  ~Numpad();

  void set_callback(std::function<void(double)> cb);
  void handle_input(lv_event_t *event);
  void handle_kb_input(lv_event_t *event);
  /* void handle_defocused(lv_event_t *event); */
  void foreground_reset();
  // cover the parent from x (parent-content coordinates) to its right edge
  void cover_from(lv_coord_t x);
  // mark obj checked while the numpad is open for it; NULL clears
  void set_highlight_target(lv_obj_t *obj);
  // forget obj if it is the current target, before it is deleted
  void release_if_target(lv_obj_t *obj);

  static void _handle_input(lv_event_t *event) {
    Numpad *panel = (Numpad*)event->user_data;
    panel->handle_input(event);
  };

  static void _handle_kb_input(lv_event_t *event) {
    Numpad *panel = (Numpad*)event->user_data;
    panel->handle_kb_input(event);
  };

  /* static void _handle_defocused(lv_event_t *event) { */
  /*   Numpad *panel = (Numpad*)event->user_data; */
  /*   panel->handle_defocused(event); */
  /* }; */

 private:
  lv_obj_t *edit_cont;
  lv_obj_t *input;
  lv_obj_t *kb;
  std::function<void(double)> ready_cb;
  bool prev_was_empty;
  lv_obj_t *highlight_target;
};

#endif // __NUMPAD_H__
