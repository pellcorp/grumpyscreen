#include "setting_panel.h"
#include "i18n.h"
#include "config.h"
#include "logger.h"
#include "subprocess.hpp"
#include "simple_dialog.h"
#include "icons.h"
#include "theme.h"
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <experimental/filesystem>
#include <vector>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;

static std::pair<std::string, std::string> read_version_file() {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);

    if (len <= 0)
        return {"unknown", "unknown"};

    exe_path[len] = '\0';
    fs::path version_path = fs::path(exe_path).parent_path() / "VERSION";

    std::ifstream file(version_path);
    std::string version, commit;

    if (!(file >> version))
        return {"unknown", "unknown"};

    std::getline(file, commit);
    const auto start = commit.find_first_not_of(" \t\r\n");
    commit = (start == std::string::npos) ? "" : commit.substr(start);

    if (commit.empty())
        commit = "unknown";

    return {version, commit};
}


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

static constexpr int32_t DISPLAY_SLEEP_VALUES[] = {-1, 60, 300, 600, 1800};
static lv_obj_t *language_selector = nullptr;
static std::vector<std::string> g_lang_codes;
static std::vector<const char *> g_lang_map;
static const char *DISPLAY_SLEEP_MAP[] = {::tr("Never"), "1 min", "5 min", "10 min", "30 min", ""};

static int display_sleep_index(int32_t seconds) {
  for (size_t i = 0; i < sizeof(DISPLAY_SLEEP_VALUES) / sizeof(DISPLAY_SLEEP_VALUES[0]); ++i) {
    if (DISPLAY_SLEEP_VALUES[i] == seconds) return static_cast<int>(i);
  }
  return -1;
}

static WifiPanelOptions embedded_wifi_options(lv_obj_t *parent) {
  WifiPanelOptions opts;
  opts.parent = parent;
  opts.flush = true;
  return opts;
}

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

static void layout_tiles(lv_obj_t *cont, const std::vector<ButtonContainer *> &tiles) {
  static const size_t cols = 4;
  static lv_coord_t grid_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  grid_row_dsc[1] = tiles.size() > cols ? LV_GRID_CONTENT : LV_GRID_TEMPLATE_LAST;
  lv_obj_set_grid_dsc_array(cont, grid_col_dsc, grid_row_dsc);

  for (size_t i = 0; i < tiles.size(); i++) {
    tiles[i]->use_plain();
    lv_obj_set_height(tiles[i]->get_container(), Theme::scale_r(92));
    lv_obj_set_grid_cell(tiles[i]->get_container(), LV_GRID_ALIGN_STRETCH, i % cols, 1,
                         LV_GRID_ALIGN_STRETCH, i / cols, 1);
  }
}

static lv_obj_t *add_about_line(lv_obj_t *parent, const std::string &text, bool title = false) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text.c_str());
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  if (title) lv_obj_add_style(label, &Theme::styles().dim_label, 0);
  return label;
}

SettingPanel::SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent)
  : ws(c)
  , owns_cont(parent == nullptr)
  , cont(Theme::create_screen(parent))  // fills the tab: it is the page
  , tabview(lv_tabview_create(cont, LV_DIR_TOP, Theme::scale_r(36)))
  , actions_tab(lv_tabview_add_tab(tabview, ::tr("Tools")))
  , wifi_tab(lv_tabview_add_tab(tabview, ::tr("Network")))
  , settings_tab(lv_tabview_add_tab(tabview, ::tr("Settings")))
  , about_tab(lv_tabview_add_tab(tabview, ::tr("About")))
  , actions_cont(Theme::create_screen(actions_tab))
  , settings_cont(Theme::create_screen(settings_tab))
  , about_cont(Theme::create_screen(about_tab))
  , emergency_prompt_switch(nullptr)
  , emergency_stop_warning(nullptr)
  , sleep_timeout_selector(nullptr)
#ifdef COSMOS
  , update_manager(c, l)
