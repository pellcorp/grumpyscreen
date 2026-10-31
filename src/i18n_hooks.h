#ifndef __I18N_HOOKS_H__
#define __I18N_HOOKS_H__

// Force-included into every C++ file by the Makefile, so text reaches LVGL in
// the active language without each call site asking. lvgl.h has to come first:
// these macros would otherwise rewrite its declarations. To set text that must
// stay as it is, call the function in parentheses: (lv_label_set_text)(obj, text)
#include "lvgl/lvgl.h"
#include "i18n.h"

#define lv_label_set_text(obj, text) lv_label_set_text((obj), I18n::tr(text))
#define lv_label_set_text_fmt(obj, format, ...) \
  lv_label_set_text_fmt((obj), I18n::tr(format), ##__VA_ARGS__)
#define lv_tabview_add_tab(tv, name) lv_tabview_add_tab((tv), I18n::tr(name))
#define lv_table_set_cell_value(obj, row, col, text) \
  lv_table_set_cell_value((obj), (row), (col), I18n::tr(text))
#define lv_textarea_set_placeholder_text(obj, text) \
  lv_textarea_set_placeholder_text((obj), I18n::tr(text))
#define lv_msgbox_create(parent, title, text, buttons, close) \
  lv_msgbox_create((parent), I18n::tr(title), I18n::tr(text), \
                   (buttons) != nullptr ? I18n::map(buttons) : nullptr, (close))
#define lv_btnmatrix_set_map(obj, btn_map) \
  lv_btnmatrix_set_map((obj), I18n::map(btn_map))

#endif  // __I18N_HOOKS_H__
