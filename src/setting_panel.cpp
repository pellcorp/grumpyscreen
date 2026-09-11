#include "setting_panel.h"
#include "config.h"
#include "logger.h"
#include "subprocess.hpp"
#include "simple_dialog.h"
#include "icons.h"
#include "theme.h"

#include <experimental/filesystem>
#include <vector>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;

struct deferred_cmd_ctx {
    lv_obj_t * mbox;
    std::string cmd;
    const char * failure_title;
    const char * failure_message;
};

// Long enough for LVGL to render the dialog before the command blocks the UI thread.
static constexpr uint32_t DEFERRED_COMMAND_DELAY_MS = 500;
// The factory reset dialog is deliberately left up longer before we block.
static constexpr uint32_t FACTORY_RESET_DELAY_MS = 5000;

static int call_command(const std::string &cmd) {
    try {
        return sp::call(cmd);
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to execute command '{}': {}", cmd, e.what());
        return -1;
    }
}

static void run_deferred_command_cb(lv_timer_t * t) {
    deferred_cmd_ctx * ctx = (deferred_cmd_ctx *)t->user_data;

    int ret = call_command(ctx->cmd);

    if (ret != 0) {
        simple_dialog_close(ctx->mbox);
        create_simple_dialog(lv_scr_act(),
                             ctx->failure_title,
                             ctx->failure_message,
                             true,
                             true);
    }

    delete ctx;
    lv_timer_del(t);
}

// Commands such as update, switch to stock and factory reset block for a long
// time and then reboot the printer. Calling them straight from the click
// handler freezes the UI thread before LVGL gets a chance to draw, so the press
// appears to do nothing at all until the machine reboots. Put the dialog up
// first and defer the command to a one shot timer so the screen is rendered
// before we block on it.
static void run_command_deferred(lv_obj_t * mbox,
                                 const std::string &cmd,
                                 const char * failure_title,
                                 const char * failure_message,
                                 uint32_t delay_ms) {
    deferred_cmd_ctx * ctx = new deferred_cmd_ctx{ mbox, cmd, failure_title, failure_message };
    lv_timer_t * timer = lv_timer_create(run_deferred_command_cb, delay_ms, ctx);
    lv_timer_set_repeat_count(timer, 1);
}

SettingPanel::SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent)
  : ws(c)
  , cont(Theme::create_screen(parent))  // fills the tab: it is the page
  , wifi_panel(l)
#ifdef COSMOS
  , update_manager(c, l)
#endif
  , wifi_btn(cont, Icons::NETWORK_IMG, "WIFI", &SettingPanel::_handle_callback, this)
  , restart_klipper_btn(cont, Icons::REFRESH_IMG, "Restart\nKlipper", &SettingPanel::_handle_callback, this,
        "Restart Klipper", "Do you want to restart klipper?", {"Back", "Restart Klipper"})
  , restart_firmware_btn(cont, Icons::REFRESH_IMG, "Firmware\nRestart", &SettingPanel::_handle_callback, this,
        "Firmware Restart", "Do you want to perform a firmware restart?", {"Back", "Firmware Restart"})
  , guppy_restart_btn(cont, Icons::REFRESH_IMG, "Restart GUI", &SettingPanel::_handle_callback, this)
  , support_zip_btn(cont, Icons::SD_IMG, "Create\nSupport ZIP", &SettingPanel::_handle_callback, this)
  , switch_to_stock_btn(cont, Icons::EMERGENCY, SWITCH_TO_STOCK_BUTTON_TEXT, &SettingPanel::_handle_callback, this,
          SWITCH_TO_STOCK_BUTTON_TITLE, SWITCH_TO_STOCK_BUTTON_PROMPT, {"Back", "Switch to Stock"})
  , factory_reset_btn(cont, Icons::EMERGENCY, FACTORY_RESET_BUTTON_TEXT, &SettingPanel::_handle_callback, this,
		  FACTORY_RESET_BUTTON_TITLE, FACTORY_RESET_BUTTON_PROMPT, {"Back", "Factory Reset"})
#ifdef COSMOS
  , update_btn(cont, Icons::UPDATE_IMG, UPDATE_BUTTON_TEXT, &SettingPanel::_handle_callback, this,
          UPDATE_BUTTON_TITLE, UPDATE_BUTTON_PROMPT, {"Back", "Update"})
#else
  , shutdown_host_btn(cont, Icons::EMERGENCY, "Shutdown Host", &SettingPanel::_handle_callback, this,
          "Shutdown host?", "Do you want to shutdown the host?", {"Back", "Shutdown Host"})
