# UI style

`src/theme.h` is the one place the UI's look is defined, and `src/icons.h` the
one place its images are. A panel that builds itself from those two follows the
whole application when a token changes, at every resolution.

The *structure* of the look (sizes, radii, which looks are used) is not
configurable. The *palette* is: every `Colour` role can be set from `[theme]`
as `<role>_colour: 0xRRGGBB` (see `grumpyscreen.cfg` for the list), on top of
the `primary_colour` and `secondary_colour` accents.

`grumpyscreen.cfg` is the one place the palette is spelled out. Every key is
required and the code carries no colour defaults: a deployment ships a complete
`grumpyscreen.cfg` and customises through its override file, so a missing or
unparseable key is a broken config. It is logged and drawn magenta. `col()`
reads the keys once, the first time a colour is asked for, so a change needs a
restart. Nothing is derived: set every role a palette needs, or override only
the ones you want to change and inherit the rest.

A custom theme is therefore an override block. A light one:

```ini
[theme]
primary_colour: 0x1976D2
bg_colour: 0xF2F2F2
surface_colour: 0xF2F2F2
raised_colour: 0xE0E0E0
border_colour: 0xE0E0E0
text_colour: 0x212121
```

For a deliberately excessive example, use the included `themes/garish.cfg`
override. Point `CONFIG_OVERRIDE_FILE` at it before starting the screen:

```sh
CONFIG_OVERRIDE_FILE=themes/garish.cfg ./build/sdl/grumpyscreen
```

To preview it in the simulator while keeping the generated printer state, use
the simulator's override hook:

```sh
SIM_CFG_OVERRIDE=themes/garish.cfg ./sim.sh
```

It changes only the `[theme]` keys, so all printer and simulator settings
continue to come from `grumpyscreen.cfg`.

When adding a `Colour`, add its `[theme]` key to `grumpyscreen.cfg` and to
`col()`, and never reach for `lv_palette_*` in a panel: a role that is not in
`Colour` cannot be themed.

Icons are white glyphs recoloured by the theme: `icon_colour` at rest, the
accent while pressed, `disabled_colour` when off (`Theme::style_icon()`, which
every icon wears). Stop, shutdown, factory reset, switch to stock and update rest
in `danger_colour` and cooldown in `info_colour`; the choice is made in
`style_icon()`. Every bitmap in `assets/` must be plain white (alpha is the
shape), because the recolour replaces the colour outright -- a coloured bitmap
would be flattened.

Custom icons: replace the `.c` bitmaps in `assets/material` (and
`assets/material_46` for the small-screen build) and rebuild. The symbol names
are what `icons.h` declares, so keep them.

## The look

A panel is a grouping, not a box: it is the page colour with no outline, tiles
are bare icon-and-label, list rows are separated by a rule, and the only
outlined things are the controls -- a card, a text entry, a table, the tray
round a set of keys. (An alternative "modern" look with outlined surfaces once
lived behind a switch; it was never released and has been removed. It is in the
git history before the commit that removed it.)

**Exactly one box round a thing.** A set of choices is boxed once, by
`key_tray`. Restoring a border without checking what already draws one gives a
box inside a box.

## For panel authors

Never write a colour or a pixel size directly. Ask `Theme` for it:

```cpp
#include "theme.h"
using namespace Theme;

lv_obj_t *page = create_screen(NULL);          // a full-screen panel root, padded by gap()
lv_obj_t *row  = create_row(page);             // invisible layout container, children gap() apart
lv_obj_t *go   = create_text_btn(row, "Load", cb, this);
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
`use_plain()`. The icon then shrinks to fit whatever cell the tile lands in (a
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
control keys and stay plain), `lv_textarea` and `lv_dropdown` (and its list, whose rows take `option`) get `input`, `lv_table` gets
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
