#include "VolumioArtists.h"
#include "CustomFonts.h"

static lv_obj_t* container = NULL;

// Placeholder screen, reachable from the dropdown menu -- content (an artist browser) to be
// built later. See CLAUDE.md.
void buildArtists(lv_obj_t* parent) {
    container = parent;
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, "Coming soon");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_center(label);
}

void hideArtists() {
    lv_obj_clean(container);
}
