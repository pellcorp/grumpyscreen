#include "system_panel.h"
#include "theme.h"

static WifiPanelOptions embedded_wifi_options(lv_obj_t *parent) {
  WifiPanelOptions opts;
  opts.parent = parent;
  opts.show_refresh_button = false;
  opts.list_grow = 3;
  opts.detail_grow = 2;
  opts.flush = true;
  return opts;
}

SystemPanel::SystemPanel(std::mutex &l, lv_obj_t *parent)
  : owns_cont(parent == nullptr)
  , cont(Theme::create_screen(parent))  // fills the tab: it is the page
  , tabview(lv_tabview_create(cont, LV_DIR_TOP, Theme::scale_r(36)))
  , network_tab(lv_tabview_add_tab(tabview, "WiFi"))
  , info_tab(lv_tabview_add_tab(tabview, "Info"))
  , wifi_panel(l, embedded_wifi_options(network_tab))
  , sysinfo_panel(info_tab)
{
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
  lv_obj_add_event_cb(tabview, &SystemPanel::_tabview_event_cb,
                      LV_EVENT_VALUE_CHANGED, this);
  lv_obj_set_style_pad_all(network_tab, 0, 0);
  lv_obj_set_style_pad_all(info_tab, 0, 0);
  Theme::style_embedded_tabview(tabview);
}

SystemPanel::~SystemPanel() {
  if (owns_cont && cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void SystemPanel::foreground() {
  refresh_active_tab();
}

void SystemPanel::_tabview_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    static_cast<SystemPanel *>(lv_event_get_user_data(event))->refresh_active_tab();
  }
}

void SystemPanel::refresh_active_tab() {
  const uint16_t idx = lv_tabview_get_tab_act(tabview);
  if (idx == lv_obj_get_index(network_tab)) {
    wifi_panel.foreground();
  } else if (idx == lv_obj_get_index(info_tab)) {
    sysinfo_panel.foreground();
  }
}
