#include "theme.h"
#include <fmt/core.h>
#include "config.h"

#include <algorithm>
#include <cstdlib>

namespace Theme {

namespace {

int knob_pad() { return scale_r(4); }  // the knob is the track's height plus this each side

// LVGL's default theme darkens a pressed button with a colour filter and never
// touches a plain object, so a tappable card has to borrow the same filter to
// react the same way. Same callback, same opacity: one press look everywhere.
lv_color_t darken_filter_cb(const lv_color_filter_dsc_t *f, lv_color_t c, lv_opa_t opa) {
  LV_UNUSED(f);
  return lv_color_darken(c, opa);
}

}  // namespace

// Classic ships; modern is compiled in but not selected. See theme.h.
bool modern() { return false; }

int frame_w() { return modern() ? border_w() : 0; }

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
    // The greys are LVGL's own dark theme, which is what grumpyscreen has
    // always worn: the page is its card colour (every full-screen panel showed
    // that before it said so explicitly), a button is its DARK_COLOR_GREY, and
    // a hairline is that same grey -- subtle against the page, the way the
    // stock button matrices and text entries have always been outlined.
    cache[BG]         = lv_color_hex(0x282b30);  // LVGL DARK_COLOR_CARD
    // Modern lifts a surface off the page and gives it a grey light enough to
    // see; classic makes them the same colour, so a panel reads flat and its
    // hairline disappears into the page even where one is still drawn.
    cache[SURFACE]    = modern() ? lv_palette_darken(LV_PALETTE_GREY, 4) : cache[BG];
    cache[RAISED]     = modern() ? lv_palette_darken(LV_PALETTE_GREY, 3)
                                 : lv_color_hex(0x2f3237);  // LVGL DARK_COLOR_GREY
    cache[SELECTED]   = lv_palette_main(LV_PALETTE_GREY);
    cache[BORDER]     = modern() ? lv_palette_darken(LV_PALETTE_GREY, 3) : cache[RAISED];
    cache[BORDER_DIM] = lv_palette_darken(LV_PALETTE_GREY, 2);
    cache[TEXT]       = modern() ? lv_color_white()
                                 : lv_palette_lighten(LV_PALETTE_GREY, 5);  // DARK_COLOR_TEXT
    cache[TEXT_DIM]   = lv_palette_main(LV_PALETTE_GREY);
    cache[DISABLED]   = lv_palette_darken(LV_PALETTE_GREY, 1);
    cache[ON_PRIMARY] = lv_color_white();
    cache[DANGER]     = lv_palette_darken(LV_PALETTE_RED, 2);
    cache[WARNING]    = lv_palette_darken(LV_PALETTE_AMBER, 2);
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
  static const int g = scale_r(6);
  return g;
}

// The radii LVGL's default theme derives from the dpi, worked out once at the
// 480x272 baseline (dpi 90): a button is rounder than the box around it.
int border_w() { return 1; }  // a hairline stays a hairline at any size

// Modern rounds the whole UI off one base, two pixels apart, so a button is
// tighter than the card round it and a panel softer. Classic keeps the radii
// LVGL's own theme derives from the dpi, where a button is the rounder of the
// two and a panel no softer than a card.
int radius_sm() { return scale_r(modern() ? 4 : 7); }  // buttons, keys, tiles, swatches
int radius_md() { return scale_r(modern() ? 6 : 5); }  // cards, entries, key trays
int radius_lg() { return modern() ? scale_r(8) : radius_md(); }  // panels, popout boxes

// a popout sits one gap in from every screen edge and pads its content by a
// gap and a half, so the whole thing follows gap()
int popout_w() { return lv_disp_get_physical_hor_res(NULL) - 2 * gap(); }
int popout_max_h() { return lv_disp_get_physical_ver_res(NULL) - 2 * gap(); }
int popout_pad() { return gap() + gap() / 2; }
// box padding, the two borders, and 2px so integer rounding cannot wrap a row
int popout_row_w() { return popout_w() - 2 * popout_pad() - 2 * border_w() - 2; }

