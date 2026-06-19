#ifndef SIMPLE_DIALOG_H
#define SIMPLE_DIALOG_H

#include "lvgl.h"
typedef void (*simple_dialog_result_cb_t)(lv_obj_t *mbox, uint32_t button_idx, void *user_data);

struct SimpleDialogOptions {
    const char **buttons = nullptr;
    bool error = false;
    bool auto_close = true;
    bool with_overlay = true;
    bool multiline_message = false;
    int32_t highlighted_button_idx = -1;
    simple_dialog_result_cb_t result_cb = nullptr;
    void *user_data = nullptr;
};

struct SimpleDialogContext {
    simple_dialog_result_cb_t result_cb;
    void *user_data;
    bool auto_close;
    int32_t highlighted_button_idx;
};

static inline void simple_dialog_btnm_draw_part_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_PART_BEGIN) return;

    auto *ctx = static_cast<SimpleDialogContext *>(lv_event_get_user_data(e));
    if (ctx == nullptr || ctx->highlighted_button_idx < 0) return;

    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc == nullptr || dsc->part != LV_PART_ITEMS) return;
    if (static_cast<int32_t>(dsc->id) != ctx->highlighted_button_idx) return;

    dsc->rect_dsc->bg_color = lv_palette_main(LV_PALETTE_RED);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
    dsc->label_dsc->color = lv_color_white();
}

static inline bool simple_dialog_has_overlay(lv_obj_t *overlay) {
    if (overlay == nullptr) return false;
    return lv_obj_has_flag(overlay, LV_OBJ_FLAG_USER_1);
}

static inline void simple_dialog_close(lv_obj_t * mbox) {
    lv_obj_t * overlay = lv_obj_get_parent(mbox);

    if (simple_dialog_has_overlay(overlay)) {
        lv_obj_del_async(overlay);   // kills mbox as a child too
    } else {
        lv_obj_del_async(mbox);
    }
}

static inline void simple_dialog_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    lv_obj_t * mbox = lv_event_get_current_target(e);
    if (mbox == NULL) return;

    lv_obj_t * btnm = lv_event_get_target(e);
    int32_t id = lv_btnmatrix_get_selected_btn(btnm);
    if (id < 0) return;

    SimpleDialogContext *ctx = static_cast<SimpleDialogContext *>(lv_event_get_user_data(e));
    if (ctx != nullptr && ctx->result_cb != nullptr) {
        ctx->result_cb(mbox, static_cast<uint32_t>(id), ctx->user_data);
    }

    if (ctx == nullptr || ctx->auto_close) {
        simple_dialog_close(mbox);
    }
}

static inline lv_obj_t * create_configurable_dialog(lv_obj_t * parent,
                                                    const char * title,
                                                    const char * message,
                                                    const SimpleDialogOptions &options = {}) {
    lv_obj_t *dialog_parent = parent;
    if (options.with_overlay) {
        lv_obj_t * overlay = lv_obj_create(parent);
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);      // eat clicks/taps
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_USER_1);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);   // no scroll
        dialog_parent = overlay;
    }

    lv_obj_t * mbox = lv_msgbox_create(dialog_parent, title, message, options.buttons, false);
    lv_obj_center(mbox);
    lv_obj_set_flex_flow(mbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(mbox, 0, 0);
    lv_obj_set_style_pad_row(mbox, 0, 0);

    const lv_color_t title_bg = options.error
        ? lv_palette_main(LV_PALETTE_RED)
        : lv_theme_get_color_primary(mbox);

    lv_obj_t * title_label = lv_msgbox_get_title(mbox);
    lv_obj_t *content = lv_msgbox_get_content(mbox);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_style_bg_color(title_label, title_bg, 0);
    lv_obj_set_style_bg_opa(title_label, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_label, 0, 0);
    lv_obj_set_style_outline_width(title_label, 0, 0);
    lv_obj_set_style_radius(title_label, 0, 0);
    lv_obj_set_style_pad_top(title_label, 10, 0);
    lv_obj_set_style_pad_bottom(title_label, 10, 0);
    lv_obj_set_style_pad_left(title_label, 12, 0);
    lv_obj_set_style_pad_right(title_label, 12, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(content, 12, 0);
    lv_obj_set_style_pad_bottom(content, 12, 0);
    lv_obj_set_style_pad_left(content, 12, 0);
    lv_obj_set_style_pad_right(content, 12, 0);

    lv_obj_t * text_label = lv_msgbox_get_text(mbox);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(text_label, LV_PCT(100));

    const bool has_buttons = options.buttons != nullptr;
    if (has_buttons) {
        constexpr lv_coord_t button_bottom_padding = 8;
        lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
        uint32_t btn_count = 0;
        while (options.buttons[btn_count] != nullptr && options.buttons[btn_count][0] != '\0') {
            ++btn_count;
        }
        for (uint32_t i = 0; i < btn_count; ++i) {
            lv_btnmatrix_set_btn_ctrl(btnm, i, LV_BTNMATRIX_CTRL_CHECKED);
        }
        lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(btnm, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(btnm, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btnm, 0, LV_PART_MAIN);
        lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, -button_bottom_padding);

        auto hscale = (double)lv_disp_get_physical_ver_res(nullptr) / 480.0;
        lv_obj_set_size(btnm, LV_PCT(90), 50 * hscale);

        SimpleDialogContext *ctx = new SimpleDialogContext{
            options.result_cb,
            options.user_data,
            options.auto_close,
            options.highlighted_button_idx,
        };
        lv_obj_add_event_cb(mbox, simple_dialog_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
        lv_obj_add_event_cb(btnm, simple_dialog_btnm_draw_part_cb, LV_EVENT_DRAW_PART_BEGIN, ctx);
        lv_obj_add_event_cb(
            mbox,
            [](lv_event_t *e) {
                if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
                auto *ctx = static_cast<SimpleDialogContext *>(lv_event_get_user_data(e));
                delete ctx;
            },
            LV_EVENT_DELETE,
            ctx);
    }

#ifdef GUPPY_SMALL_SCREEN
    if (has_buttons) {
        lv_obj_set_size(mbox, LV_PCT(95), options.multiline_message ? LV_PCT(65) : LV_PCT(50));
    } else {
        lv_obj_set_size(mbox, LV_PCT(95), options.multiline_message ? LV_PCT(50) : LV_PCT(35));
    }
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_18, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
#else
    if (has_buttons) {
        lv_obj_set_size(mbox, LV_PCT(95), options.multiline_message ? LV_PCT(52) : LV_PCT(40));
    } else {
        lv_obj_set_size(mbox, LV_PCT(95), options.multiline_message ? LV_PCT(35) : LV_PCT(25));
    }
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_22, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
#endif

    return mbox;
}

static inline lv_obj_t * create_simple_dialog(lv_obj_t * parent, const char * title, const char * message, bool closable, bool error) {
    static const char * btns[] = {"OK", ""};

    SimpleDialogOptions options{};
    options.buttons = closable ? btns : nullptr;
    options.error = error;
    options.highlighted_button_idx = (closable && error) ? 0 : -1;
    return create_configurable_dialog(parent, title, message, options);
}

#endif // SIMPLE_DIALOG_H
