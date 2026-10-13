#ifndef __TOOL_MAP_PANEL_H__
#define __TOOL_MAP_PANEL_H__

#include "mmu_backend.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Print-start tool mapping: one table row per tool the file uses and the slot
// feeding it. Tapping a row picks another slot. Picks are held until Print,
// which sends them through MmuBackend::map_tool(); Back sends nothing.
class ToolMapPanel {
 public:
  ~ToolMapPanel();

  // caller holds lv_lock; print() starts the job after the screen closes
  void open(MmuBackend *backend, const std::vector<MmuFileTool> &tools,
            const std::string &filename, std::function<void()> print);
  void close();
  bool is_open() const;
  void update();  // backend state changed; caller holds lv_lock

  static void _handle_table(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->handle_table(e); }
  static void _handle_pick(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->pick(e); }
  static void _handle_close_picker(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->close_picker(); }
  static void _handle_action(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->handle_action(e); }

 private:
  enum class Status { Ok, Warn, Blocked };
  struct RowPaint {
    Status status = Status::Ok;
    std::string file_colour, slot_colour;
    bool slot_filled = false;
    bool unconfigured = false;    // no slot, or no material on it
    bool other_material = false;
  };

  void create();
  void handle_table(lv_event_t *e);
  void show_picker(size_t t);
  void pick(lv_event_t *e);
  void close_picker();
  void handle_action(lv_event_t *e);
  void assign(size_t t, int slot);

  MmuBackend *backend = NULL;
  std::vector<MmuFileTool> tools;
  std::vector<int> want;         // picked slot per tool, -1 none
  std::vector<RowPaint> paint;   // per tool, for the draw callback
  std::function<void()> print;
  int picking = -1;              // tool index whose picker is open

  lv_obj_t *cont = NULL;
  lv_obj_t *table = NULL;
  lv_obj_t *title = NULL;
  std::unique_ptr<ButtonContainer> back_btn;
  std::unique_ptr<ButtonContainer> print_btn;
  lv_obj_t *picker = NULL;
  lv_obj_t *picker_box = NULL;
};

#endif // __TOOL_MAP_PANEL_H__
