#include "tool_map_panel.h"
#include "mmu_panel.h"
#include "mmu_tools.h"
#include "theme.h"
#include "icons.h"
#include "logger.h"

#include <algorithm>

using namespace Theme;

// header plus four rows fit; more scroll
static const int VISIBLE_ROWS = 5;
// the spoolman panel's tints
static const lv_opa_t HEADER_TINT = LV_OPA_20;
static const lv_opa_t ZEBRA_TINT = LV_OPA_40;
static const lv_opa_t PRESSED_TINT = LV_OPA_40;

enum Col { STATUS, TOOL, FILE_MAT, REQ, FILE_DOT, TO, SLOT_DOT, LANE, SLOT_MAT, AVAIL, COLS };

static int dot_d() { return scale_r(18); }

static std::string weight_text(double grams) {
  if (grams < 10) return fmt::format("{:.1f}g", grams);
  if (grams < 1000) return fmt::format("{:.0f}g", grams);
  return fmt::format("{:.2f}kg", grams / 1000);
}

ToolMapPanel::~ToolMapPanel() {
  if (picker != NULL) lv_obj_del(picker);
  back_btn.reset();
  print_btn.reset();
  if (cont != NULL) lv_obj_del(cont);
}

void ToolMapPanel::create() {
  if (cont != NULL) return;

  // laid out like the spoolman panel: the table, then a row of controls
  cont = create_screen(NULL);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  table = lv_table_create(cont);
  lv_obj_set_width(table, LV_PCT(100));
  lv_obj_set_flex_grow(table, 1);
  manage_scroll(table);
  lv_obj_add_event_cb(table, &ToolMapPanel::_handle_table, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(table, &ToolMapPanel::_handle_table, LV_EVENT_DRAW_PART_BEGIN, this);
  lv_obj_add_event_cb(table, &ToolMapPanel::_handle_table, LV_EVENT_DRAW_PART_END, this);

  lv_obj_t *controls = create_row(cont);
  lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  title = lv_label_create(controls);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, 1);
  lv_obj_set_flex_grow(title, 1);
  lv_obj_set_style_pad_left(title, gap(), 0);

  print_btn = std::make_unique<ButtonContainer>(controls, Icons::PRINT, "Print",
                                                &ToolMapPanel::_handle_action, this);
  back_btn = std::make_unique<ButtonContainer>(controls, Icons::BACK, "Back",
                                               &ToolMapPanel::_handle_action, this);
  for (ButtonContainer *b : {back_btn.get(), print_btn.get()}) {
    b->use_plain();
    b->hide_label();
    lv_obj_set_size(b->get_container(), touch_h(), touch_h());
  }

  lv_table_set_col_cnt(table, COLS);
  lv_table_set_row_cnt(table, 1);
  const char *headers[COLS] = {"", "Tool", "File", "Req.", "", "", "", "Lane", "Printer", "Avail."};
  for (uint16_t c = 0; c < COLS; c++) {
    lv_table_set_cell_value(table, 0, c, headers[c]);
    lv_table_add_cell_ctrl(table, 0, c, LV_TABLE_CELL_CTRL_TEXT_CROP);
  }

  // row height: the table split into VISIBLE_ROWS; column widths: fixed for
  // marks, circles and figures, the rest shared by the three names
  lv_obj_update_layout(cont);
  const lv_font_t *font = lv_obj_get_style_text_font(table, LV_PART_ITEMS);
  const int row_h = lv_obj_get_content_height(table) / VISIBLE_ROWS;
  lv_obj_set_style_pad_ver(table, std::max(0, (row_h - lv_font_get_line_height(font)) / 2), LV_PART_ITEMS);

  const int pad = lv_obj_get_style_pad_left(table, LV_PART_ITEMS) +
                  lv_obj_get_style_pad_right(table, LV_PART_ITEMS);
  const int widths[COLS] = {scale_w(24), scale_w(46), 0, scale_w(50), dot_d() + pad,
                            scale_w(30), dot_d() + pad, 0, 0, scale_w(56)};
  int remain = lv_obj_get_content_width(table);
  for (int w : widths) remain -= w;
  for (uint16_t c = 0; c < COLS; c++) lv_table_set_col_width(table, c, widths[c]);
  lv_table_set_col_width(table, FILE_MAT, remain * 30 / 100);
  lv_table_set_col_width(table, LANE, remain * 38 / 100);
  lv_table_set_col_width(table, SLOT_MAT, remain - remain * 30 / 100 - remain * 38 / 100);
}

void ToolMapPanel::open(MmuBackend *b, const std::vector<MmuFileTool> &t,
                        const std::string &filename, std::function<void()> p) {
  create();
  backend = b;
  tools = t;
  print = p;
  want.clear();
  for (const MmuFileTool &tool : tools) want.push_back(mmu_slot_for_tool(backend->slots, tool.tool));
  lv_label_set_text(title, fmt::format(I18n::tr("Preparing {}"), filename).c_str());
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
  lv_obj_scroll_to_y(table, 0, LV_ANIM_OFF);
  update();
}

