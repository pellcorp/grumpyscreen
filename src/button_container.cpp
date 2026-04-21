#include "button_container.h"

#include <utility>

LV_FONT_DECLARE(lv_font_montserrat_22);
ButtonContainer::ButtonContainer(lv_obj_t *parent,
                                 const void *btn_img,
                                 const char *text,
                                 Action button_action)
  : btn_cont(lv_obj_create(parent))
  , btn(lv_imgbtn_create(btn_cont))
  , label(lv_label_create(btn_cont))
  , action(std::move(button_action))
{
  lv_obj_set_style_pad_all(btn_cont, 0, 0);
  auto width_scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
  lv_obj_set_size(btn_cont, 150 * width_scale, LV_SIZE_CONTENT);

  lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_imgbtn_set_src(btn, LV_IMGBTN_STATE_RELEASED, NULL, btn_img, NULL);
  lv_obj_set_width(btn, LV_SIZE_CONTENT);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_visual_state, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_visual_state, LV_EVENT_RELEASED, this);
  if (action.prompt_callback) {
    lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_click, LV_EVENT_CLICKED, this);
  } else if (action.click_cb) {
    lv_obj_add_event_cb(btn_cont, action.click_cb, LV_EVENT_CLICKED, action.user_data);
  }

  lv_label_set_text(label, text);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, lv_palette_darken(LV_PALETTE_GREY, 1), LV_STATE_DISABLED);

  lv_obj_align_to(label, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

ButtonContainer::Action ButtonContainer::direct(lv_event_cb_t click_cb, void *user_data) {
  Action action;
  action.click_cb = click_cb;
  action.user_data = user_data;
  return action;
}

ButtonContainer::Action ButtonContainer::confirm(std::string prompt_text,
                                                 std::function<void()> prompt_callback,
                                                 PromptStyle prompt_style) {
  Action action;
  action.prompt_text = std::move(prompt_text);
  action.prompt_callback = std::move(prompt_callback);
  action.prompt_style = prompt_style;
  return action;
}

ButtonContainer::Action ButtonContainer::conditional_confirm(bool should_prompt,
                                                             std::string prompt_text,
                                                             std::function<void()> prompt_callback,
                                                             lv_event_cb_t click_cb,
                                                             void *user_data,
                                                             PromptStyle prompt_style) {
  if (should_prompt) {
    return confirm(std::move(prompt_text), std::move(prompt_callback), prompt_style);
  }
  return direct(click_cb, user_data);
}

lv_obj_t *ButtonContainer::get_container() {
  return btn_cont;
}

lv_obj_t *ButtonContainer::get_button() {
  return btn;
}

void ButtonContainer::disable() {
  lv_obj_add_state(btn, LV_STATE_DISABLED);
  lv_obj_add_state(btn_cont, LV_STATE_DISABLED);
  lv_obj_add_state(label, LV_STATE_DISABLED);
  
}

void ButtonContainer::enable() {
  lv_obj_clear_state(btn, LV_STATE_DISABLED);
  lv_obj_clear_state(btn_cont, LV_STATE_DISABLED);
  lv_obj_clear_state(label, LV_STATE_DISABLED);
}

void ButtonContainer::hide() {
  lv_obj_add_flag(btn_cont, LV_OBJ_FLAG_HIDDEN);
}

void ButtonContainer::set_image(const void *img) {
  lv_imgbtn_set_src(btn, LV_IMGBTN_STATE_RELEASED, NULL, img, NULL);
}

void ButtonContainer::handle_visual_state(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_imgbtn_set_state(btn, LV_IMGBTN_STATE_PRESSED);
  } else if (code == LV_EVENT_RELEASED) {
    lv_imgbtn_set_state(btn, LV_IMGBTN_STATE_RELEASED);
  }
}

void ButtonContainer::handle_click(lv_event_t *event) {
  LV_UNUSED(event);
  handle_prompt();
}

void ButtonContainer::handle_prompt() {
  static const char *destructive_btns[] = {"Cancel", "Confirm", ""};
  static const char *btns[] = {"Confirm", "Cancel", ""};

  const bool destructive = action.prompt_style == PromptStyle::Destructive;
  lv_obj_t *mbox1 = lv_msgbox_create(NULL, NULL, action.prompt_text.c_str(),
                                    destructive ? destructive_btns : btns, false);
  lv_obj_t *msg = ((lv_msgbox_t*)mbox1)->text;
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(msg, LV_PCT(100));
  lv_obj_center(msg);

  lv_obj_t *btnm = lv_msgbox_get_btns(mbox1);
  lv_btnmatrix_set_btn_ctrl(btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_btnmatrix_set_btn_ctrl(btnm, 1, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);
  
  auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;

  lv_obj_set_size(btnm, LV_PCT(90), 50 *hscale);
  if (destructive) {
#ifdef GUPPY_SMALL_SCREEN
    lv_obj_set_size(mbox1, LV_PCT(95), LV_PCT(65));
    lv_obj_set_style_text_font(mbox1, &lv_font_montserrat_16, LV_STATE_DEFAULT);
#else
    lv_obj_set_size(mbox1, LV_PCT(95), LV_PCT(52));
    lv_obj_set_style_text_font(mbox1, &lv_font_montserrat_22, LV_STATE_DEFAULT);
#endif
  } else {
    lv_obj_set_size(mbox1, LV_PCT(50), LV_PCT(35));
  }
  lv_obj_add_event_cb(mbox1, &ButtonContainer::_handle_prompt_result, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_center(mbox1);
}

void ButtonContainer::handle_prompt_result(lv_event_t *event) {
  lv_obj_t *msgbox = lv_obj_get_parent(lv_event_get_target(event));
  uint32_t clicked_btn = lv_msgbox_get_active_btn(msgbox);
  const bool destructive = action.prompt_style == PromptStyle::Destructive;
  const uint32_t confirm_idx = destructive ? 1 : 0;

  if (clicked_btn == confirm_idx && action.prompt_callback) {
    action.prompt_callback();
  }

  lv_msgbox_close(msgbox);
}
