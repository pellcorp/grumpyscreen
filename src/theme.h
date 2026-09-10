#ifndef __THEME_H__
#define __THEME_H__

#include "lvgl/lvgl.h"

#include <string>

// The one place the UI's look is defined.
//
// Panels build their widgets from these tokens instead of writing colours and
// pixel sizes inline, so restyling grumpyscreen is an edit here rather than a
// sweep through twenty source files. Change one token, every panel wearing it
// follows.
//
// Everything is lazy: the sizes need a display, which exists by the time the
// first panel is built. Nothing here costs anything until a panel asks for it.
namespace Theme {

// --- which look ------------------------------------------------------------
//
// Two looks are compiled in. "Modern" is the one with surfaces: a panel is an
// outlined box a shade off the page, a tile is a card, a list row is a card,
// corners are rounder. "Classic" is what grumpyscreen has always worn: panels
// and tiles are the page itself, rows are separated by a rule, and the only
// boxes are the controls.
//
// Classic is what ships. Modern is not reachable from grumpyscreen.cfg on
// purpose -- there is no [theme] key for it and it is not a user setting; it is
// kept in the build so the look can be finished and released later without
// having to be written again. Flip this one function to see it.
//
// Anything that differs between the two belongs behind modern(), and nearly
// all of it is a colour, a radius or a border width in theme.cpp. Only three
// differences are structural enough to need a test elsewhere:
// ButtonContainer::use_card, SensorContainer's colour stripe and MmuPanel's
// lane tiles. docs/theming.md says why each one cannot be a token.
bool modern();

// The hairline round a box that only the modern look draws -- a panel, a
// button, the rule beside the nav bar. border_w() there, nothing in classic.
// Ask for this instead of testing modern() at the call site: a widget should
// say "frame me", not "which theme is this".
//
// Not to be confused with border_w(), the width of a hairline, which both
// looks draw round the things that are outlined in either: cards, entries,
// tables, key trays.
int frame_w();

// --- palette --------------------------------------------------------------
//
// Roles, not colour names: a panel asks for SURFACE, never "grey 4", so a
// restyle is an edit to col() and not an inversion of every call site. The
// values are the greys grumpyscreen has always worn.
enum Colour {
  BG,          // the page behind everything, and full-screen overlays
  SURFACE,     // cards, panels, popout boxes: the page colour, so they read flat
  RAISED,      // buttons, and a surface lifted off the page: one step lighter
  SELECTED,    // the slot behind a selected nav tab: a light grey wash
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
// [theme] primary_colour / secondary_colour, the two accents a user has always
// been able to set: "0x2196F3" (or "2196F3") -> colour, def when absent or
// unparsable. Nothing else in the look is configurable.
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
// the hairline round cards, panels, popouts and entries, in pixels at the
// design size; 0 for none, which is what the classic look wears
int border_w();
// three radii, all derived from one base, so rounding the whole UI off is a
// single number
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
  lv_style_t btn;                    // flat text button
  lv_style_t card, card_pressed;     // a tappable outlined box; pressed darkens
  lv_style_t tile;                   // an icon-over-label tile: its paddings
  lv_style_t row_card;               // a full-width list row, ruled off from the next
  lv_style_t panel;                  // a titled column of controls
  lv_style_t popout;                 // full-screen dim behind a popout
  lv_style_t popout_box;             // the popout itself
  lv_style_t row;                    // invisible layout row/box
  lv_style_t screen;                 // an opaque full-screen overlay panel; sets the text colour
  lv_style_t swatch;                 // small square tile
  lv_style_t dim_disabled;           // tiles fade when not editable
  lv_style_t dim_label;              // section titles and secondary text
  lv_style_t icon_pressed;           // an icon under a finger: accent recolour
  lv_style_t icon_disabled;          // a greyed-out icon
  lv_style_t text_btn;               // a button whose label carries the feedback
  lv_style_t text_btn_pressed;       // its word under a finger: accent
  lv_style_t text_btn_disabled;      // and greyed out when it is off
  // applied by widget class from the theme callback, so every keyboard, text
  // entry and table in the UI wears them without the panel doing anything
  lv_style_t key_body;               // keyboard/btnmatrix body: bare, all room to the keys
  lv_style_t key_tray;               // a selector's outlined tray round a set of keys
  lv_style_t key, key_checked;       // one key, and the chosen one
  lv_style_t input;                  // a text entry
  lv_style_t input_placeholder;      // its hint text
  lv_style_t table;                  // a list/table: outlined, cells padded
  lv_style_t table_cell;
  lv_style_t track;                  // the groove of a switch
  lv_style_t groove;                 // the groove of a slider or bar: a wash of the accent
  lv_style_t fill;                   // the filled part of any of those: the accent
  lv_style_t knob;                   // a slider knob (a switch keeps LVGL's own)
  lv_style_t slider;                 // a slider's track, inset so its knob stays inside the widget
  lv_style_t arc_track, arc_fill;    // the same two for arcs and spinners (no box)
};

Styles &styles();

// opens a run of a theme colour in a label with lv_label_set_recolor() on:
// "#rrggbb " -- the caller closes it with '#'. Captions in the dim text colour
// over their values, for one label that reads as a form.
std::string recolor(Colour c);

// Nothing in the UI draws a scrollbar; lists are swiped. What is left to
// manage is the bounce: an object that is scrollable but has nothing to scroll
// still slides under a finger and springs back. These keep the scroll flag in
// step with the content, so a list that fits sits still. Content changes are
// not events, so call refresh_scroll() after repopulating the object.
void manage_scroll(lv_obj_t *scrollee);
void refresh_scroll(lv_obj_t *scrollee);

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
// A flat button whose label does what an icon does: the word turns the accent
// under a finger and greys out when disabled, rather than the box alone
// reacting. Enable and disable it with LV_STATE_DISABLED, not set_action_btn.
lv_obj_t *create_text_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data);
void set_btn_label(lv_obj_t *btn, const char *text);
// An action button carries the plain button grey unless its colour says
// something the label cannot: red for a verb that moves filament out, the
// accent for a setting that is currently on. Disabled ones all look the same.
void set_action_btn(lv_obj_t *btn, bool enabled, lv_color_t enabled_colour);

}  // namespace Theme

#endif  // __THEME_H__
