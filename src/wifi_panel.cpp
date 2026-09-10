#include "wifi_panel.h"
#include "config.h"
#include "utils.h"
#include "logger.h"
#include "theme.h"
#include "subprocess.hpp"
#include "icons.h"
#include "simple_dialog.h"

#include <sstream>
#include <iostream>
#include <vector>
#include <utility>
#include <algorithm>

namespace sp = subprocess;

static void draw_part_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
  if(dsc->part == LV_PART_ITEMS) {
    uint32_t row = dsc->id /  lv_table_get_col_cnt(obj);
    uint32_t col = dsc->id - row * lv_table_get_col_cnt(obj);

    if(col == 1) {
      dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
    }
  }
}

WifiPanel::WifiPanel(std::mutex &l, const WifiPanelOptions &options)
  : lv_lock(l)
  , cont(Theme::create_screen(options.parent))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , top_cont(Theme::create_row(cont))
  , wifi_table(lv_table_create(top_cont))
  , wifi_right(Theme::create_row(top_cont))
  , prompt_cont(wifi_right)
  , wifi_label(lv_label_create(prompt_cont))
  , password_input(lv_textarea_create(prompt_cont))
  , footer_label(options.footer_text != nullptr ? lv_label_create(cont) : nullptr)
  , on_back(options.on_back)
  , back_btn(cont, Icons::BACK, "Back", &WifiPanel::_handle_back_btn, this)
  , refresh_btn(cont, Icons::REFRESH_IMG, "Refresh", &WifiPanel::_handle_refresh_btn, this)
  , kb(lv_keyboard_create(cont))
{
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(spinner, Theme::scale_r(60), Theme::scale_r(60));
  lv_obj_set_style_arc_width(spinner, Theme::scale_r(6), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, Theme::scale_r(6), LV_PART_INDICATOR);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

  back_btn.float_bottom_right();
  refresh_btn.float_bottom_right();
  if (options.show_back_button) {
    // Refresh sits one tile to the left of Back
    lv_obj_align(refresh_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT,
                 -(ButtonContainer::float_w() + Theme::gap()), 0);
  } else {
    back_btn.hide();
  }

  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_width(top_cont, LV_PCT(100));

  // the list and the prompt column share the row; the SSID column takes
  // whatever the icon column leaves (set once the table has its width, see
  // handle_callback), and rows are tall enough for a finger
  lv_obj_set_size(wifi_table, 0, LV_PCT(100));
  lv_obj_set_flex_grow(wifi_table, 1);
  lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
  lv_table_set_col_width(wifi_table, 1, Theme::scale_w(100));
  const lv_coord_t row_text_h = lv_font_get_line_height(lv_obj_get_style_text_font(wifi_table, LV_PART_ITEMS));
  lv_obj_set_style_pad_ver(wifi_table, std::max(Theme::gap(), (Theme::touch_h() - row_text_h) / 2), LV_PART_ITEMS);

  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_SIZE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_LONG_PRESSED, this);
  lv_obj_add_event_cb(wifi_table, draw_part_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  Theme::manage_scroll(wifi_table);  // beside the list, clear of its corners; re-fits when the keyboard shrinks it

  // the prompt column: status text over the password entry, one gap apart
  lv_obj_set_flex_grow(wifi_right, 1);
  lv_obj_set_height(wifi_right, LV_PCT(100));
  lv_obj_set_flex_flow(wifi_right, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(wifi_right, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);

  // a form, read top down: dim captions over their values (recolor markup in
  // the one label), left-aligned like the entry beneath
  lv_obj_set_width(wifi_label, LV_PCT(100));
  lv_label_set_recolor(wifi_label, true);

  lv_obj_set_size(password_input, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(password_input, Theme::scale_r(34), 0);
  lv_textarea_set_one_line(password_input, true);

  // the keyboard joins the column below the list while the entry has focus;
  // the screen padding keeps its keys one gap from the edges
  lv_keyboard_set_textarea(kb, password_input);
  lv_obj_set_style_bg_color(kb, Theme::col(Theme::BG), LV_PART_MAIN);  // it covers the floating tiles
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_size(kb, LV_PCT(100), LV_PCT(55));
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_FOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_DEFOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_READY, this);

  // allow clicks on non-clickables to hide the keyboard
  lv_obj_add_event_cb(prompt_cont, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(wifi_label, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);
  lv_obj_move_background(cont);
  lv_obj_move_foreground(spinner);

  if (footer_label != nullptr) {
    lv_label_set_text(footer_label, options.footer_text);
    lv_obj_add_flag(footer_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_text_color(footer_label, Theme::col(Theme::TEXT_DIM), 0);
    lv_obj_align(footer_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);  // the screen padding is its margin
  }

  wpa_event.register_callback("WifiPanel",
      [this](const std::string &event) { this->handle_wpa_event(event); });

  wpa_event.start();
}

WifiPanel::~WifiPanel() {
  stop_ip_poll();
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void WifiPanel::foreground() {
  LOG_TRACE("wifi panel fg");
  stop_ip_poll();
  lv_obj_move_foreground(cont);
  lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  wpa_event.send_command("SCAN");
}

void WifiPanel::handle_back_btn(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    LOG_TRACE("wifi panel bg");
    stop_ip_poll();
    if (on_back) {
      on_back();
      return;
    }
    lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
    Theme::refresh_scroll(wifi_table);  // the bar goes with it
    lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
}

void WifiPanel::handle_refresh_btn(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    LOG_INFO("Refreshing");
    foreground();
  }
}

void WifiPanel::remove_network(uint32_t btn_idx) {
  if (btn_idx == 0) {  // OK
    LOG_INFO("Removing network {}", selected_network);
    auto nid = list_networks.find(selected_network)->second;
    wpa_event.send_command(fmt::format("REMOVE_NETWORK {}", nid));
    wpa_event.send_command("SAVE_CONFIG");
  }
}

void WifiPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_SIZE_CHANGED) {
    const lv_coord_t ssid_w = lv_obj_get_content_width(wifi_table) - Theme::scale_w(100);
    if (ssid_w > 0) lv_table_set_col_width(wifi_table, 0, ssid_w);
    return;
  }

  if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_LONG_PRESSED) {
    uint16_t row;
    uint16_t col;
    lv_table_get_selected_cell(wifi_table, &row, &col);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE) {
      return;
    }
    selected_network = lv_table_get_cell_value(wifi_table, row, 0);
  }

  if (code == LV_EVENT_VALUE_CHANGED) {
    // we need to reload the current network so we have the right state to compare
    if (find_current_network()) {
      LOG_TRACE("handle callback - current network {}", cur_network);
    }

    const bool switching_network = !cur_network.empty() && cur_network != selected_network;
    restart_wifi_from_network.clear();

    if (cur_network.length() > 0 && cur_network == selected_network) {
      update_connection_status_label(selected_network);
      lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
    } else if (list_networks.count(selected_network)) {
      stop_ip_poll();
      if (switching_network) {
        restart_wifi_from_network = cur_network;
      }
      auto nid = list_networks.find(selected_network)->second;
      wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
      wpa_event.send_command("SAVE_CONFIG");
    } else {
      stop_ip_poll();
      if (switching_network) {
        restart_wifi_from_network = cur_network;
      }
      lv_label_set_text(wifi_label, fmt::format("{0}Network#\n{1}\n\n{0}Password#", Theme::recolor(Theme::TEXT_DIM), selected_network).c_str());
      lv_obj_clear_flag(password_input, LV_OBJ_FLAG_HIDDEN);
      entering_password = true;
      lv_event_send(password_input, LV_EVENT_FOCUSED, NULL);
    }
    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_LONG_PRESSED) {
    if (list_networks.count(selected_network)) {
      static const char *btns[] = {"OK", "Cancel", ""};
      SimpleDialogOptions opts;
      opts.buttons = btns;
      opts.highlighted_button_idx = 0;  // deleting is the destructive choice
      opts.result_cb = _remove_network;
      opts.user_data = this;
      create_configurable_dialog(lv_layer_top(), "Forget network", fmt::format("Delete {}?", selected_network).c_str(), opts);
    }
  }
}

