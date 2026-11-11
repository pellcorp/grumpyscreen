#include "back_navigation.h"

#ifdef OS_ANDROID
namespace {

// LVGL owns the marker, so it disappears when the button is deleted.
void back_button_marker(lv_event_t *) {}

// Hidden tabs and panels moved behind another screen still have live buttons.
// Use the same hit test as touch input to exclude actions covered by that UI.
bool reachable(lv_obj_t *obj) {
  for (auto *parent = obj; parent != nullptr; parent = lv_obj_get_parent(parent)) {
    if (lv_obj_has_state(parent, LV_STATE_DISABLED)) return false;
  }
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  if (!lv_obj_area_is_visible(obj, &area)) return false;
  lv_point_t point{static_cast<lv_coord_t>((area.x1 + area.x2) / 2),
                   static_cast<lv_coord_t>((area.y1 + area.y2) / 2)};
  lv_disp_t *display = lv_obj_get_disp(obj);
  lv_obj_t *hit = lv_indev_search_obj(lv_disp_get_layer_sys(display), &point);
  if (hit == nullptr) hit = lv_indev_search_obj(lv_disp_get_layer_top(display), &point);
  if (hit == nullptr) hit = lv_indev_search_obj(lv_disp_get_scr_act(display), &point);
  for (; hit != nullptr; hit = lv_obj_get_parent(hit)) {
    if (hit == obj) return true;
  }
  return false;
}

bool handle_tree(lv_obj_t *obj) {
  if (!lv_obj_is_visible(obj)) return false;
  // The last child is drawn in front.
  for (int32_t i = static_cast<int32_t>(lv_obj_get_child_cnt(obj)) - 1; i >= 0; --i) {
    if (handle_tree(lv_obj_get_child(obj, i))) return true;
  }
  if (lv_obj_get_event_user_data(obj, back_button_marker) == nullptr || !reachable(obj)) return false;
  lv_event_send(obj, LV_EVENT_CLICKED, nullptr);
  return true;
}

}  // namespace

void BackNavigation::register_button(lv_obj_t *obj) {
  if (lv_obj_get_event_user_data(obj, back_button_marker) == nullptr) {
    lv_obj_add_event_cb(obj, back_button_marker, LV_EVENT_DELETE, obj);
  }
}

bool BackNavigation::handle() {
  lv_disp_t *display = lv_disp_get_default();
  if (display == nullptr) return false;
  lv_obj_update_layout(lv_disp_get_scr_act(display));
  lv_obj_update_layout(lv_disp_get_layer_top(display));
  lv_obj_update_layout(lv_disp_get_layer_sys(display));
  return handle_tree(lv_disp_get_scr_act(display));
}
#endif
