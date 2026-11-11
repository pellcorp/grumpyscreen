// The app's text at any size. Theme::scale_font asks for a size in proportion
// to the screen, but LVGL's built-in Montserrat stops at 28px: about two thirds
// of what a phone needs and a third of a tablet's, so the text shrank inside
// boxes sized for it. Here FreeType draws Montserrat Medium (the face those
// built-in fonts are made from) at exactly the size asked for, with LVGL's
// symbols (LV_SYMBOL_*) from the same FontAwesome file the built-in fonts take
// them from. Both are embedded by grumpy_fonts_data.S.
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lvgl/lvgl.h"
#include "logger.h"
#include "platform.h"

extern "C" {
extern const unsigned char grumpy_font_text[], grumpy_font_text_end[];
extern const unsigned char grumpy_font_symbols[], grumpy_font_symbols_end[];
}

namespace {

struct Glyph {
  bool present = false;
  lv_font_glyph_dsc_t dsc{};
  std::vector<uint8_t> bitmap;  // 8bpp coverage, box_w x box_h
};

struct Face {
  FT_Face face = nullptr;
  lv_font_t font{};
  std::unordered_map<uint32_t, Glyph> glyphs;  // rendered once, on first use
};

FT_Library library = nullptr;
// by (file, pixel size); fonts live as long as the app, as LVGL's do
std::map<std::pair<const unsigned char *, int>, std::unique_ptr<Face>> faces;

const Glyph &glyph(Face &f, uint32_t letter) {
  auto it = f.glyphs.find(letter);
  if (it != f.glyphs.end()) return it->second;
  Glyph &g = f.glyphs[letter];
  const FT_UInt index = FT_Get_Char_Index(f.face, letter);
  // light hinting keeps the shapes; full hinting distorts them at these sizes
  if (index == 0 || FT_Load_Glyph(f.face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
    return g;  // not in this face: LVGL tries the fallback
  }
  const FT_GlyphSlot slot = f.face->glyph;
  const FT_Bitmap &bm = slot->bitmap;
  g.present = true;
  g.dsc.adv_w = static_cast<uint16_t>((slot->advance.x + 32) >> 6);
  g.dsc.box_w = static_cast<uint16_t>(bm.width);
  g.dsc.box_h = static_cast<uint16_t>(bm.rows);
  g.dsc.ofs_x = static_cast<int16_t>(slot->bitmap_left);
  // LVGL measures the box's bottom from the baseline
  g.dsc.ofs_y = static_cast<int16_t>(slot->bitmap_top - static_cast<int>(bm.rows));
  g.dsc.bpp = 8;
  g.bitmap.resize(static_cast<size_t>(bm.width) * bm.rows);
  for (unsigned int y = 0; y < bm.rows; y++) {
    std::memcpy(&g.bitmap[static_cast<size_t>(y) * bm.width], bm.buffer + y * bm.pitch, bm.width);
  }
  return g;
}

// LVGL hands the font back as const; the glyph cache behind it is ours to fill
Face &face_of(const lv_font_t *font) {
  return *const_cast<Face *>(static_cast<const Face *>(font->dsc));
}

bool get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t) {
  const Glyph &g = glyph(face_of(font), letter);
  if (!g.present) return false;
  *dsc = g.dsc;
  return true;
}

const uint8_t *get_glyph_bitmap(const lv_font_t *font, uint32_t letter) {
  const Glyph &g = glyph(face_of(font), letter);
  return g.present && !g.bitmap.empty() ? g.bitmap.data() : nullptr;
}

const lv_font_t *load(const unsigned char *data, const unsigned char *end, int px,
                      const lv_font_t *fallback) {
  auto key = std::make_pair(data, px);
  auto it = faces.find(key);
  if (it != faces.end()) return &it->second->font;

  if (library == nullptr && FT_Init_FreeType(&library) != 0) return nullptr;
  auto f = std::make_unique<Face>();
  if (FT_New_Memory_Face(library, data, static_cast<FT_Long>(end - data), 0, &f->face) != 0 ||
      FT_Set_Pixel_Sizes(f->face, 0, static_cast<FT_UInt>(px)) != 0) {
    LOG_ERROR("unable to load the {}px font", px);
    return nullptr;
  }
  const FT_Size_Metrics &m = f->face->size->metrics;
  const int ascender = static_cast<int>((m.ascender + 63) >> 6);
  const int descender = static_cast<int>(-(m.descender >> 6));  // below the baseline, positive
  f->font.get_glyph_dsc = get_glyph_dsc;
  f->font.get_glyph_bitmap = get_glyph_bitmap;
  f->font.line_height = static_cast<lv_coord_t>(ascender + descender);
  f->font.base_line = static_cast<lv_coord_t>(descender);
  f->font.subpx = LV_FONT_SUBPX_NONE;
  f->font.underline_position = static_cast<int8_t>(
      FT_MulFix(f->face->underline_position, m.y_scale) >> 6);
  f->font.underline_thickness = static_cast<int8_t>(
      std::max<FT_Pos>(1, FT_MulFix(f->face->underline_thickness, m.y_scale) >> 6));
  f->font.fallback = fallback;
  f->font.dsc = f.get();
  const lv_font_t *font = &f->font;
  faces.emplace(key, std::move(f));
  return font;
}

}  // namespace

const lv_font_t *android_text_font(int px) {
  if (px < 1) px = 1;
  // symbols at the text's size; anything in neither file falls back to the
  // largest built-in font rather than to nothing
  const lv_font_t *symbols = load(grumpy_font_symbols, grumpy_font_symbols_end, px,
                                  &lv_font_montserrat_28);
  const lv_font_t *text = load(grumpy_font_text, grumpy_font_text_end, px,
                               symbols != nullptr ? symbols : &lv_font_montserrat_28);
  return text != nullptr ? text : &lv_font_montserrat_28;
}
