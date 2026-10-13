#ifndef QUEUE_ACTION_MENU_H
#define QUEUE_ACTION_MENU_H

#include <lvgl.h>
#include "VolumioApi.h"

// Long-press menu (Play / Add to queue / Clear and play, plus "Update folder" when with_update).
void showQueueActionMenu(const char* title, const char* uri, const char* service, bool with_update);
// Runs one action in the background. Ignored while another is still running.
void startQueueAction(QueueAction action, const char* service, const char* uri, const char* title);

#endif
