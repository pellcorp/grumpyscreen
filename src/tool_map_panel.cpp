#include "tool_map_panel.h"
#include "mmu_panel.h"
#include "mmu_tools.h"
#include "theme.h"
#include "icons.h"
#include "logger.h"

#include <algorithm>

using namespace Theme;

// the header and four tool rows show at once; a file using more tools scrolls
static const int VISIBLE_ROWS = 5;

// the spoolman panel's header and zebra tints, so the two tables read alike
static const lv_opa_t HEADER_TINT = LV_OPA_20;
static const lv_opa_t ZEBRA_TINT = LV_OPA_40;
// a pressed row takes the accent, as a pressed tile or button does
static const lv_opa_t PRESSED_TINT = LV_OPA_40;

// The file's side, "to", the printer's side, mirrored so the two colours sit
// either side of "to".
enum Col { STATUS, TOOL, FILE_MAT, REQ, FILE_DOT, TO, SLOT_DOT, LANE, SLOT_MAT, AVAIL };

ToolMapPanel::ToolMapPanel() {}

ToolMapPanel::~ToolMapPanel() {
  if (picker != NULL) lv_obj_del(picker);
  print_btn.reset();
  back_btn.reset();
  if (cont != NULL) lv_obj_del(cont);
}

// 2.3g, 61g, 1.25kg
static std::string weight_text(double grams) {
  if (grams < 10) return fmt::format("{:.1f}g", grams);
  if (grams < 1000) return fmt::format("{:.0f}g", grams);
  return fmt::format("{:.2f}kg", grams / 1000);
}

void ToolMapPanel::create() {
  if (cont != NULL) return;

  // the spoolman panel's layout: the table takes the height, a row of
  // controls under it with its icon tiles on the right
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
  lv_obj_set_style_pad_left(title, gap(), 0);  // off the screen edge, like the table border
  lv_label_set_text(title, "");

  // the one opt-in: match the rows to what is loaded. Words, not an icon --
  // no icon in the set says "match"
  auto_btn = create_text_btn(controls, "Auto match", &ToolMapPanel::_handle_action, this);
  lv_obj_set_size(auto_btn, LV_SIZE_CONTENT, touch_h());
  lv_obj_set_style_pad_hor(auto_btn, gap() * 2, 0);
  lv_obj_set_style_bg_color(auto_btn, theme_primary(), LV_STATE_PRESSED);
  lv_obj_set_style_color_filter_opa(auto_btn, LV_OPA_TRANSP, LV_STATE_PRESSED);

  // icon-only square tiles a finger tall, as the spoolman panel has them
  back_btn = std::make_unique<ButtonContainer>(controls, Icons::BACK, "Back",
                                               &ToolMapPanel::_handle_action, this);
  print_btn = std::make_unique<ButtonContainer>(controls, Icons::PRINT, "Print",
                                                &ToolMapPanel::_handle_action, this);
  for (ButtonContainer *b : {back_btn.get(), print_btn.get()}) {
    b->use_card();
    b->hide_label();
    lv_obj_set_size(b->get_container(), touch_h(), touch_h());
  }

  // rows split the table's height: the header and four tools fit
  lv_obj_update_layout(cont);
  const lv_font_t *font = lv_obj_get_style_text_font(table, LV_PART_ITEMS);
  const int row_h = lv_obj_get_content_height(table) / VISIBLE_ROWS;
  lv_obj_set_style_pad_ver(table, std::max(0, (row_h - lv_font_get_line_height(font)) / 2), LV_PART_ITEMS);
}

static int dot_d() { return scale_r(18); }

