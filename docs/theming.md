# UI style

`src/theme.h` is the one place the UI's look is defined, and `src/icons.h` the
one place its images are. A panel that builds itself from those two follows the
whole application when a token changes, at every resolution.

The look itself is not configurable: `[theme] primary_colour` and
`secondary_colour` are the two accents users have always been able to set, and
everything else is the greys and sizes in `theme.cpp`. Restyling means editing
those; there is no `[theme]` key to add, on purpose.

Custom icons: replace the `.c` bitmaps in `assets/material` (and
`assets/material_46` for the small-screen build) and rebuild. The symbol names
are what `icons.h` declares, so keep them.

## Two looks, one switch

`Theme::modern()` picks between them. It returns `false`: **classic ships**,
and modern is compiled in but not selected. It is deliberately not a config
key -- there is no `[theme]` setting for it and it is not a user choice. It is
kept in the tree so the second look can be finished and released later without
being written again. Flip that one function to see it.

*Classic* is what grumpyscreen has always worn. A panel is a grouping, not a
box: it is the page colour with no outline, tiles are bare icon-and-label, list
rows are separated by a rule, and the only outlined things are the controls --
a card, a text entry, a table, the tray round a set of keys.

*Modern* has surfaces. A panel is an outlined box a shade off the page, a tile
is a card that darkens under a finger, a list row is a card, a plain button and
the nav bar's edge get a hairline, and corners are rounder.

Everything that differs lives behind `modern()`, and almost all of it is in
`theme.cpp` -- the palette, the three radii, and which of `panel` / `btn` /
`key_tray` / `row_card` / `table` carry a border. Panels should not test
`modern()` themselves; they ask for `frame_w()`, which is `border_w()` in
modern and nothing in classic, and let the shared styles do the rest.

Three differences are structural and cannot be a token, so they are the only
`modern()` tests outside `theme.cpp`:

- `ButtonContainer::use_card()` adds the card styles in modern and is exactly
  `use_plain()` in classic, so a panel calls it either way.
- `SensorContainer` draws the sensor's colour as a floating bar in modern and
  as the row's own left border in classic. An object has one border: classic
  spends it on the stripe, which is why those rows have no frame in that look.
- `MmuPanel`'s lane tiles take the card's surface in modern and the button grey
  in classic.

**Exactly one box round a thing.** This is the rule the two looks are most
easily broken by. A set of choices is boxed once: in classic that box is
`key_tray`, in modern the `panel` the selector already sits in, and the tray
goes invisible there. Restoring a border without checking what already draws
one gives a box inside a box. The tray's *padding* is the same in both looks
even so, because it is what holds the keys off the edge of the group and so
decides how big a key is -- classic spends a pixel of that inset on its
hairline, modern spends the same pixel on padding.

## For panel authors

Never write a colour or a pixel size directly. Ask `Theme` for it:

```cpp
#include "theme.h"
using namespace Theme;

lv_obj_t *page = create_screen(NULL);          // a full-screen panel root, padded by gap()
lv_obj_t *row  = create_row(page);             // invisible layout container, children gap() apart
lv_obj_t *go   = create_flat_btn(row, "Load", cb, this);
lv_obj_set_style_bg_color(go, col(DANGER), 0); // a role, never a grey
lv_obj_set_style_height(go, scale_r(44), 0);   // a size at the 480x272 baseline
```

**Sizes.** `scale_w()`, `scale_h()` and `scale_r()` take a measurement at the
480x272 design baseline and return it for the display that is actually
attached; `scale_font()` does the same for text (montserrat 12 to 22 are always
compiled in and 24 to 28 only when `GUPPY_SMALL_SCREEN` is off, so 12/14/16/18
at 480 become 20/24/26/28 at 800 and the size hierarchy survives without the
small build carrying fonts it cannot use). Lay a panel out once, at 480x272, and it renders at every
resolution. `hal_init()` also scales the display's dpi from the same baseline,
so LVGL's own default paddings on anything not given a shared style follow the
screen too, and the simulator matches the device. The LVGL theme is created
dark or light from the brightness of `col(BG)`, and `screen` sets `col(TEXT)`
on every page, so a light palette stays a one-place edit.

**Local beats shared.** LVGL resolves a local `lv_obj_set_style_*` write
before any style added with `lv_obj_add_style`. Write `pad_all 0` on an object
and then add `card`, and the card's padding is gone. Never write a local
property that a shared style you add also sets unless the override is the point.

