#ifndef __ICONS_H__
#define __ICONS_H__

#include "lvgl/lvgl.h"

// Every image the UI can draw, declared once.
//
// The bitmaps are compiled in from assets/ (assets/material at full size,
// assets/material_46 on small screens -- the Makefile picks the directory, the
// symbols are identical either way). Panels refer to them through the named
// constants below rather than declaring `back` for the eleventh time, so
// swapping an icon set is an edit here and nowhere else.
//
// These are extern declarations and constant pointers: an icon nothing
// references links to nothing and costs nothing.

LV_IMG_DECLARE(arrow_down);
LV_IMG_DECLARE(arrow_left);
LV_IMG_DECLARE(arrow_right);
LV_IMG_DECLARE(arrow_up);
LV_IMG_DECLARE(back);
LV_IMG_DECLARE(bed);
LV_IMG_DECLARE(cancel);
LV_IMG_DECLARE(chart_img);
LV_IMG_DECLARE(checker);
LV_IMG_DECLARE(clock_img);
LV_IMG_DECLARE(cooldown_img);
LV_IMG_DECLARE(delete_img);
LV_IMG_DECLARE(emergency);
LV_IMG_DECLARE(extrude);
LV_IMG_DECLARE(extrude_img);
LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(fan);
LV_IMG_DECLARE(fan_off_img);
LV_IMG_DECLARE(fan_on);
LV_IMG_DECLARE(filament_img);
LV_IMG_DECLARE(fine_tune_img);
LV_IMG_DECLARE(flow_down_img);
LV_IMG_DECLARE(flow_up_img);
LV_IMG_DECLARE(heater);
LV_IMG_DECLARE(home);
LV_IMG_DECLARE(home_z);
LV_IMG_DECLARE(hourglass);
LV_IMG_DECLARE(info_img);
LV_IMG_DECLARE(layers_img);
LV_IMG_DECLARE(light_img);
LV_IMG_DECLARE(light_off);
LV_IMG_DECLARE(limit_img);
LV_IMG_DECLARE(load_filament_img);
LV_IMG_DECLARE(motor_img);
LV_IMG_DECLARE(motor_off_img);
LV_IMG_DECLARE(move);
LV_IMG_DECLARE(network_img);
LV_IMG_DECLARE(pa_minus_img);
LV_IMG_DECLARE(pa_plus_img);
LV_IMG_DECLARE(pause_img);
LV_IMG_DECLARE(power_devices_img);
LV_IMG_DECLARE(print);
LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(resume);
LV_IMG_DECLARE(retract_img);
LV_IMG_DECLARE(sd_img);
LV_IMG_DECLARE(speed_down_img);
LV_IMG_DECLARE(speed_up_img);
LV_IMG_DECLARE(spoolman_img);
LV_IMG_DECLARE(sysinfo_img);
LV_IMG_DECLARE(unload_filament_img);
LV_IMG_DECLARE(update_img);
LV_IMG_DECLARE(z_closer);
LV_IMG_DECLARE(z_farther);