void ToolMapPanel::close() {
  close_picker();
  backend = NULL;
  print = nullptr;
  if (cont != NULL) {
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
}

bool ToolMapPanel::is_open() const {
  return cont != NULL && !lv_obj_has_flag(cont, LV_OBJ_FLAG_HIDDEN);
}

// a slot already picked for another row: shared where the vendor allows it,
// else the rows swap so each slot feeds one tool
void ToolMapPanel::assign(size_t t, int slot) {
  if (!backend->can_share_slot()) {
    for (size_t j = 0; j < want.size(); j++) {
      if (j != t && want[j] == slot) want[j] = want[t];
    }
  }
  want[t] = slot;
}

// Red cross (blocks Print): no slot, or no filament in it. Amber warning:
// another material, or a colour past MMU_COLOUR_MISMATCH.
void ToolMapPanel::update() {
  if (!is_open() || backend == NULL) return;
  const std::vector<MmuSlot> &slots = backend->slots;

  lv_table_set_row_cnt(table, tools.size() + 1);
  paint.assign(tools.size(), RowPaint());
  bool blocked = false;
  for (size_t t = 0; t < tools.size(); t++) {
    const MmuFileTool &tool = tools[t];
    const uint16_t row = t + 1;
    const MmuSlot *slot = want[t] >= 0 && (size_t)want[t] < slots.size() ? &slots[want[t]] : NULL;
    RowPaint &p = paint[t];

    const bool missing = slot == NULL || !mmu_has_filament(*slot);
    p.other_material = !missing && !tool.material.empty() && !slot->material.empty() &&
                       !mmu_same_material(tool.material, slot->material);
    const bool other_colour = !missing &&
                              mmu_colour_distance(tool.colour, slot->colour) > MMU_COLOUR_MISMATCH;
    p.status = missing ? Status::Blocked : p.other_material || other_colour ? Status::Warn : Status::Ok;
    p.file_colour = tool.colour;
    p.slot_colour = slot != NULL ? slot->colour : "";
    p.slot_filled = !missing;
    p.unconfigured = slot == NULL || slot->material.empty();
    blocked = blocked || missing;

    const bool weighed = backend->spoolman && slot != NULL && slot->weight > 0;  // as on the MMU tab
    const std::string cells[COLS] = {
      p.status == Status::Blocked ? LV_SYMBOL_CLOSE : p.status == Status::Warn ? LV_SYMBOL_WARNING : LV_SYMBOL_OK,
      fmt::format("T{}", tool.tool), tool.material, weight_text(tool.grams), "", "to", "",
      slot != NULL ? slot->name : "None", p.unconfigured ? "Empty" : slot->material,
      weighed ? weight_text(slot->weight) : "n/a",
    };
    for (uint16_t c = 0; c < COLS; c++) {
      lv_table_set_cell_value(table, row, c, cells[c].c_str());
      lv_table_add_cell_ctrl(table, row, c, LV_TABLE_CELL_CTRL_TEXT_CROP);
    }
  }
  refresh_scroll(table);

  if (blocked) print_btn->disable();
  else print_btn->enable();

  if (picking >= 0) show_picker(picking);  // slots changed under an open picker
}

void ToolMapPanel::handle_table(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    uint16_t row, col;
    lv_table_get_selected_cell(table, &row, &col);
    if (row != LV_TABLE_CELL_NONE && row > 0) show_picker(row - 1);
    return;
  }

  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_ITEMS) return;
  const uint32_t row = dsc->id / COLS;
  const uint32_t column = dsc->id % COLS;
  const RowPaint *p = row > 0 && row <= paint.size() ? &paint[row - 1] : NULL;

  if (code == LV_EVENT_DRAW_PART_END) {
    // the filament colours as circles; a bare ring when unknown
    if (p == NULL || (column != FILE_DOT && column != SLOT_DOT)) return;
    const bool file = column == FILE_DOT;
    lv_color_t c;
    const bool valid = parse_colour(file ? p->file_colour : p->slot_colour, &c);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius = LV_RADIUS_CIRCLE;
    rd.bg_color = c;
    rd.bg_opa = !valid ? LV_OPA_TRANSP : (file || p->slot_filled) ? LV_OPA_COVER : LV_OPA_50;
    rd.border_width = scale_r(2);
    rd.border_color = col(BORDER_DIM);
    const int d = dot_d();
    lv_area_t a;
    a.x1 = (dsc->draw_area->x1 + dsc->draw_area->x2 - d) / 2;
    a.y1 = (dsc->draw_area->y1 + dsc->draw_area->y2 - d) / 2;
    a.x2 = a.x1 + d - 1;
    a.y2 = a.y1 + d - 1;
    lv_draw_rect(dsc->draw_ctx, &rd, &a);
    return;
  }

  if (column == STATUS || column == FILE_DOT || column == TO || column == SLOT_DOT) {
    dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
  }
  if (row == 0) {
    dsc->rect_dsc->bg_color = lv_color_mix(theme_primary(), dsc->rect_dsc->bg_color, HEADER_TINT);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  }
  if (row % 2 == 0) {
    dsc->rect_dsc->bg_color = lv_color_mix(col(RAISED), dsc->rect_dsc->bg_color, ZEBRA_TINT);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  }
  if (p == NULL) return;

  // the table presses a cell; show the whole row pressed
  uint16_t pressed_row, pressed_col;
  lv_table_get_selected_cell(table, &pressed_row, &pressed_col);
  if (pressed_row == row && lv_obj_has_state(table, LV_STATE_PRESSED) &&
      !lv_obj_has_state(table, LV_STATE_SCROLLED)) {
    dsc->rect_dsc->bg_color = lv_color_mix(theme_primary(), dsc->rect_dsc->bg_color, PRESSED_TINT);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  }

  if (column == STATUS) {
    dsc->label_dsc->color = col(p->status == Status::Blocked ? DANGER
                                : p->status == Status::Warn ? WARNING : TEXT_DIM);
  } else if (column == TO) {
    dsc->label_dsc->color = col(TEXT_DIM);
  } else if (column == SLOT_MAT && p->unconfigured) {
    dsc->label_dsc->color = col(DANGER);
  } else if ((column == FILE_MAT || column == SLOT_MAT) && p->other_material) {
    dsc->label_dsc->color = col(WARNING);
  }
}