void WifiPanel::handle_wpa_event(const std::string &event) {
  if (event.rfind("<3>CTRL-EVENT-SCAN-RESULTS", 0) == 0) {
    if (entering_password) {
      return;
    }
    LOG_TRACE("got scan result event");
    std::istringstream f(wpa_event.send_command("SCAN_RESULTS"));
    std::string line;
    wifi_name_db.clear();
    uint32_t index = 0;

    bool has_current = find_current_network();
    if (has_current) {
      LOG_TRACE("handle wpa event scan results - current network {}", cur_network);
    }

    std::lock_guard<std::mutex> lock(lv_lock);
    if (!has_current) {
      lv_label_set_text(wifi_label, "");
    }
    while (std::getline(f, line)) {
      if (line.rfind("bss", 0) == 0) {
	      continue;
      }

      auto wifi_parts = KUtils::split(line, '\t');
      LOG_TRACE("wifi parts {}", join(wifi_parts, ", "));
      if (wifi_parts.size() == 5) {
        auto inserted = wifi_name_db.insert({wifi_parts[4], std::stoi(wifi_parts[2])});
        if (inserted.second) {
          lv_table_set_cell_value(wifi_table, index, 0, wifi_parts[4].c_str());
          if (cur_network != wifi_parts[4]) {
            lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_WIFI);
          } else if (cur_network.length() > 0) {
            lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_OK " " LV_SYMBOL_WIFI);
            update_connection_status_label(cur_network);
            lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
          }
          index++;
        }
      }
    } // while
    lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
    Theme::refresh_scroll(wifi_table);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  } else if (event.rfind("<3>CTRL-EVENT-CONNECTED", 0) == 0) {
    if (find_current_network()) {
      LOG_TRACE("handle wpa event connected - current network {}", cur_network);
      if (!restart_wifi_from_network.empty() && cur_network == selected_network) {
        restart_wifi();
      }
      std::vector<std::pair<std::string, int>> pairs;
      for (auto it = wifi_name_db.begin(); it != wifi_name_db.end(); ++it) {
	      pairs.push_back(*it);
      }

      std::sort(pairs.begin(), pairs.end(), [=](std::pair<std::string, int>& a,
						std::pair<std::string, int>& b) {
	      return a.second > b.second;
      });

      std::lock_guard<std::mutex> lock(lv_lock);

      uint32_t index = 0;
      for (const auto &wifi : pairs) {
        lv_table_set_cell_value(wifi_table, index, 0, wifi.first.c_str());
        if (cur_network != wifi.first) {
          lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_WIFI);
        } else if (cur_network.length() > 0) {
          lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_OK " " LV_SYMBOL_WIFI);
          update_connection_status_label(cur_network);
          start_ip_poll();
          lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
        }
        index++;
      }

      lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
      lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
      Theme::refresh_scroll(wifi_table);
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
      stop_ip_poll();
      lv_label_set_text(wifi_label, "");
    }
  } else if (event.rfind("<3>CTRL-EVENT-DISCONNECTED", 0) == 0) {
    stop_ip_poll();
  }
}

