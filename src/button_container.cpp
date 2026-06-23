#include "button_container.h"
#include "simple_dialog.h"

namespace {
  void handle_button_container_dialog_result(lv_obj_t *, uint32_t clicked_btn, void *user_data) {
    ButtonContainer *button_container = static_cast<ButtonContainer *>(user_data);
    button_container->handle_prompt_result(clicked_btn);
  }
}

ButtonContainer::ButtonContainer(lv_obj_t *parent,
				 const void *btn_img,
				 const char *text,
				 lv_event_cb_t cb,
				 void* user_data,
				 const std::string &title,
				 const std::string &prompt,
				 const PromptMode mode,
				 const bool multiline_prompt)
  : btn_cont(lv_obj_create(parent))
  , btn(lv_imgbtn_create(btn_cont))
  , label(lv_label_create(btn_cont))
  , click_cb(cb)
  , click_user_data(user_data)
  , title_text(title)
  , prompt_text(prompt)
  , prompt_mode(mode)
  , prompt_multiline(multiline_prompt)
{
  lv_obj_set_style_pad_all(btn_cont, 0, 0);
  lv_obj_set_style_opa(btn_cont, LV_OPA_50, LV_STATE_DISABLED);
  auto width_scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
  lv_obj_set_size(btn_cont, 150 * width_scale, LV_SIZE_CONTENT);

  lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_imgbtn_set_src(btn, LV_IMGBTN_STATE_RELEASED, NULL, btn_img, NULL);
  lv_obj_set_width(btn, LV_SIZE_CONTENT);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

  if (cb != NULL) {
    lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_callback, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_callback, LV_EVENT_RELEASED, this);
    if (!prompt_text.empty()) {
      lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_callback, LV_EVENT_CLICKED, this);
    }
    lv_obj_add_event_cb(btn_cont, &ButtonContainer::_dispatch_click_callback, LV_EVENT_CLICKED, this);
  }

  lv_label_set_text(label, text);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_GREY), LV_STATE_DISABLED);
  lv_obj_set_style_opa(label, LV_OPA_70, LV_STATE_DISABLED);

  lv_obj_align_to(label, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

ButtonContainer::~ButtonContainer() {
}

lv_obj_t *ButtonContainer::get_container() {
  return btn_cont;
}

lv_obj_t *ButtonContainer::get_button() {
  return btn;
}

void ButtonContainer::apply_disabled_state() {
  const bool disabled = disabled_by_user || disabled_while_executing;
  if (disabled) {
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_obj_add_state(btn_cont, LV_STATE_DISABLED);
    lv_obj_add_state(label, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(btn, LV_STATE_DISABLED);
    lv_obj_clear_state(btn_cont, LV_STATE_DISABLED);
    lv_obj_clear_state(label, LV_STATE_DISABLED);
  }
}

void ButtonContainer::disable() {
  disabled_by_user = true;
  apply_disabled_state();
}

void ButtonContainer::enable() {
  disabled_by_user = false;
  apply_disabled_state();
}

void ButtonContainer::hide() {
  lv_obj_add_flag(btn_cont, LV_OBJ_FLAG_HIDDEN);
}

void ButtonContainer::set_image(const void *img) {
  lv_imgbtn_set_src(btn, LV_IMGBTN_STATE_RELEASED, NULL, img, NULL);
}

void ButtonContainer::handle_callback(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_imgbtn_set_state(btn, LV_IMGBTN_STATE_PRESSED);
  } else if (code == LV_EVENT_RELEASED) {
    lv_imgbtn_set_state(btn, LV_IMGBTN_STATE_RELEASED);
  } else if (code == LV_EVENT_CLICKED && !dispatch_confirmed_click) {
    lv_event_stop_processing(e);
    handle_prompt();
  }
}

void ButtonContainer::dispatch_click(lv_event_t *event) {
  if (click_cb == NULL) {
    return;
  }

  disabled_while_executing = true;
  apply_disabled_state();
  lv_event_t click_event = *event;
  click_event.user_data = click_user_data;
  click_cb(&click_event);
  disabled_while_executing = false;
  apply_disabled_state();
}

void ButtonContainer::handle_prompt() {
  static const char *destructive_btns[] = {"Cancel", "Confirm", ""};
  static const char *btns[] = {"Confirm", "Cancel", ""};

  const bool destructive = prompt_mode == PromptMode::Destructive;
  SimpleDialogOptions options{};
  options.buttons = destructive ? destructive_btns : btns;
  options.error = destructive;
  options.highlighted_button_idx = destructive ? 1 : -1;
  options.multiline_message = prompt_multiline;
  options.result_cb = handle_button_container_dialog_result;
  options.user_data = this;
  create_configurable_dialog(lv_scr_act(), title_text.c_str(), prompt_text.c_str(), options);
}

void ButtonContainer::handle_prompt_result(uint32_t clicked_btn) {
  const bool destructive = prompt_mode == PromptMode::Destructive;
  const uint32_t confirm_idx = destructive ? 1 : 0;

  if (clicked_btn == confirm_idx) {
    dispatch_confirmed_click = true;
    lv_event_send(btn_cont, LV_EVENT_CLICKED, NULL);
    dispatch_confirmed_click = false;
  }
}
