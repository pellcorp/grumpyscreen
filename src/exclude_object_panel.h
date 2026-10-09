#ifndef __EXCLUDE_OBJECT_PANEL_H__
#define __EXCLUDE_OBJECT_PANEL_H__

#include "button_container.h"
#include "notify_consumer.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <string>
#include <vector>

class ExcludeObjectPanel : public NotifyConsumer {
 public:
  ExcludeObjectPanel(KWebSocketClient &ws, std::mutex &l);
  ~ExcludeObjectPanel();

  void foreground();
  void consume(json &j);
  void handle_callback(lv_event_t *e);
  void handle_canvas_event(lv_event_t *e);
  void handle_zoom(lv_event_t *e);
  void handle_dialog_result(uint32_t button_idx);

  static void _handle_callback(lv_event_t *e) {
    static_cast<ExcludeObjectPanel *>(e->user_data)->handle_callback(e);
  }

  static void _handle_canvas_event(lv_event_t *e) {
    static_cast<ExcludeObjectPanel *>(e->user_data)->handle_canvas_event(e);
  }

  static void _handle_zoom(lv_event_t *e) {
    static_cast<ExcludeObjectPanel *>(e->user_data)->handle_zoom(e);
  }

 private:
  struct ObjBox {
    std::string name;
    int number;
    lv_coord_t x0, y0, x1, y1;
    lv_coord_t cx, cy, radius;
    bool excluded;
    std::vector<lv_point_t> polygon;
    double mx, my;  // the centre on the bed, in mm
  };

  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  lv_obj_t *canvas;
  lv_coord_t canvas_dim;
  lv_color_t *canvas_buf;
  lv_obj_t *info_cont;
  lv_obj_t *status_label;
  lv_obj_t *zoom_row;
  lv_obj_t *zoom_out_btn;
  lv_obj_t *zoom_in_btn;
  ButtonContainer back_btn;

  bool is_foreground = false;
  std::string pending_name;
  std::string selected_name;
  lv_obj_t *confirm_mbox = nullptr;

  double bed_min_x = 0.0;
  double bed_max_x = 220.0;
  double bed_min_y = 0.0;
  double bed_max_y = 220.0;

  // the view: a zoom factor and the bed point, in mm, at the canvas centre
  double zoom = 1.0;
  double view_x = 110.0;
  double view_y = 110.0;

  // the current touch on the canvas
  bool press_on_canvas = false;
  bool dragged = false;
  bool long_pressed = false;
  lv_point_t press_point = {0, 0};
  double press_view_x = 0.0;
  double press_view_y = 0.0;

  std::vector<ObjBox> obj_boxes;

  void load_bed_bounds();
  void reset_view();
  void clamp_view();
  void set_zoom(double z);
  void redraw();
  double px_per_mm();
  lv_point_t to_px(double mx, double my);
  const ObjBox *hit_test(const lv_point_t &point);
  void confirm_exclude(const ObjBox &obj);
  void do_exclude();
};

#endif // __EXCLUDE_OBJECT_PANEL_H__
