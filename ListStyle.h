#ifndef LIST_STYLE_H
#define LIST_STYLE_H

#include <lvgl.h>
#include "DisplayConfig.h"

// The look of every list in the app (Library, Artists, Queue, the long-press menu): rows with no
// background and no border, so the list's black shows through, and a grey fill only while a row
// is pressed. Overrides what LVGL's default theme gives list rows (a fill, and a bottom border).

// A screen's list, in the LIST_BODY_H between the title row and the page bar: black, square,
// not scrollable, no padding, the ext 14px font (metadata has accented letters and smart quotes
// the stock font lacks, see CustomFonts.h). Square matters: the theme gives lists rounded corners
// and clip_corner, which cut off the corners of whichever row touches the top or bottom edge.
lv_obj_t* createList(lv_obj_t* parent);

// A row from lv_list_add_button(), height in px.
void styleListRow(lv_obj_t* row, int32_t height);
// A text row from lv_list_add_text() ("Loading...", "Empty", a menu title).
void styleListText(lv_obj_t* text);

// Every list screen, top to bottom: a LIST_HEADER_H title row, the list (LIST_BODY_H, everything
// in between, a little more than its 4 rows of 34px) and a LIST_PAGE_BAR_H page bar aligned to
// the bottom.
#define LIST_HEADER_H   28
#define LIST_PAGE_BAR_H 34
#define LIST_BODY_H     (SCREEN_HEIGHT - TOP_BAR_H - LIST_HEADER_H - LIST_PAGE_BAR_H)

// The title row above a list: a black, non-scrolling flex row of the given height, children
// left-aligned and vertically centred, 8px from the edges and from each other. A back/up button
// in it should be createPagerButton(header, 46, LIST_HEADER_H - 4, ...).
lv_obj_t* createListHeader(lv_obj_t* parent, int32_t height);
// A label in the header's title style (grey, 14px ext font). Returns it.
lv_obj_t* addListTitle(lv_obj_t* header, const char* text);

#endif
