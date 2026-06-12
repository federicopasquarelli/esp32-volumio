#ifndef UI_HANDLER_H
#define UI_HANDLER_H
#include <lvgl.h>
void setupUI();
void updateTime();
lv_obj_t* addTab(const char* name);
void updateVolumioUI(const char* title, const char* artist, const char* album, bool isPlaying);
void updateVolumeUI(int volume);
#endif
