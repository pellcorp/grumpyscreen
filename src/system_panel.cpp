#include "system_panel.h"

static WifiPanelOptions embedded_wifi_options(lv_obj_t *parent) {
  WifiPanelOptions opts;
  opts.parent = parent;
  opts.flush = true;
  return opts;
}

SystemPanel::SystemPanel(std::mutex &l, lv_obj_t *parent)
  : wifi_panel(l, embedded_wifi_options(parent))
{}

void SystemPanel::foreground() {
  wifi_panel.foreground();
}
