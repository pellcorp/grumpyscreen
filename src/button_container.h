#ifndef __BUTTON_CONTAINER_H__
#define __BUTTON_CONTAINER_H__

#include "lvgl/lvgl.h"

#include <array>
#include <string>
class ButtonContainer {
 public:
  ButtonContainer(lv_obj_t *parent,
		  const void *btn_img,
		  const char *text,
		  lv_event_cb_t cb,
		  void *user_data,
		  const std::string &title_text = {},
		  const std::string &prompt_text = {},
		  const std::array<std::string, 2> &prompt_buttons = {},
		  const bool prompt_multiline = false);
  ~ButtonContainer();

  lv_obj_t *get_container();
  lv_obj_t *get_button();
  void disable();
  void enable();
  void hide();
  // Keep the pressed visual visible and ignore input until the delay expires.
  bool start_pressed_transition(uint32_t duration_ms);

  void set_image(const void *img);

  void handle_callback(lv_event_t *event);
  void handle_prompt();
  void handle_prompt_result(uint32_t clicked_btn);
  
  static void _handle_callback(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_callback(event);
  };

 private:
  lv_obj_t *btn_cont;
  lv_obj_t *btn;
  lv_obj_t *label;
  std::string title_text;
  std::string prompt_text;
  std::array<std::string, 2> prompt_buttons;
  std::array<const char *, 3> prompt_button_map;
  bool prompt_multiline;
  bool dispatch_confirmed_click = false;
  lv_timer_t *pressed_transition_timer = nullptr;

  static void _handle_pressed_transition_timer(lv_timer_t *timer);
};

#endif // __BUTTON_CONTAINER_H__
