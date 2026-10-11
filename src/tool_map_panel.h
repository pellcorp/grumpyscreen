#ifndef __TOOL_MAP_PANEL_H__
#define __TOOL_MAP_PANEL_H__

#include "mmu_backend.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Print-start tool mapping: a table with one row per tool the file uses, the
// file's filament and the printer slot feeding it side by side; tapping a row
// picks another slot. It reads MmuSlot::map and calls MmuBackend::map_tool(),
// so it knows no vendor; a backend without a tool map never gets this screen.
//
// Laid out as the spoolman panel is: a table with a tinted header and zebra
// rows, and icon tiles bottom right.
//
// Picks are held and shown until Print sends them, so Back leaves the printer
// as it was. Automatic matching runs only when the user asks for it.
class ToolMapPanel {
 public:
  ToolMapPanel();
  ~ToolMapPanel();

  // caller holds lv_lock. print() starts the job; the screen closes first.
  void open(MmuBackend *backend, const std::vector<MmuFileTool> &tools,
            const std::string &filename, std::function<void()> print);
  void close();
  bool is_open() const;
  // the backend's slots changed; caller holds lv_lock
  void update();

  static void _handle_table(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->handle_table(e); }
  static void _handle_pick(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->pick(e); }
  static void _handle_close_picker(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->close_picker(); }
  static void _handle_action(lv_event_t *e) { ((ToolMapPanel*)e->user_data)->handle_action(e); }

 private:
  // what a row's first cell says, and what its circles are painted
  enum class Status { Ok, Warn, Blocked };
  struct RowPaint {
    Status status = Status::Ok;
    std::string file_colour;
    std::string slot_colour;
    bool slot_filled = false;  // the slot holds filament: its circle is drawn solid
    bool unconfigured = false; // no slot, or no material set on it: "Empty" in red
    bool other_material = false;  // both materials shown in the warning colour
  };

  void create();
  void layout_columns();
  void handle_table(lv_event_t *e);
  void show_picker(size_t t);  // index into tools
  void pick(lv_event_t *e);
  void close_picker();
  void handle_action(lv_event_t *e);
  void assign(size_t t, int slot);  // tools[t] from slot, the screen's rule
  void send_picks();
  void auto_match();

  MmuBackend *backend = NULL;
  std::vector<MmuFileTool> tools;
  std::vector<RowPaint> paint;   // indexed like tools: table row - 1
  std::function<void()> print;
  std::vector<int> want;   // slot picked for each of tools, -1 none; held until Print
  int picking_tool = -1;  // tool whose picker is open, -1 = none

  lv_obj_t *cont = NULL;
  lv_obj_t *table = NULL;
  lv_obj_t *title = NULL;
  lv_obj_t *auto_btn = NULL;
  std::unique_ptr<ButtonContainer> print_btn;
  std::unique_ptr<ButtonContainer> back_btn;

  lv_obj_t *picker = NULL;       // dim overlay
  lv_obj_t *picker_box = NULL;
};

#endif // __TOOL_MAP_PANEL_H__
