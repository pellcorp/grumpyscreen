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
  void handle_canvas_click(lv_event_t *e);
  void handle_dialog_result(uint32_t button_idx);

  static void _handle_callback(lv_event_t *e) {
    static_cast<ExcludeObjectPanel *>(e->user_data)->handle_callback(e);
  }

  static void _handle_canvas_click(lv_event_t *e) {
    static_cast<ExcludeObjectPanel *>(e->user_data)->handle_canvas_click(e);
  }

 private:
  struct ObjBox {
    std::string name;
    int number;
    lv_coord_t x0, y0, x1, y1;
    lv_coord_t cx, cy, radius;
    bool excluded;
    std::vector<lv_point_t> polygon;
  };

  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  lv_obj_t *canvas;
  lv_coord_t canvas_dim;
  lv_color_t *canvas_buf;
  lv_obj_t *info_cont;
  lv_obj_t *status_label;
  ButtonContainer back_btn;

  bool is_foreground = false;
  std::string pending_name;
  lv_obj_t *confirm_mbox = nullptr;

  double bed_min_x = 0.0;
  double bed_max_x = 220.0;
  double bed_min_y = 0.0;
  double bed_max_y = 220.0;

  std::vector<ObjBox> obj_boxes;

  void load_bed_bounds();
  void redraw();
  lv_point_t to_px(double mx, double my);
  void confirm_exclude(const ObjBox &obj);
  void do_exclude();
};

#endif // __EXCLUDE_OBJECT_PANEL_H__
