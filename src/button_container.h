#ifndef __BUTTON_CONTAINER_H__
#define __BUTTON_CONTAINER_H__

#include "lvgl/lvgl.h"

#include <string>
class ButtonContainer {
 public:
  enum class PromptMode {
    Standard,
    Destructive,
  };

  ButtonContainer(lv_obj_t *parent,
		  const void *btn_img,
		  const char *text,
		  lv_event_cb_t cb,
		  void *user_data,
		  const std::string &prompt_text = {},
		  const PromptMode prompt_mode = PromptMode::Standard);
  ~ButtonContainer();

  lv_obj_t *get_container();
  lv_obj_t *get_button();
  void disable();
  void enable();
  void hide();

  void set_image(const void *img);

  void handle_callback(lv_event_t *event);
  void handle_prompt();
  void handle_prompt_result(lv_event_t *event);
  
  static void _handle_callback(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_callback(event);
  };

  static void _handle_prompt_result(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_prompt_result(event);
  };

 private:
  lv_obj_t *btn_cont;
  lv_obj_t *btn;
  lv_obj_t *label;
  std::string prompt_text;
  PromptMode prompt_mode;
  bool dispatch_confirmed_click = false;
};

#endif // __BUTTON_CONTAINER_H__