void WifiPanel::start_ip_poll() {
  waiting_for_ip = true;

  if (cur_network.empty()) {
    stop_ip_poll();
    return;
  }

  auto iface = KUtils::get_wifi_interface();
  auto ip = iface.empty() ? "0.0.0.0" : KUtils::interface_ip(iface);
  if (ip != "0.0.0.0") {
    stop_ip_poll();
    update_connection_status_label(cur_network);
    return;
  }

  if (ip_poll_timer == nullptr) {
    ip_poll_timer = lv_timer_create(&WifiPanel::_handle_ip_poll_timer, 500, this);
  }
}

void WifiPanel::stop_ip_poll() {
  waiting_for_ip = false;
  if (ip_poll_timer != nullptr) {
    lv_timer_del(ip_poll_timer);
    ip_poll_timer = nullptr;
  }
}

void WifiPanel::update_connection_status_label(const std::string &network_name) {
  auto iface = KUtils::get_wifi_interface();
  auto ip = iface.empty() ? "0.0.0.0" : KUtils::interface_ip(iface);
  if (ip != "0.0.0.0") {
    lv_label_set_text(wifi_label, fmt::format("{0}Connected to#\n{1}\n\n{0}IP address#\n{2}", Theme::recolor(Theme::TEXT_DIM), network_name, ip).c_str());
  } else {
    lv_label_set_text(wifi_label, fmt::format("Connecting to {}", network_name).c_str());
  }
}

