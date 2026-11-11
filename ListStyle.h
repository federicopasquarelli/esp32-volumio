#ifndef LIST_STYLE_H
#define LIST_STYLE_H

#include <lvgl.h>

// The look of every list in the app (Library, Artists, Queue, the long-press menu): rows with no
// background and no border, so the list's black shows through, and a grey fill only while a row
// is pressed. Overrides what LVGL's default theme gives list rows (a fill, and a bottom border).

// A row from lv_list_add_button(), height in px.
void styleListRow(lv_obj_t* row, int32_t height);
// A text row from lv_list_add_text() ("Loading...", "Empty", a menu title).
void styleListText(lv_obj_t* text);

#endif
