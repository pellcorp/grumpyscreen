#include "i18n.h"
#include "logger.h"
#include "lvgl/lvgl.h"

#include <cstring>
#include <string>
#include <unordered_map>

// The hooks in i18n_hooks.h translate on the way in; the setters are called
// in parentheses here so text that is already final is not looked up again.
namespace {

using Reverse = std::unordered_map<std::string, std::string>;

// the new-language text for what a widget shows now, or null to leave it alone
const char *retranslated(const Reverse &reverse, const char *shown) {
  if (shown == nullptr || shown[0] == '\0') return nullptr;
  const auto english = reverse.find(shown);
  const char *next = I18n::tr(english == reverse.end() ? shown : english->second.c_str());
  return std::strcmp(next, shown) == 0 ? nullptr : next;
}

void retranslate_table(lv_obj_t *obj, const Reverse &reverse) {
  for (uint16_t row = 0; row < lv_table_get_row_cnt(obj); row++) {
    for (uint16_t col = 0; col < lv_table_get_col_cnt(obj); col++) {
      const char *next = retranslated(reverse, lv_table_get_cell_value(obj, row, col));
      if (next != nullptr) (lv_table_set_cell_value)(obj, row, col, next);
    }
  }
}

void retranslate_tabs(lv_obj_t *obj, const Reverse &reverse) {
  lv_obj_t *buttons = lv_tabview_get_tab_btns(obj);
  const uint32_t tabs = lv_obj_get_child_cnt(lv_tabview_get_content(obj));
  for (uint32_t tab = 0; tab < tabs; tab++) {
    const char *next = retranslated(reverse, lv_btnmatrix_get_btn_text(buttons, tab));
    if (next != nullptr) lv_tabview_rename_tab(obj, tab, next);
  }
}

lv_obj_tree_walk_res_t retranslate_obj(lv_obj_t *obj, void *data) {
  const Reverse &reverse = *static_cast<const Reverse *>(data);
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char *next = retranslated(reverse, lv_label_get_text(obj));
    if (next != nullptr) (lv_label_set_text)(obj, next);
  } else if (lv_obj_check_type(obj, &lv_table_class)) {
    retranslate_table(obj, reverse);
  } else if (lv_obj_check_type(obj, &lv_tabview_class)) {
    retranslate_tabs(obj, reverse);
  } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
    const char *next = retranslated(reverse, lv_textarea_get_placeholder_text(obj));
    if (next != nullptr) (lv_textarea_set_placeholder_text)(obj, next);
  } else if (lv_obj_check_type(obj, &lv_btnmatrix_class)) {
    // I18n::load already rewrote the map this points at
    lv_obj_invalidate(obj);
  }
  return LV_OBJ_TREE_WALK_NEXT;
}

}  // namespace

namespace I18n {

bool switch_to(const std::string &code) {
  // the text on screen is in the old language: remember how to read it first
  Reverse reverse;
  for (auto &entry : reverse_table()) reverse.emplace(std::move(entry));

  if (!load(default_dir(), code)) {
    LOG_ERROR("Failed to load language {}", code);
    return false;
  }

  lv_disp_t *disp = lv_disp_get_default();
  for (uint32_t i = 0; i < disp->screen_cnt; i++) {
    lv_obj_tree_walk(disp->screens[i], retranslate_obj, &reverse);
  }
  lv_obj_tree_walk(disp->top_layer, retranslate_obj, &reverse);
  lv_obj_tree_walk(disp->sys_layer, retranslate_obj, &reverse);

  LOG_INFO("Language {} active, {} strings", code, loaded_strings());
  return true;
}

}  // namespace I18n
