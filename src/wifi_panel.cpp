#include "wifi_panel.h"
#include "config.h"
#include "utils.h"
#include "logger.h"
#include "theme.h"
#include "subprocess.hpp"
#ifdef GUPPY_BOOTSTRAP
#include "icons.h"
#endif
#include "simple_dialog.h"

#include <sstream>
#include <iostream>
#include <vector>
#include <utility>
#include <algorithm>
#include <cstring>

namespace sp = subprocess;

static constexpr uint16_t ETHERNET_ROW = 0;
static constexpr uint16_t STATUS_COL = 1;
static constexpr uint16_t ICON_COL = 2;

static uint16_t first_wifi_row(bool has_ethernet) {
  return has_ethernet ? ETHERNET_ROW + 1 : ETHERNET_ROW;
}

WifiPanel::WifiPanel(std::mutex &l, const WifiPanelOptions &options)
  : lv_lock(l)
  , owns_cont(options.parent == nullptr)
  , cont(Theme::create_screen(options.parent))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , wifi_table(lv_table_create(cont))
  , credential_overlay(lv_obj_create(lv_layer_top()))
  , credential_box(lv_obj_create(credential_overlay))
  , credential_title(lv_label_create(credential_box))
  , credential_status(lv_label_create(credential_box))
  , password_row(Theme::create_row(credential_box))
  , password_input(lv_textarea_create(password_row))
  , password_visibility_btn(Theme::create_text_btn(password_row, LV_SYMBOL_EYE_OPEN,
        &WifiPanel::_handle_password_visibility, this))
#ifdef GUPPY_BOOTSTRAP
  , footer_label(options.footer_text != nullptr ? lv_label_create(cont) : nullptr)
  , refresh_btn(cont, Icons::REFRESH_IMG, "Refresh", &WifiPanel::_handle_refresh_btn, this)