void ToolMapPanel::show_picker(size_t t) {
  if (backend == NULL || t >= tools.size()) return close_picker();
  const MmuFileTool &tool = tools[t];
  const std::vector<MmuSlot> &slots = backend->slots;

  if (picker == NULL) {
    picker_box = create_popout(&picker, &ToolMapPanel::_handle_close_picker, this);
    lv_obj_set_flex_flow(picker_box, LV_FLEX_FLOW_ROW_WRAP);
  }
  lv_obj_clean(picker_box);
  picking = (int)t;

  lv_obj_t *head = lv_label_create(picker_box);
  lv_obj_set_width(head, LV_PCT(100));
  lv_obj_set_style_text_font(head, scale_font(14), 0);

  const bool mappable = backend->can_map_tool(tool.tool);
  if (!mappable) {
    lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
    lv_label_set_text(head, fmt::format(I18n::tr("T{} can't be mapped on this printer"), tool.tool).c_str());
  } else {
    lv_label_set_text(head, fmt::format(I18n::tr("T{} {} from:"), tool.tool, tool.material).c_str());
    // four across, as in the backup picker; more slots scroll the popout
    const int cols = std::max(1, std::min((int)slots.size(), 4));
    const int tile_w = (popout_row_w() - (cols - 1) * gap()) / cols;
    for (size_t i = 0; i < slots.size(); i++) {
      lv_obj_t *b = create_slot_tile(picker_box, slots[i], i, tile_w, &ToolMapPanel::_handle_pick, this, true);
      lv_obj_set_user_data(b, (void*)(intptr_t)i);
      if ((int)i == want[t]) {
        lv_obj_set_style_border_color(b, theme_primary(), 0);
        lv_obj_set_style_border_width(b, scale_r(2), 0);
      }
    }
  }

  lv_obj_t *cancel = create_text_btn(picker_box, mappable ? "Cancel" : "Close",
                                     &ToolMapPanel::_handle_close_picker, this);
  lv_obj_set_size(cancel, LV_PCT(100), scale_r(40));
  lv_obj_scroll_to_y(picker_box, 0, LV_ANIM_OFF);
  show_popout(picker);
}

void ToolMapPanel::pick(lv_event_t *e) {
  const int slot = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
  const int t = picking;
  close_picker();
  if (backend == NULL || t < 0) return;
  assign(t, slot);
  update();
}

void ToolMapPanel::close_picker() {
  picking = -1;
  hide_popout(picker);
}

void ToolMapPanel::handle_action(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (backend == NULL) return;
  if (btn == back_btn->get_container()) {
    close();
  } else if (btn == print_btn->get_container() && !lv_obj_has_state(btn, LV_STATE_DISABLED)) {
    // one map command per changed tool, queued ahead of the print. In file
    // order is safe where the vendor swaps (AFC): no two rows share a slot
    for (size_t t = 0; t < tools.size(); t++) {
      const int tool = tools[t].tool;
      if (want[t] >= 0 && want[t] != mmu_slot_for_tool(backend->slots, tool) && backend->can_map_tool(tool)) {
        backend->map_tool(tool, want[t]);
      }
    }
    std::function<void()> p = print;
    close();
    if (p) p();
  }
}
