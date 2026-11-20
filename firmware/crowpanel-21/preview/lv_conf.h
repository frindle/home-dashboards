#pragma once
#ifndef LV_CONF_H
#define LV_CONF_H
// LVGL settings for the preview build. Mirrors the lv_conf.h ESPHome generates for crowpanel-21.yaml (RGB565, Montserrat
// 14/16/20/48, label + bar, default theme, software renderer). Only the memory and OS lines differ so it runs on a PC.
#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_MEM_SIZE (512 * 1024)
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_DEF_REFR_PERIOD 16
#define LV_DRAW_BUF_ALIGN 32
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB888 1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_A8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_USE_GRADIENT 1
#define LV_GRADIENT_MAX_STOPS 2
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_LABEL_LONG_TXT_HINT 0
#define LV_LABEL_TEXT_SELECTION 0
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_GROW 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_USE_OBJ_ID_BUILTIN 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
#endif