#endif
{
  // the optional tiles only appear with a command behind them; the grid is
  // built from what is left, four across, so a hidden tile never leaves a hole
  Config *conf = Config::get_instance();
  auto has_cmd = [conf](const char *key) { return conf->get<std::string>(key) != ""; };
  std::vector<ButtonContainer *> tiles = {&wifi_btn, &restart_klipper_btn, &restart_firmware_btn, &guppy_restart_btn};
  struct Optional { ButtonContainer *tile; const char *cmd; };
  for (const Optional &o : {Optional{&support_zip_btn, "/commands/support_zip_cmd"},
                            Optional{&switch_to_stock_btn, "/commands/switch_to_stock_cmd"},
                            Optional{&factory_reset_btn, "/commands/factory_reset_cmd"},
#ifndef COSMOS
                            Optional{&shutdown_host_btn, "/commands/shutdown_host_cmd"},
#endif
                            }) {
    if (has_cmd(o.cmd)) tiles.push_back(o.tile); else o.tile->hide();
  }
#ifdef COSMOS
  tiles.push_back(&update_btn);
#endif

  static const size_t cols = 4;
  static lv_coord_t grid_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  if (tiles.size() <= cols) grid_row_dsc[1] = LV_GRID_TEMPLATE_LAST;  // at most eight tiles: one row or two
  lv_obj_set_grid_dsc_array(cont, grid_col_dsc, grid_row_dsc);

  for (size_t i = 0; i < tiles.size(); i++) {
    tiles[i]->use_card();
    lv_obj_set_grid_cell(tiles[i]->get_container(), LV_GRID_ALIGN_STRETCH, i % cols, 1,
                         LV_GRID_ALIGN_STRETCH, i / cols, 1);
  }
}

SettingPanel::~SettingPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *SettingPanel::get_container() {
  return cont;
}

void SettingPanel::handle_callback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
      lv_obj_t *btn = lv_event_get_current_target(event);
    if (btn == wifi_btn.get_container()) {
      wifi_panel.foreground();
    } else if (btn == restart_klipper_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto restart_command = conf->get<std::string>("/commands/restart_klipper_cmd");
      auto ret = call_command(restart_command);
      if (ret != 0) {
        create_simple_dialog(lv_scr_act(), "Restart Klipper Failed", "Failed to restart Klipper!", true, true);
      }
    } else if (btn == restart_firmware_btn.get_container()) {
      ws.send_jsonrpc("printer.firmware_restart");
    } else if (btn == guppy_restart_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto restart_command = conf->get<std::string>("/commands/gui_restart_cmd");
      auto ret = call_command(restart_command);
      if (ret != 0) {
        create_simple_dialog(lv_scr_act(), "Restart GUI Failed", "Failed to restart GUI!", true, true);
      }
#ifdef COSMOS
    } else if (btn == update_btn.get_container()) {
      // Moonraker runs the update and reports its progress to every UI.
      update_manager.start();
#else
    } else if (btn == shutdown_host_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto shutdown_host_cmd = conf->get<std::string>("/commands/shutdown_host_cmd");
      lv_obj_t *mbox = create_simple_dialog(lv_scr_act(), "Shutdown Host Initiated", "Shutdown of host has been initiated", false, false);
      run_command_deferred(mbox, shutdown_host_cmd,
                           "Shutdown Host Failed", "Failed to shutdown host!",
                           DEFERRED_COMMAND_DELAY_MS);
#endif
    } else if (btn == support_zip_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto support_zip_cmd = conf->get<std::string>("/commands/support_zip_cmd");
      if (support_zip_cmd != "") {
        auto ret = call_command(support_zip_cmd);
        if (ret == 0) {
          create_simple_dialog(lv_scr_act(), "Support ZIP Success", "The support.zip can be found in the config directory!", true, false);
        } else {
          create_simple_dialog(lv_scr_act(), "Support ZIP Failed", "Failed to generate a support zip!", true, true);
        }
      }
    } else if (btn == switch_to_stock_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto switch_to_stock_cmd = conf->get<std::string>("/commands/switch_to_stock_cmd");
      lv_obj_t *mbox = create_simple_dialog(lv_scr_act(), SWITCH_TO_STOCK_BUTTON_TITLE " Initiated", SWITCH_TO_STOCK_BUTTON_SUCCESS, false, false);
      run_command_deferred(mbox, switch_to_stock_cmd,
                           SWITCH_TO_STOCK_BUTTON_TITLE " Failed", SWITCH_TO_STOCK_BUTTON_FAILURE,
                           DEFERRED_COMMAND_DELAY_MS);
    } else if (btn == factory_reset_btn.get_container()) {
      lv_obj_t *mbox  = create_simple_dialog(lv_scr_act(), FACTORY_RESET_BUTTON_TITLE " Initiated", FACTORY_RESET_BUTTON_SUCCESS, false, false);

      Config *conf = Config::get_instance();
      auto cmd = conf->get<std::string>("/commands/factory_reset_cmd");
      run_command_deferred(mbox, cmd,
                           FACTORY_RESET_BUTTON_TITLE " Failed", FACTORY_RESET_BUTTON_FAILURE,
                           FACTORY_RESET_DELAY_MS);
    }
  }
}