Styles &styles() {
  static Styles s;
  static bool ready = false;
  if (ready) return s;

  // The stock button: the grey LVGL has always used, rounded the same amount.
  // Nothing here says what a press looks like -- LVGL's own theme is this
  // one's parent, so its pressed darkening and disabled greying already reach
  // every button, key and image button, and that is the feedback the UI has
  // always had.
  lv_style_init(&s.btn);
  lv_style_set_pad_all(&s.btn, 0);
  lv_style_set_shadow_width(&s.btn, 0);
  lv_style_set_radius(&s.btn, radius_sm());
  lv_style_set_bg_color(&s.btn, col(RAISED));
  lv_style_set_bg_opa(&s.btn, LV_OPA_COVER);
  // Modern outlines its boxes, a button included -- Spoolman especially, which
  // stands in for an icon tile rather than reading as a plain button. Classic
  // leaves the fill to say where the button is.
  //
  // BORDER is tuned to sit against a surface, and in modern it is the same grey
  // as RAISED, so a button drawn with it frames itself in its own fill and the
  // hairline vanishes. A button asks for the lighter one instead.
  lv_style_set_border_width(&s.btn, frame_w());
  lv_style_set_border_color(&s.btn, col(BORDER_DIM));

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
  // No vertical padding, which is what grumpyscreen's tiles have always had:
  // the icon is fitted to the room the cell leaves, so a pad here is taken
  // straight off the icon and the tiles read noticeably smaller than they used
  // to. The sides stay tight so a seven-tile row fits one-word labels.
  lv_style_set_pad_hor(&s.tile, scale_r(4));
  lv_style_set_pad_ver(&s.tile, 0);
  lv_style_set_pad_row(&s.tile, scale_r(2));

  // A tapped card darkens, like every button: the accent is reserved for what
  // is active, and an active card says so with its border.
  static lv_color_filter_dsc_t darken;
  lv_color_filter_dsc_init(&darken, darken_filter_cb);
  lv_style_init(&s.card_pressed);
  lv_style_set_color_filter_dsc(&s.card_pressed, &darken);
  lv_style_set_color_filter_opa(&s.card_pressed, 35);

  // A list row (a fan slider, a temperature readout): no box, just a hairline
  // rule under it, which is what has always separated one row from the next.
  lv_style_init(&s.row_card);
  lv_style_set_radius(&s.row_card, modern() ? radius_md() : 0);
  lv_style_set_bg_color(&s.row_card, col(SURFACE));
  lv_style_set_bg_opa(&s.row_card, LV_OPA_COVER);
  lv_style_set_border_width(&s.row_card, border_w());
  lv_style_set_border_color(&s.row_card, col(BORDER));
  lv_style_set_border_side(&s.row_card, modern() ? LV_BORDER_SIDE_FULL : LV_BORDER_SIDE_BOTTOM);
  lv_style_set_pad_all(&s.row_card, gap());
  lv_style_set_pad_row(&s.row_card, 0);

  // A panel is a group, not a box: the page colour and no outline of its own,
  // so a screen full of them reads as one surface. What gets outlined is the
  // set of controls inside it (a key tray, an entry), never the grouping.
  lv_style_init(&s.panel);
  lv_style_set_radius(&s.panel, radius_lg());
  lv_style_set_bg_color(&s.panel, col(SURFACE));
  lv_style_set_bg_opa(&s.panel, LV_OPA_COVER);
  lv_style_set_border_width(&s.panel, frame_w());
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

  // A button whose label carries the feedback an icon's would: it keeps the
  // button face, and the word on it turns the accent under a finger. The
  // colours go on the button, not its label, so the label inherits them and
  // follows the button's state.
  lv_style_init(&s.text_btn);
  lv_style_set_text_color(&s.text_btn, col(TEXT));

  lv_style_init(&s.text_btn_pressed);
  lv_style_set_text_color(&s.text_btn_pressed, theme_primary());

  lv_style_init(&s.text_btn_disabled);
  lv_style_set_text_color(&s.text_btn_disabled, col(DISABLED));

  // A keyboard or keypad is all keys: no box of its own and no padding, so the
  // keys get the whole widget.
  lv_style_init(&s.key_body);
  lv_style_set_bg_opa(&s.key_body, LV_OPA_TRANSP);
  lv_style_set_border_width(&s.key_body, 0);
  lv_style_set_pad_all(&s.key_body, 0);
  lv_style_set_pad_gap(&s.key_body, scale_r(4));

  // A set of choices gets exactly one box. In classic the tray is that box: a
  // hairline round the set, never round each key. In modern the panel the
  // selector already sits in carries the border, so the tray goes invisible --
  // otherwise the group would be outlined twice, once inside the other.
  //
  // The tray is also what holds the keys off the edge of the group, so it is
  // what decides how big a key is, and skinning it must not resize anything.
  // The arithmetic below says so: whatever the tray does not spend on ink it
  // spends on padding. A border eats content space and padding does not, so
  // this is what keeps a key the same size in both looks.
  const int tray_border = modern() ? 0 : border_w();
  lv_style_init(&s.key_tray);
  lv_style_set_bg_opa(&s.key_tray, modern() ? LV_OPA_TRANSP : LV_OPA_COVER);
  lv_style_set_bg_color(&s.key_tray, col(SURFACE));
  lv_style_set_radius(&s.key_tray, radius_md());
  lv_style_set_border_width(&s.key_tray, tray_border);
  lv_style_set_border_color(&s.key_tray, col(BORDER));
  lv_style_set_pad_all(&s.key_tray, scale_r(4) + border_w() - tray_border);
  lv_style_set_pad_gap(&s.key_tray, scale_r(4));

  lv_style_init(&s.key);
  lv_style_set_radius(&s.key, radius_sm());
  lv_style_set_bg_color(&s.key, col(RAISED));
  lv_style_set_bg_opa(&s.key, LV_OPA_COVER);
  lv_style_set_border_width(&s.key, 0);
  lv_style_set_text_color(&s.key, col(TEXT));

  // the chosen key, and only it, wears the accent
  lv_style_init(&s.key_checked);
  lv_style_set_bg_color(&s.key_checked, theme_primary());
  lv_style_set_text_color(&s.key_checked, col(ON_PRIMARY));

  // an entry is a card-shaped box with the same hairline round it as always
  lv_style_init(&s.input);
  lv_style_set_radius(&s.input, radius_md());
  lv_style_set_border_width(&s.input, border_w());
  lv_style_set_border_color(&s.input, col(BORDER));
  lv_style_set_bg_color(&s.input, col(SURFACE));
  lv_style_set_bg_opa(&s.input, LV_OPA_COVER);
  lv_style_set_text_color(&s.input, col(TEXT));

  lv_style_init(&s.input_placeholder);
  lv_style_set_text_color(&s.input_placeholder, col(TEXT_DIM));

  // a list is square-cornered and outlined, so its cells meet the edge
  lv_style_init(&s.table);
  lv_style_set_radius(&s.table, modern() ? radius_lg() : 0);
  lv_style_set_bg_color(&s.table, col(SURFACE));
  lv_style_set_bg_opa(&s.table, LV_OPA_COVER);
  lv_style_set_border_width(&s.table, border_w());
  lv_style_set_border_color(&s.table, col(BORDER));
  lv_style_set_pad_all(&s.table, 0);

  lv_style_init(&s.track);
  lv_style_set_bg_color(&s.track, col(RAISED));
  lv_style_set_bg_opa(&s.track, LV_OPA_COVER);
  lv_style_set_border_width(&s.track, 0);

  // a slider's groove is a wash of the accent rather than a grey channel,
  // which is how every slider in the UI has always been drawn
  lv_style_init(&s.groove);
  lv_style_set_bg_color(&s.groove, theme_primary());
  lv_style_set_bg_opa(&s.groove, LV_OPA_20);
  lv_style_set_border_width(&s.groove, 0);

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
  lv_style_set_bg_color(&s.knob, theme_primary());
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

std::string recolor(Colour c) { return fmt::format("#{:06x} ", lv_color_to32(col(c)) & 0xffffff); }

lv_obj_t *create_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_add_style(row, &styles().row, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

// A list that fits still slides under a finger and springs back, which reads
// as a broken tap. Keep the scroll flag in step with the content instead: set
// on overflow, cleared when everything fits.
void manage_scroll(lv_obj_t *scrollee) {
  lv_obj_set_scrollbar_mode(scrollee, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(scrollee, LV_DIR_VER);
  auto follow = [](lv_event_t *e) { refresh_scroll(lv_event_get_target(e)); };
  lv_obj_add_event_cb(scrollee, follow, LV_EVENT_SIZE_CHANGED, NULL);
  refresh_scroll(scrollee);
}

void refresh_scroll(lv_obj_t *scrollee) {
  const bool overflows = lv_obj_get_scroll_top(scrollee) > 0 || lv_obj_get_scroll_bottom(scrollee) > 0;
  if (overflows) lv_obj_add_flag(scrollee, LV_OBJ_FLAG_SCROLLABLE);
  else lv_obj_clear_flag(scrollee, LV_OBJ_FLAG_SCROLLABLE);
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

lv_obj_t *create_text_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data) {
  lv_obj_t *btn = create_flat_btn(parent, text, cb, user_data);
  lv_obj_add_style(btn, &styles().text_btn, 0);
  lv_obj_add_style(btn, &styles().text_btn_pressed, LV_STATE_PRESSED);
  lv_obj_add_style(btn, &styles().text_btn_disabled, LV_STATE_DISABLED);
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
    if (label) lv_obj_set_style_text_color(label, col(TEXT), 0);
  } else {
    // grey text on the plain button grey: reads as off, unlike TEXT which
    // would make it identical to a live flat button
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn, col(RAISED), 0);
    if (label) lv_obj_set_style_text_color(label, col(DISABLED), 0);
  }
}

}  // namespace Theme
