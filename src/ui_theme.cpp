#include "ui_theme.h"

namespace UiTheme {

lv_style_t styleCard;
lv_style_t styleCardPressed;
lv_style_t styleCardChecked;
lv_style_t styleButton;
lv_style_t styleButtonPressed;
lv_style_t styleFlat;

void init() {
  // Resting card: a hull panel with a hairline edge. Borders over shadows.
  lv_style_init(&styleCard);
  lv_style_set_radius(&styleCard, RADIUS_MD);
  lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
  lv_style_set_bg_color(&styleCard, col(HULL));
  lv_style_set_border_width(&styleCard, 1);
  lv_style_set_border_color(&styleCard, col(LINE));
  lv_style_set_border_opa(&styleCard, LV_OPA_COVER);
  lv_style_set_shadow_width(&styleCard, 0);
  lv_style_set_text_color(&styleCard, col(INK_MUTED));
  lv_style_set_text_font(&styleCard, &at_sans_14);
  lv_style_set_pad_all(&styleCard, 0);

  lv_style_init(&styleCardPressed);
  lv_style_set_bg_color(&styleCardPressed, col(HULL_HIGH));
  lv_style_set_border_color(&styleCardPressed, col(LINE_STRONG));

  // The running mode. Tinted, not glowing: phosphor text on its soft ground,
  // edged in phosphor — the same treatment as the design system's "online"
  // tag, so it reads as a live state rather than a button.
  lv_style_init(&styleCardChecked);
  lv_style_set_bg_color(&styleCardChecked, col(PHOSPHOR_SOFT));
  lv_style_set_border_color(&styleCardChecked, col(PHOSPHOR));
  lv_style_set_text_color(&styleCardChecked, col(PHOSPHOR));

  // Secondary button: transparent, line-strong outline, ink label.
  lv_style_init(&styleButton);
  lv_style_set_radius(&styleButton, RADIUS_MD);
  lv_style_set_bg_opa(&styleButton, LV_OPA_TRANSP);
  lv_style_set_bg_color(&styleButton, col(HULL_HIGH));
  lv_style_set_border_width(&styleButton, 1);
  lv_style_set_border_color(&styleButton, col(LINE_STRONG));
  lv_style_set_border_opa(&styleButton, LV_OPA_COVER);
  lv_style_set_text_color(&styleButton, col(INK));
  lv_style_set_text_font(&styleButton, &at_sans_14);
  lv_style_set_pad_all(&styleButton, 0);

  lv_style_init(&styleButtonPressed);
  lv_style_set_bg_opa(&styleButtonPressed, LV_OPA_COVER);
  lv_style_set_border_color(&styleButtonPressed, col(ION));

  lv_style_init(&styleFlat);
  lv_style_set_radius(&styleFlat, 0);
  lv_style_set_bg_opa(&styleFlat, LV_OPA_TRANSP);
  lv_style_set_border_width(&styleFlat, 0);
  lv_style_set_outline_width(&styleFlat, 0);
  lv_style_set_shadow_width(&styleFlat, 0);
  lv_style_set_pad_all(&styleFlat, 0);
}

lv_obj_t *slab(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
               lv_coord_t h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &styleFlat, 0);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t *hairline(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                   lv_coord_t h) {
  lv_obj_t *o = slab(parent, x, y, w, h);
  lv_obj_set_style_bg_color(o, col(LINE), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

lv_obj_t *eyebrow(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &at_mono_11, 0);
  lv_obj_set_style_text_color(l, col(INK_MUTED), 0);
  lv_obj_set_style_text_letter_space(l, 2, 0);  // the label style's 0.14em
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_color_t contrastOn(uint8_t r, uint8_t g, uint8_t b) {
  // Rec. 601 luma is close enough and avoids a float pow() per repaint.
  const uint16_t luma = (uint16_t)((r * 299u + g * 587u + b * 114u) / 1000u);
  return luma > 150 ? lv_color_hex(VOID) : lv_color_hex(0xFFFFFF);
}

}  // namespace UiTheme
