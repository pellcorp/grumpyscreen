#ifndef __IMAGE_LABEL_H__
#define __IMAGE_LABEL_H__

#include "lvgl/lvgl.h"

// A readout chip: an icon and a value in a card. The card takes whatever cell
// its parent gives it and the icon sizes itself to that.
class ImageLabel {
 public:
  ImageLabel(lv_obj_t *parent, const void *img, const char *value);
  ~ImageLabel();

  lv_obj_t *get_container();
  void update_label(const char *value);

 private:
  lv_obj_t *cont;
  lv_obj_t *image;
  lv_obj_t *label;
};

#endif // __IMAGE_LABEL_H__