// the fixed columns take what a symbol, a circle or a short figure needs; the
// three named columns share the rest
void ToolMapPanel::layout_columns() {
  lv_obj_update_layout(table);
  // a circle and the cell padding either side of it
  const int pad = lv_obj_get_style_pad_left(table, LV_PART_ITEMS) +
                  lv_obj_get_style_pad_right(table, LV_PART_ITEMS);
  const int status_w = scale_w(24), tool_w = scale_w(46), dot_w = dot_d() + pad;
  const int to_w = scale_w(30), req_w = scale_w(50), avail_w = scale_w(56);
  const int remain = lv_obj_get_content_width(table) - status_w - tool_w - req_w -
                     2 * dot_w - to_w - avail_w;
  lv_table_set_col_width(table, STATUS, status_w);
  lv_table_set_col_width(table, TOOL, tool_w);
  lv_table_set_col_width(table, FILE_MAT, remain * 30 / 100);
  lv_table_set_col_width(table, REQ, req_w);
  lv_table_set_col_width(table, FILE_DOT, dot_w);
  lv_table_set_col_width(table, TO, to_w);
  lv_table_set_col_width(table, SLOT_DOT, dot_w);
  lv_table_set_col_width(table, LANE, remain * 38 / 100);
  lv_table_set_col_width(table, SLOT_MAT, remain - remain * 30 / 100 - remain * 38 / 100);
  lv_table_set_col_width(table, AVAIL, avail_w);
}

void ToolMapPanel::open(MmuBackend *b, const std::vector<MmuFileTool> &t,
                        const std::string &filename, std::function<void()> p) {
  create();
  backend = b;
  tools = t;
  print = p;
  lv_label_set_text(title, fmt::format("Preparing {}", filename).c_str());
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
  lv_obj_scroll_to_y(table, 0, LV_ANIM_OFF);

  // start from the printer's own map; nothing changes until the user picks
  want.clear();
  for (const MmuFileTool &tool : tools) want.push_back(mmu_slot_for_tool(backend->slots, tool.tool));
  update();
}

