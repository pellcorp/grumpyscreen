#ifndef __THEME_H__
#define __THEME_H__

#include "lvgl/lvgl.h"

#include <string>

// The one place the UI's look is defined.
//
// Panels build their widgets from these tokens instead of writing colours and
// pixel sizes inline, so restyling grumpyscreen is an edit to [theme] in
// grumpyscreen.cfg rather than a sweep through twenty source files. Change one
// token, every panel wearing it follows.
//
// Everything is lazy: the colours need the config loaded and the sizes need a
// display, and both exist by the time the first panel is built. Nothing here
// costs anything until a panel asks for it.
namespace Theme {

// --- palette --------------------------------------------------------------
//
// Roles, not colour names: a panel asks for SURFACE, never "grey 4", so a
// light theme is a config edit and not an inversion of every call site. Each
// role reads /theme/<key>_colour and falls back to the stock grey when the key
// is absent, so an existing config renders exactly as it does today.
enum Colour {
  BG,          // the page behind everything, and full-screen overlays
  SURFACE,     // cards, panels, popout boxes: the standard raised background
  RAISED,      // buttons, and the pressed state of a surface: one step lighter
  BORDER,      // hairline around a surface
  BORDER_DIM,  // hairline around a small tile (a swatch, a spool)
  TEXT,        // default label
  TEXT_DIM,    // section titles and secondary text
  DISABLED,    // greyed-out label, or the recolour on a disabled icon
  ON_PRIMARY,  // text and glyphs drawn on the primary colour
  DANGER,      // faults
  WARNING,     // something the user should notice that is not a fault
  COLOUR_COUNT
};

lv_color_t col(Colour c);
// "/theme/<key>" as "0x2196F3" (or "2196F3") -> colour; def when absent or unparsable
lv_color_t cfg_col(const char *key, lv_color_t def);

// the LVGL theme already owns these two (guppyscreen.cpp reads them from
// [theme] and hands them to hal_init); ask it rather than parsing them again
lv_color_t theme_primary();
lv_color_t theme_secondary();

// --- metrics --------------------------------------------------------------
//
// The design baseline is the 480x272 small screen: every structural size is
// that design times the current display scale, so any resolution renders the
// same layout, just larger. At 480x272 all three helpers are identity.
int scale_w(int px);
int scale_h(int px);
// squares and circles follow the tighter axis so they stay round
int scale_r(int px);

// snap to the smallest enabled montserrat (12..28) that fits the scaled size.
// At 480x272 every lookup returns px unchanged; at 800x480 12/14/16/18 land on
// 20/24/26/28, so the size hierarchy survives the trip.
const lv_font_t *scale_font(int px);

// the one spacing token: screen edges, headers, rows, cards. Also the default
// child gap of every row and screen, so nothing needs to set pad_row/pad_column
// unless it wants something other than the standard gap.
int gap();
// /theme/scrollbars, default on: off keeps every list swiping but draws no
// bar, LVGL's own or the side ones below
bool scrollbars();
// three radii, all derived from /theme/radius, so rounding the whole UI off is
// a single number
// the hairline round cards, panels, popouts and entries: /theme/border, in
// pixels, 0 for none
int border_w();
int radius_sm();  // buttons, swatches
int radius_md();  // cards
int radius_lg();  // panels, popout boxes

// popout boxes span the screen minus one gap on every side, and pad their
// content by a gap and a half (the popout_box style already applies it)
int popout_w();
int popout_max_h();
int popout_pad();
// usable row width inside a popout: box padding, 1px borders, and a little
// headroom so integer rounding can never wrap a full row of tiles. Lay tiles
// out with pad_column = gap() (the popout_box default) and this is exact.
int popout_row_w();

// --- shared styles --------------------------------------------------------
//
// The recurring looks, defined once and shared by every widget that wears one,
// instead of a dozen local style properties per object. Shared styles are
// pointer references; local ones allocate per object, so this is also the
// cheaper of the two at runtime.
//
// Only construction-time looks belong here; anything that changes with state
// (a spool's colour, a button greying out) stays a local style write.
struct Styles {
  lv_style_t btn, btn_pressed;       // flat text button
  lv_style_t card, card_pressed;     // tappable card
  lv_style_t tile;                   // a card holding icon over label: its paddings
  lv_style_t row_card;               // a card used as a full-width list row
  lv_style_t panel;                  // a titled column of controls
  lv_style_t popout;                 // full-screen dim behind a popout
  lv_style_t popout_box;             // the popout itself
  lv_style_t row;                    // invisible layout row/box
  lv_style_t screen;                 // an opaque full-screen overlay panel; sets the text colour
  lv_style_t swatch, swatch_pressed; // small square tile
  lv_style_t dim_disabled;           // tiles fade when not editable
  lv_style_t dim_label;              // section titles and secondary text
  lv_style_t icon_pressed;           // an icon under a finger: accent recolour
  lv_style_t icon_disabled;          // a greyed-out icon
  // applied by widget class from the theme callback, so every keyboard, text
  // entry and table in the UI wears them without the panel doing anything
  lv_style_t key_tray;               // keyboard/btnmatrix body: invisible
  lv_style_t key, key_pressed;       // one key
  lv_style_t input;                  // a text entry
  lv_style_t input_placeholder;      // its hint text
  lv_style_t table;                  // a list/table: the panel look, cells padded
  lv_style_t table_cell;
  lv_style_t track;                  // the groove of a slider, bar, switch, arc, spinner
  lv_style_t fill;                   // the filled part of any of those: the accent
  lv_style_t knob;                   // a slider or switch knob
  lv_style_t slider;                 // a slider's track, inset so its knob stays inside the widget
  lv_style_t arc_track, arc_fill;    // the same two for arcs and spinners (no box)
  lv_style_t scrollbar;              // every scrollbar: a thin solid thumb in its own lane
};

Styles &styles();

// opens a run of a theme colour in a label with lv_label_set_recolor() on:
// "#rrggbb " -- the caller closes it with '#'. Captions in the dim text colour
// over their values, for one label that reads as a form.
std::string recolor(Colour c);

// LVGL draws a scrollbar inside the object that scrolls, over its contents and
// across its rounded corners. This one is drawn beside it instead: a thin lane
// inserted just before the object in its flex row, holding a thumb that follows
// the object's scrolling. The lane is hidden, and the object takes its width
// back, while the content fits. Content changes are not events, so call
// refresh_side_scrollbar() after repopulating the object.
void add_side_scrollbar(lv_obj_t *scrollee);
void refresh_side_scrollbar(lv_obj_t *scrollee);

// --- images ---------------------------------------------------------------

// Zoom an image (source already set) to fit inside w x h, keeping its aspect,
// never above max_zoom (LV_IMG_ZOOM_NONE: never enlarge a bitmap, it only
// blurs). The image is put in REAL size mode and its box refreshed, so layout
// sees exactly the drawn size. Always use this rather than lv_img_set_zoom:
// this LVGL does not refresh a REAL-mode image's box on zoom, and a stale box
// tiles the drawing inside it. Returns the zoom applied.
int fit_img(lv_obj_t *img, int w, int h, int max_zoom = LV_IMG_ZOOM_NONE);
// an LV_EVENT_SIZE_CHANGED handler: fits the target's first child (an image)
// to the target's content height, for a card whose icon is its first child
void fit_first_icon(lv_event_t *e);

// the height of anything a finger taps: buttons, list rows, entry lines
int touch_h();
// a finger-sized slider's track height; no slider in the UI is taller, so the
// shared slider style insets every track by this one's knob overhang
int slider_h();
// how far a slider's knob reaches past the end of its track: half the track
// height plus the knob's pad. A panel that wants a longer track can inset by
// less and let the knob hang into room it knows it has.
int knob_overhang(int track_h);

// --- widget factories -----------------------------------------------------

// a plain container: no background of its own, no scrolling, just layout
lv_obj_t *create_row(lv_obj_t *parent);
// a full-screen panel root: paints the page, fills its parent (the active
// screen when parent is NULL), padded by gap() all round, no scrolling. The
// caller still hides it / moves it to the background as its lifecycle needs.
lv_obj_t *create_screen(lv_obj_t *parent);
lv_obj_t *create_flat_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data);
void set_btn_label(lv_obj_t *btn, const char *text);
// enabled buttons carry their own colour, disabled ones all look the same
void set_action_btn(lv_obj_t *btn, bool enabled, lv_color_t enabled_colour);

}  // namespace Theme

#endif  // __THEME_H__