This is what replaced `#ifdef GUPPY_SMALL_SCREEN` in panel code, so do not add
new ones. The define still exists, but it now lives only in the makefiles and
means two build-time things: which directory of icon bitmaps is compiled in
(`assets/material` at 64px, `assets/material_46` at 46px -- 16M against 7.9M of
image data, so it is a real size saving and not a layout switch) and the size
of the desktop SDL window, which has to be decided before any display exists.
No source file branches on it.

There is deliberately **no** "is this a small screen" helper. One design
baseline scales; two tuned size sets do not, and they rot as soon as a third
resolution shows up. Where a proportion genuinely will not do, size the widget
to its content instead of branching -- `simple_dialog.h` is the worked example:
it was two sets of height percentages picked by screen size, and is now
`LV_SIZE_CONTENT` with a `max_height` cap, which fits any amount of text at
any resolution.

The same rule covers a variable number of things. A printer might have three
fans or five, so never derive a row count from the screen height. Build the
rows, call `lv_obj_update_layout()`, and ask `lv_obj_get_scroll_bottom()`
whether they overflow -- `fan_panel.cpp` and `led_panel.cpp` do exactly that,
and neither one knows how tall a row is.

**Layout.** One application, one set of margins. Every panel's root is a
`create_screen()` (padded by `gap()` all round, children `gap()` apart) and
every layout container a `create_row()` (children `gap()` apart), and nothing
else adds space between elements: elements fill the room they are given.
Anything a finger taps is `touch_h()` tall wherever the layout allows. In a grid that means
`LV_GRID_ALIGN_STRETCH`; in a flex column, `lv_obj_set_flex_grow(..., 1)` on
the thing that should take the slack (the temperature chart on the main tab,
the fan rows when they do not overflow). Content-sized rows (`LV_GRID_CONTENT`)
are for a row of selectors or a Back tile whose height is its own business; do
not STRETCH an item vertically into one of those, because lv_grid measures the
row from the item's current height and the two chase each other upward.

**Tiles.** An icon-and-label action is a `ButtonContainer` wearing
`use_card()`. The icon then shrinks to fit whatever cell the tile lands in (a
tile is never clipped, and no panel picks icon sizes), so a five-row homing
grid and a four-row extruder column both work at 272px. Back is a tile like
any other: put it in the grid where there is a grid, or call
`float_bottom_right()` on panels whose content is a list. Selectors
(`Selector`) are titled panels and go in the grid with `STRETCH` across their
columns.

**By class.** `GuppyScreen::new_theme_apply_cb` (the LVGL theme callback)
styles whole widget classes at creation: every `lv_btn` gets `btn`, every
`lv_btnmatrix` and `lv_keyboard` gets `key_tray`/`key` (a matrix's checked key
is the chosen value and wears the accent; a keyboard's checked keys are its
control keys and stay plain), `lv_textarea` gets `input`, `lv_table` gets
`table`, and sliders, bars, switches, arcs and spinners get `track`/`fill`/
`knob`. A panel never restyles those itself; it adds a style on top only when
the widget has a different role (the console log is a textarea wearing
`panel`). Extend that callback when a new widget class appears, do not decorate
it in one panel.

**Looks.** `styles()` holds the recurring ones -- `card`, `panel`, `btn`,
`popout_box`, `swatch`, `row`, `screen`, `icon_pressed`/`icon_disabled` -- as shared `lv_style_t`. Prefer them to local
`lv_obj_set_style_*` calls: shared styles are a pointer reference per widget,
local ones allocate, so this is also the cheaper of the two at runtime. Only
construction-time looks belong in `styles()`; anything that changes with state
(a spool's colour, a button greying out) stays a local write.

**Radii.** `radius_sm/md/lg()` all derive from one base, two pixels apart, so
the whole UI rounds off from a single number (0 today: square corners, as
grumpyscreen has always had). Pick by the size of the thing: `sm` for buttons
and tiles, `md` for cards, `lg` for panels and popouts.

**Popouts.** A popout is `styles().popout` (the dim over the page) holding a
`styles().popout_box`, which is `popout_w()` wide, at most `popout_max_h()`
tall, and lays its children out `gap()` apart. `popout_row_w()` is the width a
row of tiles inside it has to share; divide by the tile count and subtract
`gap()` and a full row never wraps. Dialogs (`simple_dialog.h`) are the same
two styles with a title bar and a button row.

`src/mmu_panel.cpp` is the reference implementation -- it is where all of this
came from.
