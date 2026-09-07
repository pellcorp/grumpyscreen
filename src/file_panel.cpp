#include "file_panel.h"
#include "theme.h"
#include "config.h"
#include "state.h"
#include "utils.h"
#include "logger.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

FilePanel::FilePanel(lv_obj_t *parent)
  : file_cont(lv_obj_create(parent))
  , thumbnail(lv_img_create(file_cont))
  , fname_label(lv_label_create(file_cont))
  , detail_label(lv_label_create(file_cont))
{
  // a panel: thumbnail on top, then the name, then the details, all centred
  lv_obj_add_style(file_cont, &Theme::styles().panel, 0);
  lv_obj_set_size(file_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(file_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(file_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(file_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(file_cont, Theme::gap(), 0);

  // REAL: the object is the zoomed bitmap, so the labels below never get
  // overdrawn; refresh_view() picks the zoom that fits the space left over
  lv_img_set_size_mode(thumbnail, LV_IMG_SIZE_MODE_REAL);
  lv_obj_set_width(fname_label, LV_PCT(100));
  lv_label_set_long_mode(fname_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(fname_label, Theme::scale_font(14), 0);
  lv_obj_set_style_text_align(fname_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(fname_label, "");
  lv_obj_add_style(detail_label, &Theme::styles().dim_label, 0);
  lv_obj_set_style_text_font(detail_label, Theme::scale_font(14), 0);
  lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(detail_label, "");
}

FilePanel::~FilePanel() {
  if (file_cont != NULL) {
    lv_obj_del(file_cont);
    file_cont = NULL;
  }
}

void FilePanel::refresh_view(json &j, const std::string &gcode_path) {
  auto v = j["/result/modified"_json_pointer];
  std::stringstream time_stream;  
  if (!v.is_null()) {
    std::time_t timestamp = v.template get<std::time_t>();
    std::tm lt = *std::localtime(&timestamp);
    time_stream << std::put_time(&lt, "%Y-%m-%d %H:%M");
  } else {
    time_stream << "(unknown)";
  }
  
  v = j["/result/estimated_time"_json_pointer];
  int eta =  v.is_null() ? -1 : v.template get<int>();
  v = j["/result/filament_weight_total"_json_pointer];
  int fweight = v.is_null() ? -1 : v.template get<int>();

  auto filename = fs::path(gcode_path).filename();
  lv_label_set_text(fname_label, filename.string().c_str());
  
  std::string detail = fmt::format("Filament Weight: {} g\nPrint Time: {}\nSize: {} MB",
				   fweight > 0 ? std::to_string(fweight) : "(unknown)",
				   eta > 0 ? KUtils::eta_string(eta) : "(unknown)",
				   KUtils::bytes_to_mb(j["result"]["size"].template get<size_t>()));

  lv_label_set_text(detail_label, detail.c_str());
  auto thumb_detail = KUtils::get_thumbnail(gcode_path, j, Theme::scale_w(180));
  std::string fullpath = thumb_detail.first;
  if (fullpath.length() > 0) {
    lv_img_set_src(thumbnail, ("A:" + fullpath).c_str());
    // fit the bitmap to the slot the labels leave it, both ways; a small
    // thumbnail may grow to fill it, but no further than 2x or it blurs
    lv_obj_update_layout(file_cont);
    const lv_coord_t slot_w = lv_obj_get_content_width(file_cont);
    const lv_coord_t slot_h = lv_obj_get_content_height(file_cont) - lv_obj_get_height(fname_label)
                              - lv_obj_get_height(detail_label) - 2 * lv_obj_get_style_pad_row(file_cont, 0);
    Theme::fit_img(thumbnail, slot_w, slot_h, 2 * LV_IMG_ZOOM_NONE);
  } else {
    // free src
    lv_img_set_src(thumbnail, NULL);
    // hack to color in empty space.
    ((lv_img_t*)thumbnail)->src_type = LV_IMG_SRC_SYMBOL;
  }
}

void FilePanel::foreground() {
  lv_obj_move_foreground(file_cont);
}

lv_obj_t *FilePanel::get_container() {
  return file_cont;
}
