#ifndef SIMPLE_DIALOG_H
#define SIMPLE_DIALOG_H

#include "lvgl.h"
#include "theme.h"

#include <algorithm>
typedef void (*simple_dialog_result_cb_t)(lv_obj_t *mbox, uint32_t button_idx, void *user_data);

struct SimpleDialogOptions {
    const char **buttons = nullptr;
    bool error = false;
    bool auto_close = true;
    bool with_overlay = true;
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

    lv_obj_t *btnm = lv_event_get_current_target(e);
    if (lv_obj_has_state(btnm, LV_STATE_PRESSED) &&
        lv_btnmatrix_get_selected_btn(btnm) == static_cast<int32_t>(dsc->id)) {
        return;
    }

    dsc->rect_dsc->bg_color = Theme::col(Theme::DANGER);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
    dsc->label_dsc->color = Theme::col(Theme::ON_PRIMARY);
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
        lv_obj_add_style(overlay, &Theme::styles().popout, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);      // eat clicks/taps
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_USER_1);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);   // no scroll
        dialog_parent = overlay;
    }

    lv_obj_t * mbox = lv_msgbox_create(dialog_parent, title, message, options.buttons, false);
    lv_obj_center(mbox);
    lv_obj_set_flex_flow(mbox, LV_FLEX_FLOW_COLUMN);
    // the popout box look: surface, hairline, gap()*1.5 padding, capped at the
    // screen minus a gap each side
    lv_obj_add_style(mbox, &Theme::styles().popout_box, 0);
    // The title is a banner across the top of the box, not a panel inset in
    // it, so the box itself pads nothing and the message and buttons below
    // carry their own. Clipped, so the banner's square corners follow the
    // box's rounded ones.
    lv_obj_set_style_pad_all(mbox, 0, 0);
    lv_obj_set_style_pad_row(mbox, 0, 0);
    lv_obj_set_style_pad_bottom(mbox, Theme::gap(), 0);
    lv_obj_set_style_clip_corner(mbox, true, 0);

    const lv_color_t title_bg = options.error
        ? Theme::col(Theme::DANGER)
        : Theme::theme_primary();

    lv_obj_t * title_label = lv_msgbox_get_title(mbox);
    lv_obj_t *content = lv_msgbox_get_content(mbox);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_style_bg_color(title_label, title_bg, 0);
    lv_obj_set_style_bg_opa(title_label, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_label, 0, 0);
    lv_obj_set_style_outline_width(title_label, 0, 0);
    lv_obj_set_style_radius(title_label, 0, 0);
    lv_obj_set_style_pad_ver(title_label, Theme::gap(), 0);
    lv_obj_set_style_pad_hor(title_label, Theme::popout_pad(), 0);
    lv_obj_set_style_text_font(title_label, Theme::scale_font(18), 0);
    // the accent and the danger colour both take the on-primary text
    lv_obj_set_style_text_color(title_label, Theme::col(Theme::ON_PRIMARY), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    // the message sits in its own margin, clear of the banner above it and the
    // buttons below: this is the one thing in the dialog anyone has to read
    const lv_coord_t text_pad = 2 * Theme::gap();
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content, text_pad, 0);
    // the text area grows with the message but never past what the screen can
    // show with the title and buttons still visible: beyond that it scrolls.
    const lv_coord_t button_h = Theme::touch_h();
    lv_obj_set_style_max_height(content,
        Theme::popout_max_h() - Theme::gap()
        - (lv_font_get_line_height(Theme::scale_font(18)) + 2 * Theme::gap())
        - button_h - 2 * text_pad, 0);

    // The box hugs its message: a short question makes a small box with even
    // air round it, a long one wraps at the box's maximum width. Measured up
    // front, because percent-sized children (the banner, the button row)
    // cannot size a content-sized parent.
    // Each of the three rows carries its own side padding, so each one asks for
    // a different box width and the widest ask wins.
    const lv_coord_t box_max_w = std::min(Theme::popout_w(), Theme::scale_w(400));
    const lv_coord_t banner_pad = 2 * Theme::popout_pad();
    lv_point_t text_size, title_size;
    lv_txt_get_size(&text_size, message, Theme::scale_font(16), 0, 0, box_max_w - 2 * text_pad, LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&title_size, title, Theme::scale_font(18), 0, 0, box_max_w - banner_pad, LV_TEXT_FLAG_NONE);

    // Every key in a button matrix is the same width, so the widest label sets
    // how wide the row has to be for none of them to read as cramped.
    lv_coord_t buttons_w = 0;
    if (options.buttons != nullptr) {
        lv_coord_t widest = 0;
        int count = 0;
        for (const char **b = options.buttons; *b != nullptr && (*b)[0] != '\0'; ++b, ++count) {
            lv_point_t s;
            lv_txt_get_size(&s, *b, Theme::scale_font(16), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            widest = std::max(widest, s.x);
        }
        if (count > 0) {
            buttons_w = count * (widest + 4 * Theme::gap()) + (count - 1) * Theme::gap();
        }
    }
    // a few pixels of slack: a box measured to the exact text width still
    // wraps it, since the label is laid out inside the content's own padding
    const lv_coord_t asked = std::max({text_size.x + 2 * text_pad,
                                       title_size.x + banner_pad,
                                       buttons_w + banner_pad});
    const lv_coord_t box_w = std::clamp<lv_coord_t>(asked + Theme::scale_w(8),
                                                    Theme::scale_w(260), box_max_w);  // 260: two buttons

    lv_obj_t * text_label = lv_msgbox_get_text(mbox);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(text_label, LV_PCT(100));

    const bool has_buttons = options.buttons != nullptr;
    if (has_buttons) {
        // the key look comes from the theme callback; only the highlighted
        // button is CHECKED (the draw callback then paints it the danger
        // colour), the rest stay plain keys
        lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
        if (options.highlighted_button_idx >= 0) {
            lv_btnmatrix_set_btn_ctrl(btnm, options.highlighted_button_idx, LV_BTNMATRIX_CTRL_CHECKED);
        }
        // the buttons line up with the message above them
        lv_obj_set_size(btnm, LV_PCT(100), button_h);
        lv_obj_set_style_pad_hor(btnm, Theme::popout_pad(), 0);

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

    // The box is as tall as the message it holds, capped so it always fits.
    // That replaces two hand-tuned sets of height percentages picked by screen
    // size: the text scales, so the box follows it at any resolution and for
    // any amount of text.
    lv_obj_set_size(mbox, box_w, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(mbox, Theme::scale_font(16), LV_STATE_DEFAULT);

    return mbox;
}

static inline lv_obj_t * create_simple_dialog(lv_obj_t * parent, const char * title, const char * message, bool closable, bool error) {
    static const char * btns[] = {"OK", ""};

    SimpleDialogOptions options{};
    options.buttons = closable ? btns : nullptr;
    options.error = error;
    return create_configurable_dialog(parent, title, message, options);
}

#endif // SIMPLE_DIALOG_H