#endif
  , wifi_panel(l, embedded_wifi_options(wifi_tab))
  , restart_klipper_btn(actions_cont, Icons::RESTART_IMG, ::tr("Restart\nKlipper"), &SettingPanel::_handle_callback, this,
        ::tr("Restart Klipper"), ::tr("Do you want to restart klipper?"), {::tr("Back"), ::tr("Restart Klipper")})
  , restart_firmware_btn(actions_cont, Icons::FIRMWARE_IMG, ::tr("Firmware\nRestart"), &SettingPanel::_handle_callback, this,
        ::tr("Firmware Restart"), ::tr("Do you want to perform a firmware restart?"), {::tr("Back"), ::tr("Firmware Restart")})
  , guppy_restart_btn(actions_cont, Icons::GUI_IMG, ::tr("Restart GUI"), &SettingPanel::_handle_callback, this)
  , support_zip_btn(actions_cont, Icons::SUPPORT_ZIP_IMG, ::tr("Create\nSupport ZIP"), &SettingPanel::_handle_callback, this)
  , switch_to_stock_btn(actions_cont, Icons::SWITCH_STOCK_IMG, SWITCH_TO_STOCK_BUTTON_TEXT, &SettingPanel::_handle_callback, this,
          SWITCH_TO_STOCK_BUTTON_TITLE, SWITCH_TO_STOCK_BUTTON_PROMPT, {::tr("Back"), ::tr("Switch to Stock")})
  , factory_reset_btn(actions_cont, Icons::FACTORY_RESET_IMG, FACTORY_RESET_BUTTON_TEXT, &SettingPanel::_handle_callback, this,
		  FACTORY_RESET_BUTTON_TITLE, FACTORY_RESET_BUTTON_PROMPT, {::tr("Back"), ::tr("Factory Reset")})
#ifdef COSMOS
  // no prompt of its own: the update manager client checks for an update
  // first and asks only when there is one to install
  , update_btn(actions_cont, Icons::UPDATE_IMG, UPDATE_BUTTON_TEXT, &SettingPanel::_handle_callback, this)
