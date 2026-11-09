/*
 * Resolution-independent icons for the Android build.
 *
 * The desktop and printer builds compile assets/material (64px) or
 * assets/material_46 bitmaps; a phone or tablet screen wants them two to
 * five times that, and zooming a 64px bitmap up only blurs it. Here every
 * icon is instead its SVG (assets/material_svg, embedded by
 * embed_assets.cmake) behind an LV_IMG_CF_USER_ENCODED_0 descriptor of the
 * same name whose data is a grumpy_icon_src_t, and this decoder hands LVGL
 * each one drawn once, with nanosvg, at the size the screen needs: the 64px
 * the 800x480 layout was drawn for, scaled as the rest of the UI scales
 * (Theme::scale_r). Code that fits icons to their space only ever shrinks
 * them, so they stay sharp.
 *
 * The icons are white masks recoloured by style, as the C arrays are, so
 * only the drawing's alpha is used.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include <android/log.h>

#define NANOSVG_IMPLEMENTATION
#include "third_party/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "third_party/nanosvg/nanosvgrast.h"

#if LV_COLOR_DEPTH != 32
#error "grumpy_icons.c writes 32-bit pixels"
#endif

// the size the 64px assets were drawn for on an 800x480 screen, per scale_r
// unit (scale_r: px * min(w / 480, h / 272); 800x480 gives 5/3)
#define BASE_PX_PER_R (64.0 * 3 / 5)
// never below the small screen's 46px set; LVGL's image header holds 11 bits
#define MIN_PX 46
#define MAX_PX 1024

static int icon_px = 64;
static int nav_px = 40;  // android_set_nav_icon_px

typedef struct {
  const void *src;
  uint8_t *px;
} entry_t;
static entry_t *entries;
static int n_entries, cap_entries;

// what an icon descriptor's data points at (embed_assets.cmake)
typedef struct {
  const char *svg;  // NUL-terminated
  size_t size;      // without the NUL
  int nav;          // a nav bar icon: drawn at nav_px
} grumpy_icon_src_t;

static int px_of(const lv_img_dsc_t *dsc) {
  return ((const grumpy_icon_src_t *)dsc->data)->nav ? nav_px : icon_px;
}

static uint8_t *render(const lv_img_dsc_t *dsc) {
  const grumpy_icon_src_t *src = (const grumpy_icon_src_t *)dsc->data;
  // nsvgParse writes into its input
  char *text = malloc(src->size + 1);
  if (!text) return NULL;
  memcpy(text, src->svg, src->size + 1);
  NSVGimage *image = nsvgParse(text, "px", 96);
  free(text);
  if (!image) return NULL;

  const int n = px_of(dsc);
  uint8_t *out = NULL;
  uint8_t *rgba = calloc((size_t)n * n, 4);
  NSVGrasterizer *rast = nsvgCreateRasterizer();
  if (rgba && rast && image->width > 0 && image->height > 0) {
    // fit the drawing's box into n x n, centred, as rsvg-convert -w -h does
    const float side = image->width > image->height ? image->width : image->height;
    const float scale = n / side;
    const float tx = (n - image->width * scale) / 2, ty = (n - image->height * scale) / 2;
    nsvgRasterize(rast, image, tx, ty, scale, rgba, n, n, n * 4);
    out = rgba;  // reused in place: RGBA in, white BGRA out
    for (int i = 0; i < n * n; i++) {
      const uint8_t a = rgba[i * 4 + 3];
      out[i * 4 + 0] = 0xff;  // b
      out[i * 4 + 1] = 0xff;  // g
      out[i * 4 + 2] = 0xff;  // r
      out[i * 4 + 3] = a;
    }
  } else {
    free(rgba);
  }
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);
  return out;
}

static int is_ours(const void *src) {
  return lv_img_src_get_type(src) == LV_IMG_SRC_VARIABLE &&
         ((const lv_img_dsc_t *)src)->header.cf == LV_IMG_CF_USER_ENCODED_0 &&
         ((const lv_img_dsc_t *)src)->data_size == sizeof(grumpy_icon_src_t);
}

static lv_res_t icon_info(lv_img_decoder_t *decoder, const void *src, lv_img_header_t *header) {
  LV_UNUSED(decoder);
  if (!is_ours(src)) return LV_RES_INV;
  header->always_zero = 0;
  header->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  header->w = px_of(src);
  header->h = px_of(src);
  return LV_RES_OK;
}

/* Rendered on first use and kept: LVGL's image cache holds only a few. */
static lv_res_t icon_open(lv_img_decoder_t *decoder, lv_img_decoder_dsc_t *dsc) {
  LV_UNUSED(decoder);
  if (!is_ours(dsc->src)) return LV_RES_INV;
  for (int i = 0; i < n_entries; i++) {
    if (entries[i].src == dsc->src) {
      dsc->img_data = entries[i].px;
      return LV_RES_OK;
    }
  }
  uint8_t *px = render((const lv_img_dsc_t *)dsc->src);
  if (!px) return LV_RES_INV;
  if (n_entries == cap_entries) {
    int cap = cap_entries ? cap_entries * 2 : 64;
    entry_t *grown = realloc(entries, sizeof(entry_t) * cap);
    if (!grown) {
      free(px);
      return LV_RES_INV;
    }
    entries = grown;
    cap_entries = cap;
  }
  entries[n_entries].src = dsc->src;
  entries[n_entries].px = px;
  n_entries++;
  dsc->img_data = px;
  return LV_RES_OK;
}

static void icon_close(lv_img_decoder_t *decoder, lv_img_decoder_dsc_t *dsc) {
  LV_UNUSED(decoder);
  LV_UNUSED(dsc);  // the pixels stay cached
}

void grumpy_icons_init(int hor_res, int ver_res) {
  const double r = fmin(hor_res / 480.0, ver_res / 272.0);
  int px = (int)lround(BASE_PX_PER_R * r);
  icon_px = px < MIN_PX ? MIN_PX : px > MAX_PX ? MAX_PX : px;
  lv_img_decoder_t *d = lv_img_decoder_create();
  lv_img_decoder_set_info_cb(d, icon_info);
  lv_img_decoder_set_open_cb(d, icon_open);
  lv_img_decoder_set_close_cb(d, icon_close);
  __android_log_print(ANDROID_LOG_INFO, "grumpyscreen", "icons at %dpx", icon_px);
}

/* The nav bar icons' size, before anything draws one (main_panel.cpp). */
void android_set_nav_icon_px(int px) {
  nav_px = px < 16 ? 16 : px > MAX_PX ? MAX_PX : px;
}
