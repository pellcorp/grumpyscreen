// Exercise LVGL's real glyph lookup, fallback resolution and bitmap retrieval.
#include "lvgl/lvgl.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

LV_FONT_DECLARE(dejavusans_mono_14);
#ifndef COSMOS
LV_FONT_DECLARE(dejavusans_mono_hh_14);
#endif

int main() {
  const lv_font_t *font = &dejavusans_mono_14;
  lv_font_glyph_dsc_t ascii;
  assert(lv_font_get_glyph_dsc(font, &ascii, 'A', 0));
  for (uint32_t letter : {0x00A0, 0x00EA, 0x0394}) {
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(font, &glyph, letter, 0));
    assert(glyph.resolved_font == font);
    assert(glyph.adv_w == ascii.adv_w);
  }
  const uint32_t symbols[] = {
    0x2014, 0x2022, 0x2500, 0x2501, 0x2502, 0x2508, 0x250C, 0x2510,
    0x2514, 0x2518, 0x251C, 0x2524, 0x252B, 0x252C, 0x2534, 0x253C,
    0x2550, 0x2588, 0x258C, 0x25A0, 0x25B6, 0x25B7, 0x25BC, 0x25C0,
    0x25C1, 0x25C9, 0x25CF, 0x25EF,
  };
  for (uint32_t letter : symbols) {
    lv_font_glyph_dsc_t glyph;
    bool found = lv_font_get_glyph_dsc(font, &glyph, letter, 0);
#ifdef COSMOS
    assert(font->fallback == nullptr);
    assert(!found || glyph.is_placeholder);
#else
    assert(font->fallback == &dejavusans_mono_hh_14);
    assert(found && !glyph.is_placeholder);
    assert(glyph.resolved_font == &dejavusans_mono_hh_14);
    assert(glyph.adv_w == ascii.adv_w);
    assert(glyph.box_w > 0 && glyph.box_h > 0);
    assert(lv_font_get_glyph_bitmap(glyph.resolved_font, letter) != nullptr);
#endif
  }
  std::puts("console font tests passed");
}