#endif
  , shutdown_host_btn(actions_cont, Icons::SHUTDOWN_IMG, ::tr("Shutdown\nHost"), &SettingPanel::_handle_callback, this,
          ::tr("Shutdown host?"), ::tr("Do you want to shutdown the host?"), {::tr("Back"), ::tr("Shutdown Host")})
{
  // Restart Klipper is a local service command and remains useful even when
  // Moonraker or Klipper is unavailable. Firmware restart, on the other hand,
  // is a Klipper RPC and starts disabled until that connection is confirmed.
  restart_firmware_btn.disable();
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
  lv_obj_add_event_cb(tabview, &SettingPanel::_tabview_event_cb,
                      LV_EVENT_VALUE_CHANGED, this);
  lv_obj_set_style_pad_all(actions_tab, 0, 0);
  lv_obj_set_style_pad_all(wifi_tab, 0, 0);
  lv_obj_set_style_pad_all(settings_tab, 0, 0);
  lv_obj_set_style_pad_all(about_tab, 0, 0);
  Theme::style_embedded_tabview(tabview);

  Config *conf = Config::get_instance();
  lv_obj_t *about_panel = lv_obj_create(about_cont);
  lv_obj_clear_flag(about_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(about_panel, &Theme::styles().panel, 0);
  lv_obj_set_size(about_panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(about_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(about_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(about_panel, Theme::gap() * 2, 0);
#ifdef COSMOS
  add_about_line(about_panel, "Cosmos", true);
#else
  add_about_line(about_panel, "Grumpyscreen", true);
#endif
  const auto version_info = read_version_file();
  add_about_line(about_panel, std::string(::tr("Version: ")) + version_info.first);
  add_about_line(about_panel, std::string("Commit: ") + version_info.second);

  lv_obj_t *settings_panel = lv_obj_create(settings_cont);
  lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(settings_panel, &Theme::styles().panel, 0);
  lv_obj_set_size(settings_panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(settings_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(settings_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(settings_panel, Theme::gap() * 2, 0);

  lv_obj_t *emergency_row = Theme::create_row(settings_panel);
  lv_obj_set_size(emergency_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(emergency_row, Theme::touch_h(), 0);
  lv_obj_set_flex_flow(emergency_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(emergency_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Keep the warning with the label it qualifies, rather than making it read
  // like a separate setting below this row.
  lv_obj_t *emergency_text = lv_obj_create(emergency_row);
  lv_obj_remove_style_all(emergency_text);
  lv_obj_clear_flag(emergency_text, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(emergency_text, 0);
  lv_obj_set_flex_grow(emergency_text, 1);
  lv_obj_set_flex_flow(emergency_text, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(emergency_text, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *emergency_label = lv_label_create(emergency_text);
  lv_label_set_text(emergency_label, ::tr("Confirm emergency stop"));
  lv_obj_add_style(emergency_label, &Theme::styles().dim_label, 0);
  emergency_prompt_switch = lv_switch_create(emergency_row);
  if (conf->get<bool>("/ui/prompt_emergency_stop")) {
    lv_obj_add_state(emergency_prompt_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(emergency_prompt_switch, &SettingPanel::_setting_changed_cb,
                      LV_EVENT_VALUE_CHANGED, this);

  emergency_stop_warning = lv_label_create(emergency_text);
  lv_label_set_text(emergency_stop_warning,
                    ::tr("Long press of E-stop will be required to activate!"));
  lv_obj_set_width(emergency_stop_warning, LV_PCT(100));
  lv_label_set_long_mode(emergency_stop_warning, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(emergency_stop_warning, Theme::col(Theme::DANGER), 0);
  refresh_emergency_stop_warning();

  add_about_line(settings_panel, ::tr("Screen timeout"), true);
  sleep_timeout_selector = lv_btnmatrix_create(settings_panel);
  lv_obj_set_size(sleep_timeout_selector, LV_PCT(100), Theme::touch_h());
  lv_obj_add_style(sleep_timeout_selector, &Theme::styles().key_tray, 0);
  lv_btnmatrix_set_map(sleep_timeout_selector, DISPLAY_SLEEP_MAP);
  lv_btnmatrix_set_btn_ctrl_all(sleep_timeout_selector, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(sleep_timeout_selector, true);
  const int sleep_idx = display_sleep_index(conf->get<int32_t>("/ui/display_sleep_sec"));
  if (sleep_idx >= 0) {
    lv_btnmatrix_set_btn_ctrl(sleep_timeout_selector, sleep_idx, LV_BTNMATRIX_CTRL_CHECKED);
  }
  lv_obj_add_event_cb(sleep_timeout_selector, &SettingPanel::_setting_changed_cb,
                      LV_EVENT_VALUE_CHANGED, this);

  add_about_line(settings_panel, ::tr("Language"), true);
  if (g_lang_codes.empty()) {
    g_lang_codes.push_back("en");
    std::string dir = (std::filesystem::canonical("/proc/self/exe").parent_path() / "language").string();
    for (auto &c : i18n_list(dir)) if (c != "en") g_lang_codes.push_back(c);
    for (size_t i = 0; i < g_lang_codes.size(); i++) {
      if (i && i % 4 == 0) g_lang_map.push_back("\n");
      g_lang_map.push_back(g_lang_codes[i].c_str());
    }
    g_lang_map.push_back("");
  }
  language_selector = lv_btnmatrix_create(settings_panel);
  lv_obj_set_size(language_selector, LV_PCT(100), Theme::touch_h());
  lv_obj_add_style(language_selector, &Theme::styles().key_tray, 0);
  lv_btnmatrix_set_map(language_selector, g_lang_map.data());
  lv_btnmatrix_set_btn_ctrl_all(language_selector, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(language_selector, true);
  {
    std::string cur = conf->get<std::string>("/ui/language");
    size_t idx = 0;
    for (size_t i = 0; i < g_lang_codes.size(); i++) if (g_lang_codes[i] == cur) idx = i;
    lv_btnmatrix_set_btn_ctrl(language_selector, idx, LV_BTNMATRIX_CTRL_CHECKED);
  }
  lv_obj_add_event_cb(language_selector, &SettingPanel::_setting_changed_cb,
                      LV_EVENT_VALUE_CHANGED, this);

  // Optional tiles only appear with a command behind them; the grid is built
  // from what is left, four across, so a hidden tile never leaves a hole.
  auto has_cmd = [conf](const char *key) { return conf->get<std::string>(key) != ""; };
  std::vector<ButtonContainer *> action_tiles = {
      &restart_klipper_btn, &restart_firmware_btn, &guppy_restart_btn};
  struct Optional { ButtonContainer *tile; const char *cmd; };
  for (const Optional &o : {Optional{&support_zip_btn, "/commands/support_zip_cmd"}}) {
    if (has_cmd(o.cmd)) action_tiles.push_back(o.tile); else o.tile->hide();
  }
  for (const Optional &o : {Optional{&switch_to_stock_btn, "/commands/switch_to_stock_cmd"},
                            Optional{&factory_reset_btn, "/commands/factory_reset_cmd"}
                            }) {
    if (has_cmd(o.cmd)) action_tiles.push_back(o.tile); else o.tile->hide();
  }
#ifdef COSMOS
  action_tiles.push_back(&update_btn);
#endif

  for (const Optional &o : {Optional{&shutdown_host_btn, "/commands/shutdown_host_cmd"}}) {
    if (has_cmd(o.cmd)) action_tiles.push_back(o.tile); else o.tile->hide();
  }

  layout_tiles(actions_cont, action_tiles);
}

SettingPanel::~SettingPanel() {
  if (owns_cont && cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *SettingPanel::get_container() {
  return cont;
}

void SettingPanel::foreground() {
  refresh_active_tab();
}

void SettingPanel::select_first_tab() {
  lv_tabview_set_act(tabview, lv_obj_get_index(actions_tab), LV_ANIM_OFF);
}

void SettingPanel::set_firmware_restart_available(bool available) {
  if (available) restart_firmware_btn.enable();
  else restart_firmware_btn.disable();
}

void SettingPanel::_tabview_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    static_cast<SettingPanel *>(lv_event_get_user_data(event))->refresh_active_tab();
  }
}

void SettingPanel::_setting_changed_cb(lv_event_t *event) {
  static_cast<SettingPanel *>(lv_event_get_user_data(event))->setting_changed(event);
}

void SettingPanel::setting_changed(lv_event_t *event) {
  Config *conf = Config::get_instance();
  lv_obj_t *target = lv_event_get_target(event);
  bool saved = false;

  if (target == emergency_prompt_switch) {
    const bool enabled = lv_obj_has_state(emergency_prompt_switch, LV_STATE_CHECKED);
    saved = conf->set_prompt_emergency_stop(enabled);
    if (!saved) {
      if (conf->get<bool>("/ui/prompt_emergency_stop"))
        lv_obj_add_state(emergency_prompt_switch, LV_STATE_CHECKED);
      else
        lv_obj_clear_state(emergency_prompt_switch, LV_STATE_CHECKED);
    }
  } else if (target == sleep_timeout_selector) {
    const uint32_t selected = lv_btnmatrix_get_selected_btn(sleep_timeout_selector);
    if (selected < sizeof(DISPLAY_SLEEP_VALUES) / sizeof(DISPLAY_SLEEP_VALUES[0])) {
      saved = conf->set_display_sleep_sec(DISPLAY_SLEEP_VALUES[selected]);
    }
    if (!saved) {
      lv_btnmatrix_clear_btn_ctrl_all(sleep_timeout_selector, LV_BTNMATRIX_CTRL_CHECKED);
      const int old_idx = display_sleep_index(conf->get<int32_t>("/ui/display_sleep_sec"));
      if (old_idx >= 0) {
        lv_btnmatrix_set_btn_ctrl(sleep_timeout_selector, old_idx, LV_BTNMATRIX_CTRL_CHECKED);
      }
    }
  } else if (target == language_selector) {
    const uint32_t sel = lv_btnmatrix_get_selected_btn(language_selector);
    if (sel < g_lang_codes.size()) {
      saved = conf->set_language(g_lang_codes[sel]);
      if (saved)
        create_simple_dialog(lv_scr_act(), ::tr("Language"),
                             ::tr("Restart GUI to apply the language."), true, false);
    }
  }

  if (!saved) {
    create_simple_dialog(lv_scr_act(), ::tr("Unable to Save Setting"),
                         ::tr("The config file could not be updated."), true, true);
  }

  if (target == emergency_prompt_switch) refresh_emergency_stop_warning();
}

void SettingPanel::refresh_emergency_stop_warning() {
  if (lv_obj_has_state(emergency_prompt_switch, LV_STATE_CHECKED)) {
    lv_obj_add_flag(emergency_stop_warning, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(emergency_stop_warning, LV_OBJ_FLAG_HIDDEN);
  }
}

void SettingPanel::refresh_active_tab() {
  const uint16_t idx = lv_tabview_get_tab_act(tabview);
  if (idx == lv_obj_get_index(wifi_tab)) {
    wifi_panel.foreground();
  }
}

void SettingPanel::handle_callback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
      lv_obj_t *btn = lv_event_get_current_target(event);
    if (btn == restart_klipper_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto restart_command = conf->get<std::string>("/commands/restart_klipper_cmd");
      auto ret = call_command(restart_command);
      if (ret != 0) {
        create_simple_dialog(lv_scr_act(), ::tr("Restart Klipper Failed"), ::tr("Failed to restart Klipper!"), true, true);
      }
    } else if (btn == restart_firmware_btn.get_container()) {
      ws.send_jsonrpc("printer.firmware_restart");
    } else if (btn == guppy_restart_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto restart_command = conf->get<std::string>("/commands/gui_restart_cmd");
      auto ret = call_command(restart_command);
      if (ret != 0) {
        create_simple_dialog(lv_scr_act(), ::tr("Restart GUI Failed"), ::tr("Failed to restart GUI!"), true, true);
      }
#ifdef COSMOS
    } else if (btn == update_btn.get_container()) {
      // Moonraker runs the update and reports its progress to every UI.
      update_manager.start();
#endif
    } else if (btn == shutdown_host_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto shutdown_host_cmd = conf->get<std::string>("/commands/shutdown_host_cmd");
      lv_obj_t *mbox = create_simple_dialog(lv_scr_act(), ::tr("Shutdown Host Initiated"), ::tr("Shutdown of host has been initiated"), false, false);
      run_command_deferred(mbox, shutdown_host_cmd,
                           ::tr("Shutdown Host Failed"), ::tr("Failed to shutdown host!"),
                           DEFERRED_COMMAND_DELAY_MS);
    } else if (btn == support_zip_btn.get_container()) {
      Config *conf = Config::get_instance();
      auto support_zip_cmd = conf->get<std::string>("/commands/support_zip_cmd");
      if (support_zip_cmd != "") {
        lv_obj_add_state(support_zip_btn.get_button(), LV_STATE_PRESSED);
        lv_refr_now(NULL);
        auto ret = call_command(support_zip_cmd);
        lv_obj_clear_state(support_zip_btn.get_button(), LV_STATE_PRESSED);
        if (ret == 0) {
          create_simple_dialog(lv_scr_act(), ::tr("Support ZIP Success"), ::tr("The support.zip can be found in the config directory!"), true, false);
        } else {
          create_simple_dialog(lv_scr_act(), ::tr("Support ZIP Failed"), ::tr("Failed to generate a support zip!"), true, true);
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