void WifiPanel::handle_ip_poll_timer() {
  if (!waiting_for_ip || cur_network.empty()) {
    stop_ip_poll();
    return;
  }

  update_connection_status_label(cur_network);

  auto iface = KUtils::get_wifi_interface();
  auto ip = iface.empty() ? "0.0.0.0" : KUtils::interface_ip(iface);
  if (ip != "0.0.0.0") {
    stop_ip_poll();
  }
}

void WifiPanel::handle_kb_input(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    entering_password = false;
    lv_label_set_text(wifi_label, "Please select your wifi network");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY) {
    const char *password = lv_textarea_get_text(password_input);
    if (password == NULL || password[0] == 0) {
      return;
    }

    // add network, set password, save wpa
    entering_password = false;
    connect(password);
    lv_textarea_set_text(password_input, "");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(wifi_label, fmt::format("Connecting to {} ...", selected_network).c_str());
    lv_obj_clear_state(password_input, LV_STATE_FOCUSED);
    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_CLICKED) {
    lv_obj_t *target = lv_event_get_target(e);
    if (target != kb && target != password_input) {
      lv_event_send(password_input, LV_EVENT_DEFOCUSED, NULL);
    }
  }
}

void WifiPanel::connect(const char *password) {
  std::string nid = wpa_event.send_command("ADD_NETWORK");
  LOG_TRACE("add_nework {}", nid);
  if (nid.length() > 0) {
    wpa_event.send_command(fmt::format("SET_NETWORK {} ssid {:?}", nid, selected_network));
    wpa_event.send_command(fmt::format("SET_NETWORK {} psk {:?}", nid, password));
    wpa_event.send_command(fmt::format("ENABLE_NETWORK {}", nid));
    wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
    wpa_event.send_command("SAVE_CONFIG");
  } else {
    restart_wifi_from_network.clear();
  }
}

void WifiPanel::restart_wifi() {
  const std::string previous_network = std::move(restart_wifi_from_network);
  restart_wifi_from_network.clear();

  const auto cmd = Config::get_instance()->get<std::string>("/commands/restart_wifi_cmd");
  if (cmd.empty()) {
    LOG_DEBUG("WiFi restart command is not configured");
    return;
  }

  LOG_INFO("Restarting WiFi after switching from {} to {}", previous_network, selected_network);
  try {
    const int ret = sp::call(cmd);
    if (ret != 0) {
      LOG_ERROR("WiFi restart command '{}' exited with status {}", cmd, ret);
    }
  } catch (const std::exception &e) {
    LOG_ERROR("Failed to execute WiFi restart command '{}': {}", cmd, e.what());
  }
}

bool WifiPanel::find_current_network() {
  list_networks.clear();
  std::string nets = wpa_event.send_command("LIST_NETWORKS");
  LOG_TRACE("nets = {}", nets);
  std::istringstream f(nets);
  std::string line;
  cur_network = ""; // reset it to nothing in case we deleted the network
  bool found = false;
  while (std::getline(f, line)) {
    auto wifi_parts = KUtils::split(line, '\t');
    if (wifi_parts.size() == 4 && line.find("[CURRENT]") != std::string::npos) {
      cur_network = wifi_parts[1];
      list_networks.insert({wifi_parts[1], wifi_parts[0]});
      found = true;
    }
    if (wifi_parts.size() > 1) {
      list_networks.insert({wifi_parts[1], wifi_parts[0]});
    }
  }
  return found;
}
