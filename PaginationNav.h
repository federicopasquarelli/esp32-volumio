#ifndef PAGINATION_NAV_H
#define PAGINATION_NAV_H

#include <lvgl.h>

// A small, accent-filled button for a paginated list's bottom nav bar (Prev/Next, Library's "Up",
// Queue's "Clear" -- anything in that bar that flips LV_STATE_DISABLED depending on paging state).
// Shared by VolumioLibrary.cpp and VolumioQueue.cpp (each used to define its own near-identical
// copy of this) so a fix like the one below only has to happen once.
//
// Strips every style the current LVGL theme attaches to a plain lv_button, rather than trying to
// override it: the theme attaches its OWN separate style objects for the fade transition
// (lv_theme_default.c's `transition_delayed`/`transition_normal`, at the DEFAULT/PRESSED
// selectors), and LVGL sums every attached style's own transition properties independently when a
// state changes (lv_obj.c's obj_transition_states()) -- there's no "local style overrides theme
// style" rule for that specific property, so overriding OUR OWN local style at the same selector
// doesn't cancel theirs. Rapid taps or state flips (paging quickly, a list reloading) can pile up
// real ~80ms fade transitions faster than they finish, exhausting LVGL's own small static memory
// pool and hanging the whole UI with no crash, no reset. See CLAUDE.md items 25/26 for the full
// story -- found and fixed here first in Library; Queue's own copy of this button had the exact
// same latent bug, just unnoticed because nobody had paged through a short-enough queue fast
// enough to hit it.
lv_obj_t* createPagerButton(lv_obj_t* parent, int32_t width, int32_t height,
                             const char* symbol, lv_event_cb_t cb);

// Adds/removes LV_STATE_DISABLED. lv_obj_add_state()/remove_state() are already no-op-guarded
// internally if the state doesn't actually change, so this is just a readable if/else over that.
void setPagerEnabled(lv_obj_t* obj, bool enabled);

#endif
