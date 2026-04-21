#ifndef __BUTTON_CONTAINER_H__
#define __BUTTON_CONTAINER_H__

#include "lvgl/lvgl.h"

#include <string>
#include <functional>

class ButtonContainer {
 public:
  enum class PromptStyle {
    Standard,
    Destructive,
  };

  struct Action {
    lv_event_cb_t click_cb = nullptr;
    void *user_data = nullptr;
    std::string prompt_text;
    std::function<void()> prompt_callback;
    PromptStyle prompt_style = PromptStyle::Standard;
  };

  ButtonContainer(lv_obj_t *parent,
                  const void *btn_img,
                  const char *text,
                  Action action);
  ~ButtonContainer() = default;

  static Action direct(lv_event_cb_t click_cb, void *user_data);
  static Action confirm(std::string prompt_text,
                        std::function<void()> prompt_callback,
                        PromptStyle prompt_style = PromptStyle::Standard);
  static Action conditional_confirm(bool should_prompt,
                                    std::string prompt_text,
                                    std::function<void()> prompt_callback,
                                    lv_event_cb_t click_cb,
                                    void *user_data,
                                    PromptStyle prompt_style = PromptStyle::Standard);

  lv_obj_t *get_container();
  lv_obj_t *get_button();
  void disable();
  void enable();
  void hide();

  void set_image(const void *img);

  void handle_visual_state(lv_event_t *event);
  void handle_click(lv_event_t *event);
  void handle_prompt();
  void handle_prompt_result(lv_event_t *event);
  
  static void _handle_visual_state(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_visual_state(event);
  };

  static void _handle_click(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_click(event);
  };

  static void _handle_prompt_result(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_prompt_result(event);
  };

 private:
  lv_obj_t *btn_cont;
  lv_obj_t *btn;
  lv_obj_t *label;
  Action action;
};

#endif // __BUTTON_CONTAINER_H__