#endif
  , kb(lv_keyboard_create(credential_box))
{
  if (options.flush) lv_obj_set_style_pad_left(cont, 0, LV_STATE_DEFAULT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(spinner, Theme::scale_r(60), Theme::scale_r(60));
  lv_obj_set_style_arc_width(spinner, Theme::scale_r(6), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, Theme::scale_r(6), LV_PART_INDICATOR);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

#ifdef GUPPY_BOOTSTRAP
  refresh_btn.float_bottom_right();
  if (!options.show_refresh_button) {
    refresh_btn.hide();
  }
#endif

  lv_obj_set_size(wifi_table, LV_PCT(100), 0);
  lv_obj_set_flex_grow(wifi_table, 1);
  lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
  lv_table_set_col_width(wifi_table, STATUS_COL, Theme::scale_w(180));
  lv_table_set_col_width(wifi_table, ICON_COL, Theme::scale_w(55));
  const lv_coord_t row_text_h = lv_font_get_line_height(lv_obj_get_style_text_font(wifi_table, LV_PART_ITEMS));
  lv_obj_set_style_pad_ver(wifi_table, std::max(Theme::gap(), (Theme::touch_h() - row_text_h) / 2), LV_PART_ITEMS);

  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_SIZE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_LONG_PRESSED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_draw_table_cell, LV_EVENT_DRAW_PART_BEGIN, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_draw_table_cell, LV_EVENT_DRAW_PART_END, this);

  Theme::manage_scroll(wifi_table);

  // One modal credential sheet owns the form and keyboard.  It remains fixed
  // in place during authentication; its inputs are disabled until supplicant
  // either connects or reports an error.
  lv_obj_remove_style_all(credential_overlay);
  lv_obj_set_size(credential_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_add_style(credential_overlay, &Theme::styles().popout, 0);
  lv_obj_add_flag(credential_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(credential_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_add_style(credential_box, &Theme::styles().popout_box, 0);
  lv_obj_set_size(credential_box, Theme::popout_w(), Theme::popout_max_h());
  lv_obj_set_flex_flow(credential_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(credential_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(credential_box);

  lv_obj_set_width(credential_title, LV_PCT(100));
  lv_obj_set_style_text_align(credential_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(credential_title, Theme::scale_font(18), 0);

  lv_obj_set_width(credential_status, LV_PCT(100));
  lv_obj_set_style_text_align(credential_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(credential_status, Theme::col(Theme::TEXT_DIM), 0);

  lv_obj_set_size(password_row, LV_PCT(100), Theme::touch_h());
  lv_obj_set_flex_flow(password_row, LV_FLEX_FLOW_ROW);

  lv_obj_set_size(password_input, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_min_height(password_input, Theme::scale_r(34), 0);
  lv_obj_set_style_pad_right(password_input, Theme::touch_h() + Theme::gap(), LV_PART_MAIN);
  const lv_font_t *password_font = Theme::scale_font(18);
  lv_obj_set_style_text_font(password_input, password_font, LV_PART_MAIN);
  lv_textarea_set_one_line(password_input, true);
  lv_textarea_set_password_mode(password_input, true);
  lv_textarea_set_placeholder_text(password_input, "Password");

  lv_obj_set_size(password_visibility_btn, Theme::touch_h(), LV_PCT(100));
  lv_obj_add_flag(password_visibility_btn, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_style_bg_opa(password_visibility_btn, LV_OPA_TRANSP, LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(password_visibility_btn, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_align(password_visibility_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *password_visibility_label = lv_obj_get_child(password_visibility_btn, 0);
  lv_obj_set_style_text_font(password_visibility_label, Theme::scale_font(22), 0);
  lv_obj_update_layout(password_visibility_label);
  lv_obj_center(password_visibility_label);
  // Font Awesome's eye sits low within the font's line box. Keep the button's
  // full-height hit area centered, but optically center the glyph in the entry.
  lv_obj_set_style_translate_y(password_visibility_label, -Theme::scale_r(3), 0);

  lv_keyboard_set_textarea(kb, password_input);
  lv_obj_set_style_bg_color(kb, Theme::col(Theme::SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_size(kb, LV_PCT(100), 0);
  lv_obj_set_flex_grow(kb, 1);
  lv_obj_set_style_opa(kb, LV_OPA_50, LV_STATE_DISABLED);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_READY, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_CANCEL, this);
  lv_obj_add_event_cb(kb, &WifiPanel::_handle_kb_input, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_move_background(cont);
  lv_obj_move_foreground(spinner);

#ifdef GUPPY_BOOTSTRAP
  if (footer_label != nullptr) {
    lv_label_set_text(footer_label, options.footer_text);
    lv_obj_add_flag(footer_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_text_color(footer_label, Theme::col(Theme::TEXT_DIM), 0);
    lv_obj_align(footer_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);  // the screen padding is its margin
  }
#endif

  wpa_event.register_callback("WifiPanel",
      [this](const std::string &event) { this->handle_wpa_event(event); });
}

WifiPanel::~WifiPanel() {
  if (network_start_timer != nullptr) {
    lv_timer_del(network_start_timer);
    network_start_timer = nullptr;
  }
  stop_ip_poll();
  if (connection_spinner_timer != nullptr) {
    lv_timer_del(connection_spinner_timer);
    connection_spinner_timer = nullptr;
  }
  if (connection_timeout_timer != nullptr) {
    lv_timer_del(connection_timeout_timer);
    connection_timeout_timer = nullptr;
  }
  if (credential_overlay != nullptr) {
    lv_obj_del(credential_overlay);
    credential_overlay = nullptr;
  }
  if (owns_cont && cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void WifiPanel::foreground() {
  LOG_TRACE("wifi panel fg");
  stop_ip_poll();
  lv_obj_move_foreground(cont);
  lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  if (!network_started) {
    // Draw the loading state first, then start WPA after this tab-click callback returns.
    if (network_start_timer == nullptr) {
      network_start_timer = lv_timer_create(&WifiPanel::_handle_network_start_timer, 1, this);
      lv_timer_set_repeat_count(network_start_timer, 1);
    }
    return;
  }
  // WPA has started but its first scan has not completed yet. Keep the
  // loading overlay up instead of issuing synchronous requests from a tab
  // event if the user switches away and back quickly.
  if (lv_obj_has_flag(wifi_table, LV_OBJ_FLAG_HIDDEN)) return;
  update_ethernet_status();
  if (find_current_network()) {
    update_connection_status_label(cur_network);
    start_ip_poll();
  }
  wpa_event.send_command("SCAN");
}

void WifiPanel::start_network() {
  if (network_started) return;

  if (network_start_timer != nullptr) {
    lv_timer_del(network_start_timer);
    network_start_timer = nullptr;
  }
  network_started = true;
  wpa_event.start();
}

#ifdef GUPPY_BOOTSTRAP
void WifiPanel::handle_refresh_btn(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    LOG_INFO("Refreshing");
    foreground();
  }
}
#endif

void WifiPanel::remove_network(uint32_t btn_idx) {
  if (btn_idx == 0) {  // OK
    LOG_INFO("Removing network {}", selected_network);
    const auto network = list_networks.find(selected_network);
    if (network == list_networks.end()) return;

    const bool removing_current_network = selected_network == cur_network;
    const auto response = wpa_event.send_command(fmt::format("REMOVE_NETWORK {}", network->second));
    if (response.rfind("OK", 0) != 0) {
      set_network_status(selected_network, "Could not forget", LV_SYMBOL_WIFI);
      return;
    }

    connection_in_progress = false;
    restart_wifi_after_connect = false;
    restart_wifi_from_network.clear();
    list_networks.erase(network);
    wpa_event.send_command("SAVE_CONFIG");
    wpa_event.send_command("SCAN");
    if (removing_current_network) {
      stop_ip_poll();
      cur_network.clear();
      stop_connection_spinner();
    }
  }
}

void WifiPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED) {
    suppress_next_table_selection = false;
    return;
  }

  if (code == LV_EVENT_SIZE_CHANGED) {
    const lv_coord_t ssid_w = lv_obj_get_content_width(wifi_table) -
        lv_table_get_col_width(wifi_table, STATUS_COL) -
        lv_table_get_col_width(wifi_table, ICON_COL);
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
    if ((has_ethernet && row == ETHERNET_ROW) || selected_network == "No networks found") return;
  }

  if (code == LV_EVENT_VALUE_CHANGED) {
    if (suppress_next_table_selection) {
      suppress_next_table_selection = false;
      return;
    }

    // we need to reload the current network so we have the right state to compare
    if (find_current_network()) {
      LOG_TRACE("handle callback - current network {}", cur_network);
    }

    const bool selecting_new_network = cur_network != selected_network;
    restart_wifi_after_connect = false;
    restart_wifi_from_network.clear();

    if (cur_network.length() > 0 && cur_network == selected_network) {
      connection_in_progress = false;
      update_connection_status_label(selected_network);
    } else if (list_networks.count(selected_network)) {
      stop_ip_poll();
      if (selecting_new_network) {
        restart_wifi_after_connect = true;
        restart_wifi_from_network = cur_network;
      }
      auto nid = list_networks.find(selected_network)->second;
      connection_in_progress = true;
      show_connecting_status(selected_network);
      const auto response = wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
      if (response.rfind("OK", 0) != 0) {
        connection_in_progress = false;
        restart_wifi_after_connect = false;
        restart_wifi_from_network.clear();
        set_network_status(selected_network, "Could not connect", LV_SYMBOL_WIFI);
        return;
      }
      wpa_event.send_command("SAVE_CONFIG");
    } else {
      stop_ip_poll();
      if (selecting_new_network) {
        restart_wifi_after_connect = true;
        restart_wifi_from_network = cur_network;
      }
      show_password_dialog();
    }
  } else if (code == LV_EVENT_LONG_PRESSED) {
    suppress_next_table_selection = true;
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
    uint32_t index;

    bool has_current = find_current_network();
    if (has_current) {
      LOG_TRACE("handle wpa event scan results - current network {}", cur_network);
    }

    std::lock_guard<std::mutex> lock(lv_lock);
    update_ethernet_status();
    index = first_wifi_row(has_ethernet);
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
          lv_table_set_cell_value(wifi_table, index, STATUS_COL, "");
          lv_table_set_cell_value(wifi_table, index, ICON_COL, LV_SYMBOL_WIFI);
          index++;
        }
      }
    } // while
    if (index == 0) {
      lv_table_set_cell_value(wifi_table, 0, 0, "No networks found");
      lv_table_set_cell_value(wifi_table, 0, STATUS_COL, "");
      lv_table_set_cell_value(wifi_table, 0, ICON_COL, "");
      index = 1;
    }
    lv_table_set_row_cnt(wifi_table, index);
    if (connection_in_progress) show_connecting_status(selected_network);
    else if (has_current) update_connection_status_label(cur_network);
    lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
    Theme::refresh_scroll(wifi_table);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  } else if (event.rfind("<3>CTRL-EVENT-CONNECTED", 0) == 0) {
    if (find_current_network()) {
      connection_in_progress = false;
      LOG_TRACE("handle wpa event connected - current network {}", cur_network);

      const bool credential_connection_succeeded =
          entering_password && !pending_network_id.empty() && cur_network == selected_network;
      if (credential_connection_succeeded) {
        // Persist the newly authenticated network before restart_wifi() tears
        // down supplicant and DHCP state.
        wpa_event.send_command("SAVE_CONFIG");
        pending_network_id.clear();

        std::lock_guard<std::mutex> lock(lv_lock);
        if (connection_timeout_timer != nullptr) {
          lv_timer_del(connection_timeout_timer);
          connection_timeout_timer = nullptr;
        }
        hide_password_dialog();
      }

      if (restart_wifi_after_connect && cur_network == selected_network) {
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

      update_ethernet_status();
      uint32_t index = first_wifi_row(has_ethernet);
      for (const auto &wifi : pairs) {
        lv_table_set_cell_value(wifi_table, index, 0, wifi.first.c_str());
        lv_table_set_cell_value(wifi_table, index, STATUS_COL, "");
        lv_table_set_cell_value(wifi_table, index, ICON_COL, LV_SYMBOL_WIFI);
        index++;
      }
      if (index == 0) {
        lv_table_set_cell_value(wifi_table, 0, 0, "No networks found");
        lv_table_set_cell_value(wifi_table, 0, STATUS_COL, "");
        lv_table_set_cell_value(wifi_table, 0, ICON_COL, "");
        index = 1;
      }
      lv_table_set_row_cnt(wifi_table, index);
      update_connection_status_label(cur_network);
      start_ip_poll();

      lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
      lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
      Theme::refresh_scroll(wifi_table);
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
      stop_ip_poll();
      std::lock_guard<std::mutex> lock(lv_lock);
      connection_in_progress = false;
      stop_connection_spinner();
    }
  } else if (event.find("CTRL-EVENT-SSID-TEMP-DISABLED") != std::string::npos &&
             event.find("WRONG_KEY") != std::string::npos) {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (credential_connecting) {
      show_connection_error("Incorrect password. Please try again.");
    } else {
      connection_in_progress = false;
      set_network_status(selected_network, "Connection failed", LV_SYMBOL_WIFI);
    }
  } else if (event.find("CTRL-EVENT-ASSOC-REJECT") != std::string::npos) {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (credential_connecting) {
      show_connection_error("The access point rejected the connection. Try again.");
    } else {
      connection_in_progress = false;
      set_network_status(selected_network, "Connection rejected", LV_SYMBOL_WIFI);
    }
  } else if (event.find("CTRL-EVENT-NETWORK-NOT-FOUND") != std::string::npos) {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (credential_connecting) {
      show_connection_error("Network not found. Check that it is still available.");
    } else {
      connection_in_progress = false;
      set_network_status(selected_network, "Not found", LV_SYMBOL_WIFI);
    }
  } else if (event.rfind("<3>CTRL-EVENT-DISCONNECTED", 0) == 0) {
    stop_ip_poll();
    std::lock_guard<std::mutex> lock(lv_lock);
    if (!connection_in_progress) {
      stop_connection_spinner();
      set_network_status(cur_network, "", LV_SYMBOL_WIFI);
    }
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
    set_network_status(network_name, ip, LV_SYMBOL_OK " " LV_SYMBOL_WIFI);
    stop_connection_spinner();
  } else {
    show_connecting_status(network_name);
  }
}

void WifiPanel::update_ethernet_status() {
  std::string ip;
  has_ethernet = false;
  for (const auto &iface : KUtils::get_interfaces()) {
    if (!iface.empty() && iface[0] == 'e') {
      has_ethernet = true;
      const auto candidate = KUtils::interface_ip(iface);
      if (candidate != "0.0.0.0") {
        ip = candidate;
        break;
      }
    }
  }
  if (!has_ethernet) {
    return;
  }
  lv_table_set_cell_value(wifi_table, ETHERNET_ROW, 0, "Ethernet");
  lv_table_set_cell_value(wifi_table, ETHERNET_ROW, STATUS_COL,
                          ip.empty() ? "Not connected" : ip.c_str());
  lv_table_set_cell_value(wifi_table, ETHERNET_ROW, ICON_COL, "");
}

void WifiPanel::set_network_status(const std::string &network_name,
                                   const std::string &status, const char *icon) {
  for (uint16_t row = first_wifi_row(has_ethernet);
       row < lv_table_get_row_cnt(wifi_table); ++row) {
    if (network_name == lv_table_get_cell_value(wifi_table, row, 0)) {
      if (row == connection_spinner_row && !status.empty()) {
        stop_connection_spinner();
      }
      lv_table_set_cell_value(wifi_table, row, STATUS_COL, status.c_str());
      lv_table_set_cell_value(wifi_table, row, ICON_COL, icon);
      return;
    }
  }
}

void WifiPanel::show_connecting_status(const std::string &network_name) {
  for (uint16_t row = first_wifi_row(has_ethernet);
       row < lv_table_get_row_cnt(wifi_table); ++row) {
    if (network_name == lv_table_get_cell_value(wifi_table, row, 0)) {
      lv_table_set_cell_value(wifi_table, row, STATUS_COL, "");
      lv_table_set_cell_value(wifi_table, row, ICON_COL, "");
      if (connection_spinner_row == row && connection_spinner_timer != nullptr) return;

      stop_connection_spinner();
      connection_spinner_row = row;
      connection_spinner_area_valid = false;
      connection_spinner_timer = lv_timer_create(&WifiPanel::_handle_spinner_timer, 100, this);
      return;
    }
  }
}

void WifiPanel::stop_connection_spinner() {
  connection_spinner_row = LV_TABLE_CELL_NONE;
  if (connection_spinner_timer != nullptr) {
    lv_timer_del(connection_spinner_timer);
    connection_spinner_timer = nullptr;
  }
  if (wifi_table != nullptr && connection_spinner_area_valid) {
    lv_obj_invalidate_area(wifi_table, &connection_spinner_area);
  }
  connection_spinner_area_valid = false;
}

void WifiPanel::handle_spinner_timer() {
  connection_spinner_angle = (connection_spinner_angle + 18) % 360;
  if (connection_spinner_area_valid) {
    lv_obj_invalidate_area(wifi_table, &connection_spinner_area);
  }
}

void WifiPanel::draw_table_cell(lv_event_t *event) {
  lv_obj_t *table = lv_event_get_target(event);
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(event);
  if (dsc->part != LV_PART_ITEMS) return;

  const uint16_t col_count = lv_table_get_col_cnt(table);
  const uint16_t row = dsc->id / col_count;
  const uint16_t col = dsc->id % col_count;
  if (lv_event_get_code(event) == LV_EVENT_DRAW_PART_BEGIN) {
    if (col == STATUS_COL || col == ICON_COL) dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
    return;
  }

  // The built-in Montserrat fonts do not include a bold face. Draw the active
  // row's text a second time one pixel inward to produce a bold weight without
  // adding another font asset to the embedded build.
  const char *row_name = lv_table_get_cell_value(table, row, 0);
  const char *cell_text = lv_table_get_cell_value(table, row, col);
  if (!cur_network.empty() && cur_network == row_name && cell_text[0] != '\0') {
    lv_draw_label_dsc_t bold = *dsc->label_dsc;
    bold.ofs_x += bold.align == LV_TEXT_ALIGN_RIGHT ? -Theme::scale_r(1) : Theme::scale_r(1);

    lv_area_t text_area = *dsc->draw_area;
    text_area.x1 += lv_obj_get_style_pad_left(table, LV_PART_ITEMS);
    text_area.x2 -= lv_obj_get_style_pad_right(table, LV_PART_ITEMS);
    lv_point_t text_size;
    lv_txt_get_size(&text_size, cell_text, bold.font, bold.letter_space, bold.line_space,
                    lv_area_get_width(&text_area), LV_TEXT_FLAG_NONE);
    text_area.y1 = dsc->draw_area->y1 + lv_area_get_height(dsc->draw_area) / 2 - text_size.y / 2;
    text_area.y2 = text_area.y1 + text_size.y;
    lv_draw_label(dsc->draw_ctx, &bold, &text_area, cell_text, nullptr);
  }

  if (row != connection_spinner_row || col != ICON_COL) return;

  connection_spinner_area = *dsc->draw_area;
  connection_spinner_area_valid = true;

  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.color = dsc->label_dsc->color;
  arc.width = Theme::scale_r(3);
  arc.rounded = true;
  lv_point_t center = {
    static_cast<lv_coord_t>(dsc->draw_area->x1 + lv_area_get_width(dsc->draw_area) / 2),
    static_cast<lv_coord_t>(dsc->draw_area->y1 + lv_area_get_height(dsc->draw_area) / 2)
  };
  lv_draw_arc(dsc->draw_ctx, &arc, &center, Theme::scale_r(8),
              connection_spinner_angle, connection_spinner_angle + 260);
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
  if (code == LV_EVENT_VALUE_CHANGED) {
    update_password_submit_state();
  } else if (code == LV_EVENT_READY) {
    submit_password();
  } else if (code == LV_EVENT_CANCEL && !credential_connecting) {
    cancel_password_dialog();
  }
}

void WifiPanel::update_password_submit_state() {
  const char *password = lv_textarea_get_text(password_input);
  const bool enabled = password != nullptr && std::strlen(password) >= 8;

  const char **map = lv_btnmatrix_get_map(kb);
  uint16_t btn_id = 0;
  for (uint16_t map_id = 0; map[map_id][0] != '\0'; ++map_id) {
    if (std::strcmp(map[map_id], "\n") == 0) continue;

    if (std::strcmp(map[map_id], LV_SYMBOL_OK) == 0) {
      if (enabled) {
        lv_btnmatrix_clear_btn_ctrl(kb, btn_id, LV_BTNMATRIX_CTRL_DISABLED);
      } else {
        lv_btnmatrix_set_btn_ctrl(kb, btn_id, LV_BTNMATRIX_CTRL_DISABLED);
      }
      break;
    }
    ++btn_id;
  }
}

void WifiPanel::handle_password_visibility(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  const bool was_masked = lv_textarea_get_password_mode(password_input);
  lv_textarea_set_password_mode(password_input, !was_masked);
  Theme::set_btn_label(password_visibility_btn,
                       was_masked ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void WifiPanel::cancel_password_dialog() {
  discard_pending_network();
  restart_wifi_after_connect = false;
  restart_wifi_from_network.clear();
  connection_in_progress = false;
  hide_password_dialog();
  if (find_current_network()) {
    update_connection_status_label(cur_network);
  }
}

bool WifiPanel::connect(const char *password) {
  if (pending_network_id.empty()) {
    pending_network_id = wpa_event.send_command("ADD_NETWORK");
    const auto end = pending_network_id.find_last_not_of("\r\n \t");
    pending_network_id = end == std::string::npos
        ? std::string()
        : pending_network_id.substr(0, end + 1);
    if (pending_network_id.find_first_not_of("0123456789") != std::string::npos) {
      pending_network_id.clear();
    }
  }
  LOG_TRACE("pending network {}", pending_network_id);
  if (!pending_network_id.empty()) {
    const auto command_ok = [this](const std::string &command) {
      return wpa_event.send_command(command).rfind("OK", 0) == 0;
    };
    return command_ok(fmt::format("SET_NETWORK {} ssid {:?}", pending_network_id, selected_network)) &&
           command_ok(fmt::format("SET_NETWORK {} psk {:?}", pending_network_id, password)) &&
           command_ok(fmt::format("ENABLE_NETWORK {}", pending_network_id)) &&
           command_ok(fmt::format("SELECT_NETWORK {}", pending_network_id));
  } else {
    restart_wifi_after_connect = false;
    restart_wifi_from_network.clear();
    return false;
  }
}

void WifiPanel::show_password_dialog() {
  entering_password = true;
  credential_connecting = false;
  connection_in_progress = false;
  lv_label_set_text(credential_title, fmt::format("Connect to {}", selected_network).c_str());
  lv_label_set_text(credential_status, "Enter the network password");
  lv_obj_set_style_text_color(credential_status, Theme::col(Theme::TEXT_DIM), 0);
  lv_textarea_set_text(password_input, "");
  lv_textarea_set_password_mode(password_input, true);
  Theme::set_btn_label(password_visibility_btn, LV_SYMBOL_EYE_OPEN);
  lv_obj_clear_state(password_input, LV_STATE_DISABLED);
  lv_obj_clear_state(kb, LV_STATE_DISABLED);
  update_password_submit_state();
  lv_obj_clear_flag(credential_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(credential_overlay);
}

void WifiPanel::hide_password_dialog() {
  entering_password = false;
  credential_connecting = false;
  lv_textarea_set_text(password_input, "");
  lv_obj_add_flag(credential_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(credential_overlay);
}

void WifiPanel::submit_password() {
  if (credential_connecting) return;
  const char *password = lv_textarea_get_text(password_input);
  if (password == nullptr || std::strlen(password) < 8) {
    lv_label_set_text(credential_status, "Password must be at least 8 characters");
    lv_obj_set_style_text_color(credential_status, Theme::col(Theme::DANGER), 0);
    return;
  }

  credential_connecting = true;
  connection_in_progress = true;
  lv_label_set_text(credential_status, "Connecting...");
  lv_obj_set_style_text_color(credential_status, Theme::col(Theme::TEXT_DIM), 0);
  show_connecting_status(selected_network);
  lv_obj_add_state(password_input, LV_STATE_DISABLED);
  lv_obj_add_state(kb, LV_STATE_DISABLED);

  if (!connect(password)) {
    show_connection_error("Could not start the connection. Try again.");
    return;
  }

  if (connection_timeout_timer != nullptr) {
    lv_timer_del(connection_timeout_timer);
  }
  connection_timeout_timer = lv_timer_create(&WifiPanel::_handle_connection_timeout, 15000, this);
  lv_timer_set_repeat_count(connection_timeout_timer, 1);
}

void WifiPanel::show_connection_error(const char *message) {
  credential_connecting = false;
  connection_in_progress = false;
  if (connection_timeout_timer != nullptr) {
    lv_timer_del(connection_timeout_timer);
    connection_timeout_timer = nullptr;
  }
  stop_connection_spinner();
  set_network_status(selected_network, "Connection failed", LV_SYMBOL_WIFI);
  lv_label_set_text(credential_status, message);
  lv_obj_set_style_text_color(credential_status, Theme::col(Theme::DANGER), 0);
  lv_textarea_set_text(password_input, "");
  lv_obj_clear_state(password_input, LV_STATE_DISABLED);
  lv_obj_clear_state(kb, LV_STATE_DISABLED);
}

void WifiPanel::handle_connection_timeout() {
  connection_timeout_timer = nullptr;
  if (credential_connecting) {
    show_connection_error("Connection timed out. Check the password and try again.");
  }
}

void WifiPanel::discard_pending_network() {
  if (!pending_network_id.empty()) {
    wpa_event.send_command(fmt::format("REMOVE_NETWORK {}", pending_network_id));
    pending_network_id.clear();
  }
}

void WifiPanel::restart_wifi() {
  const std::string previous_network = std::move(restart_wifi_from_network);
  restart_wifi_after_connect = false;
  restart_wifi_from_network.clear();

  const auto cmd = Config::get_instance()->get<std::string>("/commands/restart_wifi_cmd");
  if (cmd.empty()) {
    LOG_DEBUG("WiFi restart command is not configured");
    return;
  }

  if (previous_network.empty()) {
    LOG_INFO("Restarting WiFi after connecting to {}", selected_network);
  } else {
    LOG_INFO("Restarting WiFi after switching from {} to {}", previous_network, selected_network);
  }
  try {
    const int ret = sp::call(cmd);
    if (ret != 0) {
      LOG_ERROR("WiFi restart command '{}' exited with status {}", cmd, ret);
    }
  } catch (const std::exception &e) {
    LOG_ERROR("Failed to execute WiFi restart command '{}': {}", cmd, e.what());
  }
  wpa_event.reconnect();
}

bool WifiPanel::find_current_network() {
  const std::string nets = wpa_event.send_command("LIST_NETWORKS");
  LOG_TRACE("nets = {}", nets);

  std::istringstream f(nets);
  std::string line;
  std::string next_current_network;
  std::map<std::string, std::string> next_list_networks;
  bool response_header_found = false;
  bool found = false;
  while (std::getline(f, line)) {
    auto wifi_parts = KUtils::split(line, '\t');
    if (line.rfind("network id", 0) == 0) {
      response_header_found = true;
      continue;
    }
    if (wifi_parts.size() == 4 && line.find("[CURRENT]") != std::string::npos) {
      next_current_network = wifi_parts[1];
      next_list_networks.insert({wifi_parts[1], wifi_parts[0]});
      found = true;
    }
    if (wifi_parts.size() > 1) {
      next_list_networks.insert({wifi_parts[1], wifi_parts[0]});
    }
  }

  if (!response_header_found) {
    LOG_DEBUG("LIST_NETWORKS unavailable; retaining the last known network list");
    return !cur_network.empty();
  }

  cur_network = std::move(next_current_network);
  list_networks = std::move(next_list_networks);
  return found;
}
