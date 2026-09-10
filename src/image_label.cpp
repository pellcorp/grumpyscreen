#include "image_label.h"
#include "theme.h"
#include <cstring>

using namespace Theme;

ImageLabel::ImageLabel(lv_obj_t *parent, const void *img, const char *value)
  : cont(lv_obj_create(parent))
  , image(lv_img_create(cont))
  , label(lv_label_create(cont))
{
  // a chip: icon, a gap, then the value taking the rest of the card
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  // the card's hairline included: these readouts have always been framed
  lv_obj_add_style(cont, &styles().card, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(cont, gap(), 0);

  // the icon is sized to the card's content height once the card knows it
  lv_img_set_src(image, img);
  lv_obj_add_event_cb(cont, fit_first_icon, LV_EVENT_SIZE_CHANGED, NULL);

  lv_label_set_text(label, value);
  lv_obj_set_flex_grow(label, 1);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

ImageLabel::~ImageLabel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *ImageLabel::get_container() {
  return cont;
}

void ImageLabel::update_label(const char *v) {
  if (std::strcmp(v, lv_label_get_text(label)) != 0) {
    lv_label_set_text(label, v);
  }
}
