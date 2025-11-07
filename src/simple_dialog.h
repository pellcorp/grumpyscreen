#ifndef SIMPLE_DIALOG_H
#define SIMPLE_DIALOG_H

#include "lvgl.h"
#include "logger.h"
#include <string.h> // strcmp

static inline void simple_dialog_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    lv_obj_t * mbox = lv_event_get_target(e);
    const char * txt = lv_msgbox_get_active_btn_text(mbox);

    if(txt && strcmp(txt, "OK") == 0) {
        lv_obj_t * overlay = lv_obj_get_parent(mbox);
        lv_msgbox_close(mbox);
        if (overlay) {
            lv_obj_del(overlay);
        }
    }
}

static inline lv_obj_t * create_simple_dialog(lv_obj_t * parent, const char * title, const char * message, bool closable) {
    static const char * btns[] = {"OK", ""};

    lv_obj_t * overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);      // eat clicks/taps
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);   // no scroll

    lv_obj_t * mbox = nullptr;
    if (closable) {
        mbox = lv_msgbox_create(overlay, title, message, btns, false);

        lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
        lv_btnmatrix_set_btn_ctrl(btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);
        lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);

        auto hscale = (double)lv_disp_get_physical_ver_res(nullptr) / 480.0;
        lv_obj_set_size(btnm, LV_PCT(90), 50 *hscale);

        lv_obj_add_event_cb(mbox, simple_dialog_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    } else {
        mbox = lv_msgbox_create(overlay, title, message, nullptr, false);
    }
    lv_obj_center(mbox);
    lv_obj_set_flex_flow(mbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(mbox, 12, 0);

    lv_obj_t * title_label = lv_msgbox_get_title(mbox);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t * text_label = lv_msgbox_get_text(mbox);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);

#ifdef GUPPY_SMALL_SCREEN
    if (closable) {
        lv_obj_set_size(mbox, LV_PCT(95), LV_PCT(50));
    } else {
        lv_obj_set_size(mbox, LV_PCT(95), LV_PCT(35));
    }
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_16, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
#else
    if (closable) {
        lv_obj_set_size(mbox, LV_PCT(95), LV_PCT(50));
    } else {
        lv_obj_set_size(mbox, LV_PCT(95), LV_PCT(25));
    }
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
#endif

    return mbox;
}

#endif // SIMPLE_DIALOG_H
