#ifndef PAGINATION_NAV_H
#define PAGINATION_NAV_H

#include <lvgl.h>

// An accent-filled button for a paginated list's bottom bar (Prev/Next, Library's Up, Queue's
// Clear), shared by every list screen.
//
// It strips every style the LVGL theme attaches instead of overriding them: the default theme
// adds its own ~80ms fade transitions, and LVGL sums the transitions of every attached style, so a
// local override doesn't cancel them. Paging quickly piles those transitions up faster than they
// finish, exhausting LVGL's memory pool, and LVGL's failed-allocation assert then hangs the UI.
lv_obj_t* createPagerButton(lv_obj_t* parent, int32_t width, int32_t height,
                             const char* symbol, lv_event_cb_t cb);

// Adds/removes LV_STATE_DISABLED.
void setPagerEnabled(lv_obj_t* obj, bool enabled);

// Formats the page indicator into buf: "-" while not listing (loading or an error),
// "<page>/<pages> (<total_count>)" when the total is known, else just "<page>". page is 0-based;
// total_count < 0 means unknown (a Volumio response without "count").
void formatPageLabel(char* buf, size_t bufsize, bool listing, int page, int page_size, int total_count);

#endif
