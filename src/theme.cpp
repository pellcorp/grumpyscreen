#include "theme.h"
#include <fmt/core.h>
#include "config.h"

#include <algorithm>
#include <cstdlib>

namespace Theme {

namespace {

// the two [theme] numbers, read once: gap() and the radii are called from
// every panel's constructor, and a config lookup each time is code and time
int knob_pad() { return scale_r(4); }  // the knob is the track's height plus this each side

int cfg_int(const char *key, int def) {
  return Config::get_instance()->get<int>(std::string("/theme/") + key, def);
}
bool cfg_bool(const char *key, bool def) {
  return Config::get_instance()->get<bool>(std::string("/theme/") + key, def);
}

// /theme/style: "modern" (the default) or "classic". A preset is nothing but
// different fallbacks for four pegs -- classic is flat: panels the page colour,
// square, no hairlines, no scrollbars -- so any peg set explicitly still wins.
bool classic() {
  static const bool c = Config::get_instance()->get<std::string>("/theme/style") == "classic";
  return c;
}

}  // namespace

lv_color_t cfg_col(const char *key, lv_color_t def) {
  const auto s = Config::get_instance()->get<std::string>(std::string("/theme/") + key);
  if (s.empty()) return def;
  char *end = NULL;
  const unsigned long v = std::strtoul(s.c_str(), &end, 16);
  return (end == s.c_str() || *end != '\0') ? def : lv_color_hex(v);
}

lv_color_t col(Colour c) {
  static lv_color_t cache[COLOUR_COUNT];
  static bool ready = false;
  if (!ready) {
    // the defaults are the greys the UI shipped with: the LVGL palette greys,
    // plus the stock dark theme's card colour (0x282b30) for the page, which is
    // what every full-screen panel showed before it said so explicitly
    cache[BG]         = cfg_col("background_colour", lv_color_hex(0x282b30));
    cache[SURFACE]    = cfg_col("surface_colour",    classic() ? cache[BG] : lv_palette_darken(LV_PALETTE_GREY, 4));
    cache[RAISED]     = cfg_col("raised_colour",     lv_palette_darken(LV_PALETTE_GREY, 3));
    cache[BORDER]     = cfg_col("border_colour",     lv_palette_darken(LV_PALETTE_GREY, 3));
    cache[BORDER_DIM] = cfg_col("border_dim_colour", lv_palette_darken(LV_PALETTE_GREY, 2));
    cache[TEXT]       = cfg_col("text_colour",       lv_color_white());
    cache[TEXT_DIM]   = cfg_col("text_dim_colour",   lv_palette_main(LV_PALETTE_GREY));
    cache[DISABLED]   = cfg_col("disabled_colour",   lv_palette_darken(LV_PALETTE_GREY, 1));
    cache[ON_PRIMARY] = cfg_col("on_primary_colour", lv_color_white());
    cache[DANGER]     = cfg_col("danger_colour",     lv_palette_darken(LV_PALETTE_RED, 2));
    cache[WARNING]    = cfg_col("warning_colour",    lv_palette_darken(LV_PALETTE_AMBER, 2));
    ready = true;
  }
  return cache[c];
}

lv_color_t theme_primary() { return lv_theme_get_color_primary(lv_scr_act()); }
lv_color_t theme_secondary() { return lv_theme_get_color_secondary(lv_scr_act()); }

int scale_w(int px) { return px * lv_disp_get_physical_hor_res(NULL) / 480; }
int scale_h(int px) { return px * lv_disp_get_physical_ver_res(NULL) / 272; }
int scale_r(int px) { return std::min(scale_w(px), scale_h(px)); }

const lv_font_t *scale_font(int px) {
  struct F { int size; const lv_font_t *font; };
  static const F fonts[] = {
    {12, &lv_font_montserrat_12}, {14, &lv_font_montserrat_14},
    {16, &lv_font_montserrat_16}, {18, &lv_font_montserrat_18},
    {20, &lv_font_montserrat_20}, {22, &lv_font_montserrat_22},
#if LV_FONT_MONTSERRAT_24  // the small-screen build leaves the big sizes out of flash
    {24, &lv_font_montserrat_24}, {26, &lv_font_montserrat_26},
    {28, &lv_font_montserrat_28},
#endif
  };
  const int target = scale_r(px);
  for (const F &f : fonts) {
    if (f.size >= target) return f.font;
  }
  return fonts[sizeof(fonts) / sizeof(fonts[0]) - 1].font;
}

int gap() {
  static const int g = scale_r(cfg_int("gap", 6));
  return g;
}

// one config number sets the middle radius; the other two step either side of
// it, and the small one cannot go negative on a square-cornered theme
static int radius_base() {
  static const int r = cfg_int("radius", classic() ? 0 : 6);
  return r;
}
int border_w() {
  static const int b = cfg_int("border", classic() ? 0 : 1);  // a hairline stays a hairline at any size
  return b;
}

int radius_sm() { return scale_r(std::max(0, radius_base() - 2)); }
int radius_md() { return scale_r(radius_base()); }
int radius_lg() { return scale_r(radius_base() + 2); }

// a popout sits one gap in from every screen edge and pads its content by a
// gap and a half, so the whole thing follows [theme] gap
int popout_w() { return lv_disp_get_physical_hor_res(NULL) - 2 * gap(); }
int popout_max_h() { return lv_disp_get_physical_ver_res(NULL) - 2 * gap(); }
int popout_pad() { return gap() + gap() / 2; }
// box padding, the two borders, and 2px so integer rounding cannot wrap a row
int popout_row_w() { return popout_w() - 2 * popout_pad() - 2 * border_w() - 2; }

Styles &styles() {
  static Styles s;
  static bool ready = false;
  if (ready) return s;

  lv_style_init(&s.btn);
  lv_style_set_pad_all(&s.btn, 0);
  lv_style_set_shadow_width(&s.btn, 0);
  lv_style_set_radius(&s.btn, radius_sm());
  lv_style_set_text_font(&s.btn, scale_font(14));
  lv_style_set_bg_color(&s.btn, col(RAISED));
  lv_style_set_bg_opa(&s.btn, LV_OPA_COVER);

  // pressing anything shrinks it by 2px, the grumpyscreen tap feedback
  lv_style_init(&s.btn_pressed);
  lv_style_set_transform_width(&s.btn_pressed, -2);
  lv_style_set_transform_height(&s.btn_pressed, -2);

  lv_style_init(&s.card);
  lv_style_set_radius(&s.card, radius_md());
  lv_style_set_bg_color(&s.card, col(SURFACE));
  lv_style_set_bg_opa(&s.card, LV_OPA_COVER);
  lv_style_set_border_width(&s.card, border_w());
  lv_style_set_border_color(&s.card, col(BORDER));
  lv_style_set_pad_all(&s.card, scale_r(4));

  // shared, not per-tile local writes: LVGL allocates local properties per
  // object, and there are sixty-odd tiles
  // sides stay tight so a seven-tile row fits one-word labels; top and bottom
  // follow the gap, so a roomier theme lifts the label off the edge (the icon
  // gives the room up, see ButtonContainer::fit_icon)
  lv_style_init(&s.tile);
  lv_style_set_pad_hor(&s.tile, scale_r(4));
  lv_style_set_pad_ver(&s.tile, gap());
  lv_style_set_pad_row(&s.tile, scale_r(2));

  lv_style_init(&s.card_pressed);
  lv_style_set_bg_color(&s.card_pressed, col(RAISED));
  lv_style_set_bg_opa(&s.card_pressed, LV_OPA_COVER);
  lv_style_set_transform_width(&s.card_pressed, -2);
  lv_style_set_transform_height(&s.card_pressed, -2);

  // a list row (a fan slider, a temperature readout): the card look, but
  // full width and with the padding a row of controls needs
  lv_style_init(&s.row_card);
  lv_style_set_radius(&s.row_card, radius_md());
  lv_style_set_bg_color(&s.row_card, col(SURFACE));
  lv_style_set_bg_opa(&s.row_card, LV_OPA_COVER);
  lv_style_set_border_width(&s.row_card, border_w());
  lv_style_set_border_color(&s.row_card, col(BORDER));
  lv_style_set_border_side(&s.row_card, LV_BORDER_SIDE_FULL);
  lv_style_set_pad_all(&s.row_card, gap());
  lv_style_set_pad_row(&s.row_card, 0);

  lv_style_init(&s.panel);
  lv_style_set_radius(&s.panel, radius_lg());
  lv_style_set_bg_color(&s.panel, col(SURFACE));
  lv_style_set_bg_opa(&s.panel, LV_OPA_COVER);
  lv_style_set_border_width(&s.panel, border_w());
  lv_style_set_border_color(&s.panel, col(BORDER));
  lv_style_set_pad_all(&s.panel, gap());

  lv_style_init(&s.popout);
  lv_style_set_pad_all(&s.popout, 0);
  lv_style_set_bg_color(&s.popout, lv_color_black());
  lv_style_set_bg_opa(&s.popout, LV_OPA_50);
  lv_style_set_border_width(&s.popout, 0);

  lv_style_init(&s.popout_box);
  lv_style_set_radius(&s.popout_box, radius_lg());
  lv_style_set_bg_color(&s.popout_box, col(SURFACE));
  lv_style_set_bg_opa(&s.popout_box, LV_OPA_COVER);
  lv_style_set_border_width(&s.popout_box, border_w());
  lv_style_set_border_color(&s.popout_box, col(BORDER));
  lv_style_set_pad_all(&s.popout_box, popout_pad());
  lv_style_set_text_color(&s.popout_box, col(TEXT));  // popouts sit on lv_layer_top, not a page
  lv_style_set_pad_row(&s.popout_box, gap());
  lv_style_set_pad_column(&s.popout_box, gap());
  lv_style_set_max_height(&s.popout_box, popout_max_h());

  // no padding of its own, children one gap apart: without an explicit gap the
  // LVGL default theme's DPI-derived pad_row/pad_column would leak in
  lv_style_init(&s.row);
  lv_style_set_pad_all(&s.row, 0);
  lv_style_set_pad_row(&s.row, gap());
  lv_style_set_pad_column(&s.row, gap());
  lv_style_set_bg_opa(&s.row, LV_OPA_TRANSP);
  lv_style_set_border_width(&s.row, 0);

  // A full-screen overlay has to paint its own background: a plain container
  // is transparent scaffolding now, so without this the panel underneath shows
  // through. No radius or border -- it is the page, not a card.
  lv_style_init(&s.screen);
  lv_style_set_bg_color(&s.screen, col(BG));
  lv_style_set_bg_opa(&s.screen, LV_OPA_COVER);
  lv_style_set_radius(&s.screen, 0);
  lv_style_set_border_width(&s.screen, 0);
  lv_style_set_pad_all(&s.screen, gap());
  lv_style_set_pad_row(&s.screen, gap());
  lv_style_set_pad_column(&s.screen, gap());
  // text colour inherits, so setting it on the page reaches every label under
  // it; without this labels keep the LVGL theme's own grey
  lv_style_set_text_color(&s.screen, col(TEXT));

  lv_style_init(&s.swatch);
  lv_style_set_radius(&s.swatch, radius_sm());
  lv_style_set_shadow_width(&s.swatch, 0);
  lv_style_set_pad_all(&s.swatch, 0);
  lv_style_set_border_width(&s.swatch, border_w());
  lv_style_set_border_color(&s.swatch, col(BORDER_DIM));

  lv_style_init(&s.swatch_pressed);
  lv_style_set_transform_width(&s.swatch_pressed, -2);
  lv_style_set_transform_height(&s.swatch_pressed, -2);

  lv_style_init(&s.dim_disabled);
  lv_style_set_bg_opa(&s.dim_disabled, LV_OPA_30);

  lv_style_init(&s.dim_label);
  lv_style_set_text_font(&s.dim_label, scale_font(12));
  lv_style_set_text_color(&s.dim_label, col(TEXT_DIM));

  lv_style_init(&s.icon_pressed);
  lv_style_set_img_recolor_opa(&s.icon_pressed, LV_OPA_COVER);
  lv_style_set_img_recolor(&s.icon_pressed, theme_primary());

  lv_style_init(&s.icon_disabled);
  lv_style_set_img_recolor_opa(&s.icon_disabled, LV_OPA_COVER);
  lv_style_set_img_recolor(&s.icon_disabled, col(DISABLED));

  lv_style_init(&s.key_tray);
  lv_style_set_bg_opa(&s.key_tray, LV_OPA_TRANSP);
  lv_style_set_border_width(&s.key_tray, 0);
  lv_style_set_pad_all(&s.key_tray, 0);
  lv_style_set_pad_gap(&s.key_tray, gap());

  lv_style_init(&s.key);
  lv_style_set_radius(&s.key, radius_sm());
  lv_style_set_text_font(&s.key, scale_font(14));  // same size as the entry it types into
  lv_style_set_bg_color(&s.key, col(RAISED));
  lv_style_set_bg_opa(&s.key, LV_OPA_COVER);
  lv_style_set_border_width(&s.key, 0);
  lv_style_set_text_color(&s.key, col(TEXT));

  lv_style_init(&s.key_pressed);
  lv_style_set_bg_color(&s.key_pressed, theme_primary());
  lv_style_set_text_color(&s.key_pressed, col(ON_PRIMARY));

  lv_style_init(&s.input);
  lv_style_set_radius(&s.input, radius_sm());
  // an entry with no outline is a bare line of text, so a theme without
  // hairlines still gets a faint one here, in the raised grey
  lv_style_set_border_width(&s.input, border_w() ? border_w() : 1);
  lv_style_set_border_color(&s.input, col(border_w() ? BORDER : RAISED));
  lv_style_set_bg_color(&s.input, col(SURFACE));
  lv_style_set_bg_opa(&s.input, LV_OPA_COVER);
  lv_style_set_text_font(&s.input, scale_font(14));
  lv_style_set_text_color(&s.input, col(TEXT));

  lv_style_init(&s.input_placeholder);
  lv_style_set_text_font(&s.input_placeholder, scale_font(14));
  lv_style_set_text_color(&s.input_placeholder, col(TEXT_DIM));

  lv_style_init(&s.table);
  lv_style_set_radius(&s.table, radius_lg());
  lv_style_set_bg_color(&s.table, col(SURFACE));
  lv_style_set_bg_opa(&s.table, LV_OPA_COVER);
  lv_style_set_border_width(&s.table, border_w());
  lv_style_set_border_color(&s.table, col(BORDER));
  lv_style_set_pad_all(&s.table, 0);
  lv_style_set_clip_corner(&s.table, true);  // cells follow the rounded corners instead of poking out square

  // solid rather than the stock translucent grey, so it reads as a control and
  // not a smudge; the side scrollbar thumb wears it too
  lv_style_init(&s.scrollbar);
  lv_style_set_width(&s.scrollbar, scale_r(4));
  lv_style_set_pad_all(&s.scrollbar, scale_r(2));
  lv_style_set_radius(&s.scrollbar, LV_RADIUS_CIRCLE);
  lv_style_set_bg_color(&s.scrollbar, col(TEXT_DIM));
  lv_style_set_bg_opa(&s.scrollbar, LV_OPA_COVER);

  lv_style_init(&s.track);
  lv_style_set_bg_color(&s.track, col(RAISED));
  lv_style_set_bg_opa(&s.track, LV_OPA_COVER);
  lv_style_set_border_width(&s.track, 0);

  lv_style_init(&s.fill);
  lv_style_set_bg_color(&s.fill, theme_primary());
  lv_style_set_bg_opa(&s.fill, LV_OPA_COVER);

  lv_style_init(&s.arc_track);
  lv_style_set_arc_color(&s.arc_track, col(RAISED));
  lv_style_set_bg_opa(&s.arc_track, LV_OPA_TRANSP);
  lv_style_set_border_width(&s.arc_track, 0);
  lv_style_init(&s.arc_fill);
  lv_style_set_arc_color(&s.arc_fill, theme_primary());

  lv_style_init(&s.knob);
  lv_style_set_bg_color(&s.knob, col(TEXT));
  lv_style_set_bg_opa(&s.knob, LV_OPA_COVER);
  lv_style_set_border_width(&s.knob, 0);
  lv_style_set_pad_all(&s.knob, knob_pad());

  // A slider's knob is drawn centred on the track's end, so half of it hangs
  // past the widget's box, where the parent clips it at full and at zero.
  // Insetting the track by the tallest slider's overhang keeps every knob
  // inside its box. A negative transform rather than padding: lv_bar applies
  // the transform to both the groove and the fill, but padding to the fill
  // only, and the two must end at the same place.
  lv_style_init(&s.slider);
  lv_style_set_transform_width(&s.slider, -knob_overhang(slider_h()));

  lv_style_init(&s.table_cell);
  lv_style_set_pad_ver(&s.table_cell, gap());
  lv_style_set_pad_hor(&s.table_cell, gap());
  lv_style_set_bg_color(&s.table_cell, col(SURFACE));
  lv_style_set_bg_opa(&s.table_cell, LV_OPA_COVER);
  lv_style_set_border_color(&s.table_cell, col(BORDER));
  lv_style_set_text_color(&s.table_cell, col(TEXT));

  ready = true;
  return s;
}

int fit_img(lv_obj_t *img, int w, int h, int max_zoom) {
  const lv_img_t *i = reinterpret_cast<const lv_img_t *>(img);  // w/h of the decoded source
  // a zoomed image's REAL-size box is a few px wider than the scaled bitmap on
  // each axis (anti-alias margin), so fit the bitmap to that much less or the
  // box overshoots the space it was fitted to
  const int margin = 5;
  int zoom = max_zoom;
  if (i->w > 0 && w > 0) zoom = std::min(zoom, LV_IMG_ZOOM_NONE * (w - margin) / i->w);
  if (i->h > 0 && h > 0) zoom = std::min(zoom, LV_IMG_ZOOM_NONE * (h - margin) / i->h);
  zoom = std::max(zoom, LV_IMG_ZOOM_NONE / 8);
  // a no-op when nothing changes: lv_img_set_zoom and the box refresh each
  // cost a layout pass, and size-changed handlers call this on every layout
  if (zoom == lv_img_get_zoom(img) && lv_img_get_size_mode(img) == LV_IMG_SIZE_MODE_REAL) return zoom;
  lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_zoom(img, zoom);
  lv_obj_refresh_self_size(img);
  return zoom;
}

void fit_first_icon(lv_event_t *e) {
  lv_obj_t *box = lv_event_get_target(e);
  const int h = lv_obj_get_content_height(box);
  fit_img(lv_obj_get_child(box, 0), h, h);
}

int touch_h() { return scale_r(44); }

int slider_h() { return scale_r(16); }
int knob_overhang(int track_h) { return track_h / 2 + knob_pad(); }

bool scrollbars() {
  static const bool on = cfg_bool("scrollbars", !classic());
  return on;
}

std::string recolor(Colour c) { return fmt::format("#{:06x} ", lv_color_to32(col(c)) & 0xffffff); }

lv_obj_t *create_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_add_style(row, &styles().row, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

// the lane is the sibling just before the object it tracks
static lv_obj_t *side_scrollbar_lane(lv_obj_t *scrollee) {
  return lv_obj_get_child(lv_obj_get_parent(scrollee), lv_obj_get_index(scrollee) - 1);
}

void add_side_scrollbar(lv_obj_t *scrollee) {
  lv_obj_t *lane = create_row(lv_obj_get_parent(scrollee));
  lv_obj_move_to_index(lane, lv_obj_get_index(scrollee));
  // as wide as the thumb: the row's own column gap is the air either side
  lv_obj_set_size(lane, scale_r(4), LV_PCT(100));
  lv_obj_t *thumb = lv_obj_create(lane);
  lv_obj_add_style(thumb, &styles().scrollbar, 0);
  lv_obj_clear_flag(thumb, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(thumb, LV_PCT(100));
  // the object's own bar would double up inside it
  lv_obj_set_scrollbar_mode(scrollee, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(scrollee, LV_DIR_VER);
  auto follow = [](lv_event_t *e) { refresh_side_scrollbar(lv_event_get_target(e)); };
  lv_obj_add_event_cb(scrollee, follow, LV_EVENT_SCROLL, NULL);
  lv_obj_add_event_cb(scrollee, follow, LV_EVENT_SIZE_CHANGED, NULL);
  refresh_side_scrollbar(scrollee);
}

void refresh_side_scrollbar(lv_obj_t *scrollee) {
  lv_obj_t *lane = side_scrollbar_lane(scrollee);
  const int top = lv_obj_get_scroll_top(scrollee), bottom = lv_obj_get_scroll_bottom(scrollee);
  const bool overflows = top > 0 || bottom > 0;
  // fits: no bounce under a finger; and no bar when it fits, is away, or bars are off
  if (overflows) lv_obj_add_flag(scrollee, LV_OBJ_FLAG_SCROLLABLE);
  else lv_obj_clear_flag(scrollee, LV_OBJ_FLAG_SCROLLABLE);
  if (!overflows || !scrollbars() || lv_obj_has_flag(scrollee, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(lane, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(lane, LV_OBJ_FLAG_HIDDEN);
  // the thumb is the visible share of the content, placed by how far it has scrolled
  const int view = lv_obj_get_height(scrollee), lane_h = lv_obj_get_height(lane);
  const int thumb_h = std::max(lane_h * view / (view + top + bottom), scale_r(16));
  lv_obj_t *thumb = lv_obj_get_child(lane, 0);
  lv_obj_set_height(thumb, thumb_h);
  lv_obj_set_y(thumb, (lane_h - thumb_h) * top / (top + bottom));
}

lv_obj_t *create_screen(lv_obj_t *parent) {
  lv_obj_t *scr = lv_obj_create(parent != NULL ? parent : lv_scr_act());
  lv_obj_add_style(scr, &styles().screen, 0);
  lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

// the btn look itself comes from the theme callback (every lv_btn wears it);
// this only adds the label and the click
lv_obj_t *create_flat_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_center(lbl);
  if (cb != NULL) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
  }
  return btn;
}

void set_btn_label(lv_obj_t *btn, const char *text) {
  if (btn != NULL && lv_obj_get_child_cnt(btn) > 0) {
    lv_label_set_text(lv_obj_get_child(btn, 0), text);
  }
}

void set_action_btn(lv_obj_t *btn, bool enabled, lv_color_t enabled_colour) {
  lv_obj_t *label = lv_obj_get_child_cnt(btn) > 0 ? lv_obj_get_child(btn, 0) : NULL;
  if (enabled) {
    lv_obj_clear_state(btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn, enabled_colour, 0);
    // the enabled colour is an accent, so the text is the on-accent colour
    if (label) lv_obj_set_style_text_color(label, col(ON_PRIMARY), 0);
  } else {
    // grey text on the plain button grey: reads as off, unlike TEXT which
    // would make it identical to a live flat button
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn, col(RAISED), 0);
    if (label) lv_obj_set_style_text_color(label, col(DISABLED), 0);
  }
}

}  // namespace Theme
