#pragma once

#include "lvgl/lvgl.h"
#include <array>
#include <functional>
#include <string>

// One top overlay for printer messages. Call with the LVGL lock held.
class NotificationBanner {
public:
  enum class Source { Display, Klipper, Mmu };
  enum class Level { Info, Warning, Error };
  static NotificationBanner &instance();
  void show(Source source, const std::string &text, Level level,
            std::function<void()> dismiss = {}, std::function<void()> reset = {});
  void clear(Source source);
  bool visible(Source source) const;

private:
  struct Message {
    std::string text;
    Level level = Level::Info;
    bool dismissed = false;
    unsigned long order = 0;
    std::function<void()> dismiss, reset;
  };
  void render();
  void dismiss();
  void reset();
  std::array<Message, 3> messages;
  unsigned long sequence = 0;
  int current = -1;
  lv_obj_t *container = nullptr;
  lv_obj_t *label = nullptr;
  lv_obj_t *reset_btn = nullptr;
  lv_obj_t *close_btn = nullptr;
};
