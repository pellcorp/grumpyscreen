#include "notification_banner.h"
#include "theme.h"
#include <utility>

using namespace Theme;

NotificationBanner &NotificationBanner::instance() {
  static NotificationBanner banner;
  return banner;
}

void NotificationBanner::show(Source source, const std::string &text, Level level,
                              std::function<void()> dismiss, std::function<void()> reset) {
  if (text.empty()) { clear(source); return; }
  auto &message = messages[static_cast<int>(source)];
  const bool changed = message.text != text || message.level != level ||
                       bool(message.reset) != bool(reset);
  message.dismiss = std::move(dismiss);
  message.reset = std::move(reset);
  if (!changed) return; // status ticks must not reopen dismissed messages
  message.text = text;
  message.level = level;
  message.dismissed = false;
  message.order = ++sequence;
  render();
}

void NotificationBanner::clear(Source source) {
  auto &message = messages[static_cast<int>(source)];
  if (message.text.empty()) return;
  message = Message{};
  render();
}

bool NotificationBanner::visible(Source source) const {
  const auto &message = messages[static_cast<int>(source)];
  return !message.text.empty() && !message.dismissed;
}

void NotificationBanner::dismiss() {
  if (current < 0) return;
  auto callback = messages[current].dismiss;
  messages[current].dismissed = true;
  render();
  if (callback) callback();
}

void NotificationBanner::reset() {
  if (current < 0) return;
  auto callback = messages[current].reset;
  if (callback) callback();
}

void NotificationBanner::render() {
  current = -1;
  for (size_t i = 0; i < messages.size(); ++i) {
    const auto &message = messages[i];
    if (message.text.empty() || message.dismissed) continue;
    if (current < 0 || message.level > messages[current].level ||
        (message.level == messages[current].level && message.order > messages[current].order)) {
      current = static_cast<int>(i);
    }
  }
  if (current < 0) {
    if (container) lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!container) {
    container = lv_obj_create(lv_layer_top());
    lv_obj_set_size(container, lv_pct(96), LV_SIZE_CONTENT);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, scale_r(4));
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, scale_r(8), 0);
    lv_obj_set_style_pad_hor(container, scale_r(12), 0);
    lv_obj_set_style_pad_ver(container, scale_r(8), 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    label = lv_label_create(container);
    lv_obj_set_width(label, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    // Both actions fit a text line; the touch area extends into the padding.
    const auto height = lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
    auto button = [&](const char *text, int width) {
      lv_obj_t *btn = lv_btn_create(container);
      lv_obj_set_size(btn, width, height);
      lv_obj_set_ext_click_area(btn, scale_r(4));
      lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      lv_obj_set_style_shadow_width(btn, 0, 0);
      lv_obj_set_style_pad_all(btn, 0, 0);
      lv_obj_t *text_label = lv_label_create(btn);
      lv_label_set_text(text_label, text);
      lv_obj_center(text_label);
      return btn;
    };
    reset_btn = button("Reset", scale_r(60));
    close_btn = button(LV_SYMBOL_CLOSE, scale_r(36));
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
      static_cast<NotificationBanner *>(e->user_data)->dismiss();
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(reset_btn, [](lv_event_t *e) {
      static_cast<NotificationBanner *>(e->user_data)->reset();
    }, LV_EVENT_CLICKED, this);
  }
  const auto &message = messages[current];
  lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(container);
  lv_obj_set_style_bg_color(container, col(message.level == Level::Error ? DANGER :
                                         message.level == Level::Warning ? WARNING : TEXT), 0);
  const auto foreground = col(message.level == Level::Info ? BG : ON_PRIMARY);
  lv_obj_set_style_text_color(label, foreground, 0);
  lv_obj_set_style_text_color(close_btn, foreground, 0);
  lv_obj_set_style_text_color(reset_btn, foreground, 0);
  lv_label_set_text(label, message.text.c_str());
  if (message.reset) lv_obj_clear_flag(reset_btn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(reset_btn, LV_OBJ_FLAG_HIDDEN);
}
