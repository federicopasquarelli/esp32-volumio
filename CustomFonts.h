#ifndef CUSTOM_FONTS_H
#define CUSTOM_FONTS_H

#include <lvgl.h>

// LVGL's bundled Montserrat fonts only cover plain ASCII (32-126) plus LVGL's own icon symbols --
// no accented Latin letters, no "smart" typographic punctuation (curly quotes, en/em dashes,
// ellipsis), both common in Volumio's track/library metadata. These are the same fonts
// (Montserrat-Medium.ttf + the FontAwesome icon set), regenerated with lv_font_conv from LVGL's
// own font-generation script (lvgl/scripts/generators/built_in_font/) with the range extended to
// include Latin-1 Supplement (0xA0-0xFF) and the specific smart-punctuation code points. Same
// visual style as the stock fonts they replace, just a wider glyph set -- see the "Opts:" comment
// at the top of each font_montserrat_ext_*.c for the exact lv_font_conv command used.
LV_FONT_DECLARE(lv_font_montserrat_ext_12)
LV_FONT_DECLARE(lv_font_montserrat_ext_14)
LV_FONT_DECLARE(lv_font_montserrat_ext_18)
LV_FONT_DECLARE(lv_font_montserrat_ext_32)

#endif
