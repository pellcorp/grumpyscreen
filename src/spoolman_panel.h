#ifndef __SPOOLMAN_PANEL_H__
#define __SPOOLMAN_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

class MmuPanel;

// just what the tab shows and acts on; spoolman's full record is dropped once
// these are read from it
struct SpoolRec {
  uint32_t id = 0;
  std::string name;      // "Vendor - Filament"
  std::string vendor;
  std::string material;
  std::string colour;    // spoolman's color_hex, "" when unset
  double weight = 0;     // grams remaining
  double length = 0;     // metres remaining
  bool archived = false;
};

class SpoolmanPanel {
 public:
  SpoolmanPanel(KWebSocketClient &c, std::mutex &l);
  ~SpoolmanPanel();

  // fetch the spools and the active spool. Only after create()
  void init();
  // build the panel on its tab page, the first time spoolman appears; until
  // then a printer without spoolman holds none of it. Caller holds lv_lock
  void create(lv_obj_t *parent);
  // the MMU whose lanes a held row can be assigned to; NULL for none
  void set_mmu(MmuPanel *m) { mmu = m; }
  void populate_spools(std::vector<SpoolRec> &sorted_spools);
  void handle_active_id_update(json &j);
  
  void handle_callback(lv_event_t *event);
  void handle_spoolman_action(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SpoolmanPanel *panel = (SpoolmanPanel*)event->user_data;
    panel->handle_callback(event);
  };
  
  static void _handle_spoolman_action(lv_event_t *event) {
    SpoolmanPanel *panel = (SpoolmanPanel*)event->user_data;
    panel->handle_spoolman_action(event);
  };

 private:
  // sort by id and redraw. caller must hold lv_lock
  void repopulate();
  // share the table's width across the columns: the flexible ones take what
  // the fixed ones leave, in whole pixels that reach the right edge exactly
  void layout_columns();
  // a held spool row: ask which lane it goes in. caller holds lv_lock
  void show_assign(int spool_id, const std::string &name);
  static void _assign_result(lv_obj_t *mbox, uint32_t idx, void *user_data);
  static void _clear_result(lv_obj_t *mbox, uint32_t idx, void *user_data);

  KWebSocketClient &ws;
  std::mutex &lv_lock;
  // all NULL until create(); the tab page owns them
  lv_obj_t *cont = NULL;
  lv_obj_t *table_box = NULL;  // the table and its side scrollbar
  lv_obj_t *spool_table = NULL;
  lv_obj_t *empty_box = NULL;  // "No spools", shown in the table's place
  lv_obj_t *controls = NULL;
  lv_obj_t *switch_cont = NULL;
  lv_obj_t *show_archived = NULL;
  std::unique_ptr<ButtonContainer> reload_btn;
  // archived spools are only fetched while "Show Archived" is on
  bool want_archived = false;
  int32_t active_id;
  std::map<uint32_t, SpoolRec> spools;
  uint32_t sorted_by;
  MmuPanel *mmu = NULL;
  // a long press ends in a release, which the table also reports as a tap
  bool long_pressed = false;
  lv_point_t press_point = {0, 0};  // where the press began, to tell a hold from a drag
  int assign_spool_id = -1;
  std::vector<std::string> lane_labels;  // the dialog's buttons, while it is open
  std::vector<const char *> lane_btns;
  std::vector<const char *> clear_btns;  // the same lanes, for "clear which lane"
};

#endif // __SPOOLMAN_PANEL_H__