namespace Icons {

inline constexpr const lv_img_dsc_t *ARROW_DOWN = &arrow_down;
inline constexpr const lv_img_dsc_t *ARROW_LEFT = &arrow_left;
inline constexpr const lv_img_dsc_t *ARROW_RIGHT = &arrow_right;
inline constexpr const lv_img_dsc_t *ARROW_UP = &arrow_up;
inline constexpr const lv_img_dsc_t *BACK = &back;
inline constexpr const lv_img_dsc_t *BED = &bed;
inline constexpr const lv_img_dsc_t *CANCEL = &cancel;
inline constexpr const lv_img_dsc_t *CHART_IMG = &chart_img;
inline constexpr const lv_img_dsc_t *CHECKER = &checker;
inline constexpr const lv_img_dsc_t *CLOCK_IMG = &clock_img;
inline constexpr const lv_img_dsc_t *COOLDOWN_IMG = &cooldown_img;
inline constexpr const lv_img_dsc_t *DELETE_IMG = &delete_img;
inline constexpr const lv_img_dsc_t *EMERGENCY = &emergency;
inline constexpr const lv_img_dsc_t *EXTRUDE = &extrude;
inline constexpr const lv_img_dsc_t *EXTRUDE_IMG = &extrude_img;
inline constexpr const lv_img_dsc_t *EXTRUDER = &extruder;
inline constexpr const lv_img_dsc_t *FAN = &fan;
inline constexpr const lv_img_dsc_t *FAN_OFF_IMG = &fan_off_img;
inline constexpr const lv_img_dsc_t *FAN_ON = &fan_on;
inline constexpr const lv_img_dsc_t *FILAMENT_IMG = &filament_img;
inline constexpr const lv_img_dsc_t *FINE_TUNE_IMG = &fine_tune_img;
inline constexpr const lv_img_dsc_t *FLOW_DOWN_IMG = &flow_down_img;
inline constexpr const lv_img_dsc_t *FLOW_UP_IMG = &flow_up_img;
inline constexpr const lv_img_dsc_t *HEATER = &heater;
inline constexpr const lv_img_dsc_t *HOME = &home;
inline constexpr const lv_img_dsc_t *HOME_Z = &home_z;
inline constexpr const lv_img_dsc_t *HOURGLASS = &hourglass;
inline constexpr const lv_img_dsc_t *INFO_IMG = &info_img;
inline constexpr const lv_img_dsc_t *LAYERS_IMG = &layers_img;
inline constexpr const lv_img_dsc_t *LIGHT_IMG = &light_img;
inline constexpr const lv_img_dsc_t *LIGHT_OFF = &light_off;
inline constexpr const lv_img_dsc_t *LIMIT_IMG = &limit_img;
inline constexpr const lv_img_dsc_t *LOAD_FILAMENT_IMG = &load_filament_img;
inline constexpr const lv_img_dsc_t *MOTOR_IMG = &motor_img;
inline constexpr const lv_img_dsc_t *MOTOR_OFF_IMG = &motor_off_img;
inline constexpr const lv_img_dsc_t *MOVE = &move;
inline constexpr const lv_img_dsc_t *NETWORK_IMG = &network_img;
inline constexpr const lv_img_dsc_t *PA_MINUS_IMG = &pa_minus_img;
inline constexpr const lv_img_dsc_t *PA_PLUS_IMG = &pa_plus_img;
inline constexpr const lv_img_dsc_t *PAUSE_IMG = &pause_img;
inline constexpr const lv_img_dsc_t *POWER_DEVICES_IMG = &power_devices_img;
inline constexpr const lv_img_dsc_t *PRINT = &print;
inline constexpr const lv_img_dsc_t *REFRESH_IMG = &refresh_img;
inline constexpr const lv_img_dsc_t *RESUME = &resume;
inline constexpr const lv_img_dsc_t *RETRACT_IMG = &retract_img;
inline constexpr const lv_img_dsc_t *SD_IMG = &sd_img;
inline constexpr const lv_img_dsc_t *SPEED_DOWN_IMG = &speed_down_img;
inline constexpr const lv_img_dsc_t *SPEED_UP_IMG = &speed_up_img;
inline constexpr const lv_img_dsc_t *SPOOLMAN_IMG = &spoolman_img;
inline constexpr const lv_img_dsc_t *SYSINFO_IMG = &sysinfo_img;
inline constexpr const lv_img_dsc_t *UNLOAD_FILAMENT_IMG = &unload_filament_img;
inline constexpr const lv_img_dsc_t *UPDATE_IMG = &update_img;
inline constexpr const lv_img_dsc_t *Z_CLOSER = &z_closer;
inline constexpr const lv_img_dsc_t *Z_FARTHER = &z_farther;

}  // namespace Icons

#endif  // __ICONS_H__