void ToolMapPanel::close() {
  close_picker();
  want.clear();  // nothing picked here reaches the printer unless it prints
  backend = NULL;
  print = nullptr;
  if (cont != NULL) {
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
}

// Giving a row a slot another row has: where the vendor lets tools share a
// slot they both draw from it, otherwise the two rows swap -- each slot then
// feeds one tool, and the commands land as shown whatever the vendor's does
// with the slot a tool leaves.
void ToolMapPanel::assign(size_t t, int slot) {
  if (!backend->can_share_slot()) {
    for (size_t j = 0; j < want.size(); j++) {
      if (j != t && want[j] == slot) want[j] = want[t];
    }
  }
  want[t] = slot;
}

bool ToolMapPanel::is_open() const {
  return cont != NULL && !lv_obj_has_flag(cont, LV_OBJ_FLAG_HIDDEN);
}

// Each row's verdict, in its first cell. No slot or an empty one is a red
// cross and holds Print back; another material, or a colour that is not the
// file's (MMU_COLOUR_MISMATCH), is an amber warning -- printable, but not
// what was sliced. A material mismatch also shows its two materials amber.
void ToolMapPanel::update() {
  if (!is_open() || backend == NULL) return;
  const std::vector<MmuSlot> &slots = backend->slots;

  lv_table_set_col_cnt(table, AVAIL + 1);
  lv_table_set_row_cnt(table, tools.size() + 1);

  for (uint16_t c = 0; c < lv_table_get_col_cnt(table); c++) lv_table_set_cell_value(table, 0, c, "");
  lv_table_set_cell_value(table, 0, TOOL, "Tool");
  lv_table_set_cell_value(table, 0, FILE_MAT, "File");
  lv_table_set_cell_value(table, 0, REQ, "Req.");
  lv_table_set_cell_value(table, 0, LANE, "Lane");
  lv_table_set_cell_value(table, 0, SLOT_MAT, "Printer");
  lv_table_set_cell_value(table, 0, AVAIL, "Avail.");
  // never wrapped: a taller header would cost the rows below their room
  for (uint16_t c = 0; c < lv_table_get_col_cnt(table); c++) {
    lv_table_add_cell_ctrl(table, 0, c, LV_TABLE_CELL_CTRL_TEXT_CROP);
  }

  paint.assign(tools.size(), RowPaint());
  bool blocked = false;
  for (size_t t = 0; t < tools.size(); t++) {
    const MmuFileTool &tool = tools[t];
    const uint16_t row = t + 1;
    const int s = t < want.size() ? want[t] : -1;
    const MmuSlot *slot = s >= 0 && (size_t)s < slots.size() ? &slots[s] : NULL;

    const bool missing = slot == NULL || !mmu_has_filament(*slot);
    const bool other_material = !missing && !tool.material.empty() && !slot->material.empty() &&
                                !mmu_same_material(tool.material, slot->material);
    // a close colour (blue for purple) warns as well as a far one
    const bool other_colour = !missing &&
                              mmu_colour_distance(tool.colour, slot->colour) > MMU_COLOUR_MISMATCH;
    blocked = blocked || missing;
    paint[t].status = missing ? Status::Blocked
                      : other_material || other_colour ? Status::Warn : Status::Ok;
    paint[t].file_colour = tool.colour;
    paint[t].slot_colour = slot != NULL ? slot->colour : "";
    paint[t].slot_filled = slot != NULL && mmu_has_filament(*slot);
    paint[t].unconfigured = slot == NULL || slot->material.empty();
    paint[t].other_material = other_material;

    lv_table_set_cell_value(table, row, STATUS, missing ? LV_SYMBOL_CLOSE
                                                : other_material || other_colour ? LV_SYMBOL_WARNING
                                                : LV_SYMBOL_OK);
    lv_table_set_cell_value(table, row, TOOL, fmt::format("T{}", tool.tool).c_str());
    lv_table_set_cell_value(table, row, FILE_MAT, tool.material.c_str());
    lv_table_set_cell_value(table, row, REQ, weight_text(tool.grams).c_str());
    lv_table_set_cell_value(table, row, FILE_DOT, "");
    lv_table_set_cell_value(table, row, TO, "to");
    lv_table_set_cell_value(table, row, SLOT_DOT, "");
    lv_table_set_cell_value(table, row, LANE, slot != NULL ? slot->name.c_str() : "None");
    lv_table_set_cell_value(table, row, SLOT_MAT, paint[t].unconfigured ? "Empty" : slot->material.c_str());
    // a spool weight only means something via spoolman, as on the MMU tab
    const bool weighed = backend->spoolman && slot != NULL && slot->weight > 0;
    lv_table_set_cell_value(table, row, AVAIL, weighed ? weight_text(slot->weight).c_str() : "n/a");
    // a long material or custom lane name is cut short rather than wrapped,
    // which would make its row taller than the rest
    for (uint16_t c : {FILE_MAT, LANE, SLOT_MAT}) {
      lv_table_add_cell_ctrl(table, row, c, LV_TABLE_CELL_CTRL_TEXT_CROP);
    }
  }
  layout_columns();
  refresh_scroll(table);

  // a tool with nothing to feed it cannot print
  if (blocked) print_btn->disable();
  else print_btn->enable();

  // a picker open over a slot list that just changed is rebuilt to match
  if (picking_tool >= 0) {
    for (size_t t = 0; t < tools.size(); t++) {
      if (tools[t].tool == picking_tool) { show_picker(t); break; }
    }
  }
}

void ToolMapPanel::handle_table(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    // the whole row is the control: any cell of it opens the picker
    uint16_t row, col;
    lv_table_get_selected_cell(table, &row, &col);
    if (row == LV_TABLE_CELL_NONE || row == 0) return;
    show_picker(row - 1);
    return;
  }

  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_ITEMS) return;
  const uint32_t cols = lv_table_get_col_cnt(table);
  const uint32_t row = dsc->id / cols;
  const uint32_t column = dsc->id - row * cols;
  const RowPaint *p = row > 0 && row <= paint.size() ? &paint[row - 1] : NULL;

  if (code == LV_EVENT_DRAW_PART_END) {
    // the filament colours as circles, centred in their cells; a ring alone
    // where no colour is known, half strength over an empty slot
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
    lv_area_t a;
    const int d = dot_d();
    a.x1 = (dsc->draw_area->x1 + dsc->draw_area->x2 - d) / 2;
    a.y1 = (dsc->draw_area->y1 + dsc->draw_area->y2 - d) / 2;
    a.x2 = a.x1 + d - 1;
    a.y2 = a.y1 + d - 1;
    lv_draw_rect(dsc->draw_ctx, &rd, &a);
    return;
  }

  // a header lines up with its column: the marks and circles centred, words
  // and figures from the left, in the header as in the rows
  if (column == STATUS || column == FILE_DOT || column == TO || column == SLOT_DOT) {
    dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
  }
  if (row == 0) {
    dsc->rect_dsc->bg_color = lv_color_mix(theme_primary(), dsc->rect_dsc->bg_color, HEADER_TINT);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  }
  // the spoolman panel's zebra, header included
  if (row % 2 == 0) {
    dsc->rect_dsc->bg_color = lv_color_mix(col(RAISED), dsc->rect_dsc->bg_color, ZEBRA_TINT);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  }
  if (p == NULL) return;

  // the table presses one cell; the row is the control, so all of it shows it
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
  if (backend == NULL || t >= tools.size()) {
    close_picker();
    return;
  }
  const MmuFileTool &tool = tools[t];
  const std::vector<MmuSlot> &slots = backend->slots;

  if (picker == NULL) {
    picker_box = create_popout(&picker, &ToolMapPanel::_handle_close_picker, this);
    lv_obj_set_flex_flow(picker_box, LV_FLEX_FLOW_ROW_WRAP);
  }
  lv_obj_clean(picker_box);
  picking_tool = tool.tool;

  lv_obj_t *head = lv_label_create(picker_box);
  lv_obj_set_width(head, LV_PCT(100));
  lv_obj_set_style_text_font(head, scale_font(14), 0);

  // A tool the vendor will not take (one the file uses beyond the unit's
  // tools, say) gets told so, rather than a tap that does nothing
  const bool mappable = backend->can_map_tool(tool.tool);
  if (!mappable) {
    lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
    lv_label_set_text(head, fmt::format("T{} can't be mapped on this printer", tool.tool).c_str());
  } else {
    lv_label_set_text(head, fmt::format("T{} {} from:", tool.tool, tool.material).c_str());

    // four across, the way the backup picker lays the same tiles out; a unit
    // with more slots than fit scrolls the popout
    const int cols = std::max(1, std::min((int)slots.size(), 4));
    const int tile_w = (popout_row_w() - (cols - 1) * gap()) / cols;
    const int current = t < want.size() ? want[t] : -1;
    for (size_t i = 0; i < slots.size(); i++) {
      lv_obj_t *b = create_slot_tile(picker_box, slots[i], i, tile_w, &ToolMapPanel::_handle_pick, this, true);
      lv_obj_set_user_data(b, (void*)(intptr_t)i);
      if ((int)i == current) {
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
  const int tool = picking_tool;
  close_picker();
  if (backend == NULL || tool < 0) return;
  for (size_t t = 0; t < tools.size(); t++) {
    if (tools[t].tool == tool) assign(t, slot);
  }
  update();
}

// Match the rows to the loaded filament (mmu_automap), only when asked: the
// result is held like any pick, so Back still leaves the printer as it was.
void ToolMapPanel::auto_match() {
  if (backend == NULL) return;
  // the grams count only where spoolman supplies them, as on the MMU tab
  const std::vector<int> target = mmu_automap(tools, backend->slots, want,
                                              backend->can_share_slot(), backend->spoolman);
  for (size_t i = 0; i < tools.size() && i < target.size(); i++) {
    if (!backend->can_map_tool(tools[i].tool)) continue;
    if (target[i] == MMU_AUTOMAP_CLEAR) want[i] = -1;  // the user picks
    else if (target[i] >= 0) assign(i, target[i]);
  }
  update();
}

// One map command per tool of the print not already on the slot picked for
// it. Without sharing every row's slot is a different one, so whatever a
// vendor's command does to the slot it takes from (AFC swaps it over), a later
// command never lands on a tool already placed.
void ToolMapPanel::send_picks() {
  for (size_t t = 0; t < tools.size() && t < want.size(); t++) {
    const int tool = tools[t].tool;
    if (want[t] >= 0 && want[t] != mmu_slot_for_tool(backend->slots, tool) &&
        backend->can_map_tool(tool)) {
      backend->map_tool(tool, want[t]);
    }
  }
}

void ToolMapPanel::close_picker() {
  picking_tool = -1;
  hide_popout(picker);
}

void ToolMapPanel::handle_action(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == auto_btn) {
    auto_match();
  } else if (btn == back_btn->get_container()) {
    close();
  } else if (btn == print_btn->get_container() && !lv_obj_has_state(btn, LV_STATE_DISABLED)) {
    // the map commands go ahead of the print through the same gcode queue
    if (backend != NULL) send_picks();
    std::function<void()> p = print;
    close();
    if (p) p();
  }
}
