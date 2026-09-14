#include "prompt_panel.h"
#include "simple_dialog.h"
#include "logger.h"

PromptPanel::PromptPanel(KWebSocketClient &websocket_client, std::mutex &lock)
    : NotifyConsumer(lock)
    , ws(websocket_client)
{
  ws.register_notify_update(this);
  ws.register_method_callback("notify_gcode_response", "MainPanel",[this](json& d) { this->handle_macro_response(d); });
}

PromptPanel::~PromptPanel() {
  close();
  ws.unregister_notify_update(this);
}

void PromptPanel::consume(json &j) {
}

void PromptPanel::close() {
  if (mbox != NULL) simple_dialog_close(mbox);
  mbox = NULL;
}

void PromptPanel::on_button(uint32_t idx) {
  mbox = NULL; // the dialog closes itself on a tap
  if (idx < commands.size() && !commands[idx].empty()) ws.gcode_script(commands[idx]);
}

void PromptPanel::show() {
  close();
  // a trailing row break would give the matrix an empty row
  while (!labels.empty() && labels.back() == "\n") labels.pop_back();
  button_map.clear();
  for (const auto &l : labels) button_map.push_back(l.c_str());
  button_map.push_back("");

  SimpleDialogOptions options{};
  options.buttons = labels.empty() ? nullptr : button_map.data();
  options.highlighted_button_idx = danger_idx;
  options.result_cb = _on_button;
  options.user_data = this;
  mbox = create_configurable_dialog(lv_scr_act(), title.c_str(), text.c_str(), options);
}

void PromptPanel::handle_macro_response(json &j) {
  auto &v = j["/params/0"_json_pointer];
  if (!v.is_string()) return;
  const std::string resp = v.template get<std::string>();
  if (resp.rfind("// action:prompt_", 0) != 0) return;
  std::lock_guard<std::mutex> lock(lv_lock);
  const std::string command = resp.substr(10);
  LOG_DEBUG("prompt action: {}", command);

  if (command.rfind("prompt_begin", 0) == 0) {
    title = command.size() > 13 ? command.substr(13) : "";
    text.clear();
    labels.clear();
    commands.clear();
    danger_idx = -1;
  } else if (command.rfind("prompt_text", 0) == 0) {
    if (!text.empty()) text += "\n";
    text += command.substr(12);
  } else if (command.rfind("prompt_button_group_start", 0) == 0 ||
             command.rfind("prompt_button_group_end", 0) == 0) {
    // groups are rows; a break with no keys before it is dropped at show time
    if (!labels.empty() && labels.back() != "\n") labels.push_back("\n");
  } else if (command.rfind("prompt_footer_button", 0) == 0 || command.rfind("prompt_button", 0) == 0) {
    // "<label>|<gcode>|<style>", gcode and style optional
    const size_t start = command.find("button") + 6;
    const size_t bar1 = command.find('|', start);
    const size_t bar2 = bar1 == std::string::npos ? bar1 : command.find('|', bar1 + 1);
    const std::string label = command.substr(start, bar1 == std::string::npos ? std::string::npos : bar1 - start);
    const std::string gcode = bar1 == std::string::npos ? "" : command.substr(bar1 + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar1 - 1);
    const std::string style = bar2 == std::string::npos ? "" : command.substr(bar2 + 1);
    // footer buttons make their own row, whatever group was open
    if (command.rfind("prompt_footer_button", 0) == 0 && !labels.empty() && labels.back() != "\n") labels.push_back("\n");
    if (style == "error" && danger_idx < 0) danger_idx = (int)commands.size();
    labels.push_back(label);
    commands.push_back(gcode);
  } else if (command.rfind("prompt_show", 0) == 0) {
    show();
  } else if (command.rfind("prompt_end", 0) == 0) {
    close();
  } else {
    LOG_DEBUG("prompt action {} not supported", command);
  }
}
