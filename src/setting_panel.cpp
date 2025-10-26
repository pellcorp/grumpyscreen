#include "setting_panel.h"
#include "config.h"
#include "spdlog/spdlog.h"
#include "subprocess.hpp"
#include "simple_dialog.h"

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;

LV_IMG_DECLARE(network_img);
LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(update_img);
LV_IMG_DECLARE(sysinfo_img);
LV_IMG_DECLARE(emergency);
LV_IMG_DECLARE(print);

SettingPanel::SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent)
  : ws(c)
  , cont(lv_obj_create(parent))
  , wifi_panel(l)
  , sysinfo_panel()
  , wifi_btn(cont, &network_img, "WIFI", &SettingPanel::_handle_callback, this)
  , restart_klipper_btn(cont, &refresh_img, "Restart Klipper", &SettingPanel::_handle_callback, this)
  , restart_firmware_btn(cont, &refresh_img, "Restart\nFirmware", &SettingPanel::_handle_callback, this)
  , guppy_restart_btn(cont, &refresh_img, "Restart Grumpy", &SettingPanel::_handle_callback, this)
  , sysinfo_btn(cont, &sysinfo_img, "System", &SettingPanel::_handle_callback, this)
  , guppy_update_btn(cont, &update_img, "Update Grumpy", &SettingPanel::_handle_callback, this)
  , switch_to_stock_btn(cont, &emergency, "Switch to\nStock", &SettingPanel::_handle_callback, this,
    "**WARNING** **WARNING** **WARNING**\n\nAre you sure you want to switch to stock?\n\nThis will temporarily switch the printer to stock creality firmware!",
            [](){
              spdlog::info("switch to stock pressed");
              Config *conf = Config::get_instance();
              auto switch_to_stock_cmd = conf->get<std::string>("/switch_to_stock_cmd");
              auto ret = sp::call(switch_to_stock_cmd);
              if (ret == 0) {
                create_simple_dialog(lv_scr_act(), "Switch to Stock Initiated", "Please power cycle your printer!", false);
              } else {
                create_simple_dialog(lv_scr_act(), "Switch to Stock Failed", "Failed to initiate switch to stock!", true);
              }
            },
            true)
  , factory_reset_btn(cont, &emergency, "Factory\nReset", &SettingPanel::_handle_callback, this,
    		  "**WARNING** **WARNING** **WARNING**\n\nAre you sure you want to execute an emergency factory reset?\n\nThis will reset the printer to stock creality firmware!",
          [](){
            Config *conf = Config::get_instance();
            auto factory_reset_cmd = conf->get<std::string>("/factory_reset_cmd");
            auto ret = sp::call(factory_reset_cmd);
            if (ret == 0) {
              create_simple_dialog(lv_scr_act(), "Factory Reset Initiated", "Your printer will restart shortly!\nPlease wait for the stock screen to appear!", false);
            } else {
              create_simple_dialog(lv_scr_act(), "Factory Reset Failed", "Failed to initiate factory reset.", true);
            }
          },
          true)
{
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

  Config *conf = Config::get_instance();
  auto update_script = conf->get<std::string>("/guppy_update_cmd");
  if (update_script == "") {
    guppy_update_btn.disable();
  }
  auto switch_to_stock_cmd = conf->get<std::string>("/switch_to_stock_cmd");
  if (switch_to_stock_cmd == "") {
    switch_to_stock_btn.disable();
  }
  auto factory_reset_cmd = conf->get<std::string>("/factory_reset_cmd");
    if (factory_reset_cmd == "") {
      factory_reset_btn.disable();
    }

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(5), LV_GRID_FR(5), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1
  lv_obj_set_grid_cell(wifi_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(restart_klipper_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(restart_firmware_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(guppy_restart_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 1, 1);

  // row 2
  lv_obj_set_grid_cell(sysinfo_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(guppy_update_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(switch_to_stock_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(factory_reset_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 2, 1);
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
      spdlog::trace("wifi pressed");
      wifi_panel.foreground();
    } else if (btn == sysinfo_btn.get_container()) {
      spdlog::trace("system info pressed");
      sysinfo_panel.foreground();
    } else if (btn == restart_klipper_btn.get_container()) {
      spdlog::trace("restart klipper pressed");
      ws.send_jsonrpc("printer.restart");
    } else if (btn == restart_firmware_btn.get_container()) {
      spdlog::trace("restart klipper pressed");
      ws.send_jsonrpc("printer.firmware_restart");
    } else if (btn == guppy_restart_btn.get_container()) {
      spdlog::trace("restart grumpy pressed");
      Config *conf = Config::get_instance();
      auto restart_command = conf->get<std::string>("/guppy_restart_cmd");
      auto ret = sp::call(restart_command);
      if (ret != 0) {
        create_simple_dialog(lv_scr_act(), "Restart GrumpyScreen Failed", "Failed to restart GrumpyScreen!", true);
      }
    } else if (btn == guppy_update_btn.get_container()) {
      spdlog::trace("update guppy pressed");
      Config *conf = Config::get_instance();
      auto update_script = conf->get<std::string>("/guppy_update_cmd");
      auto ret = sp::call(update_script);
      if (ret != 0) {
        create_simple_dialog(lv_scr_act(), "Update GrumpyScreen Failed", "Failed to update GrumpyScreen!", true);
      }
    }
  }
}
