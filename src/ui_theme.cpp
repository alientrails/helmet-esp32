#include "ui_theme.h"

namespace UiTheme {

lv_style_t styleCard;
lv_style_t styleCardPressed;
lv_style_t styleCardChecked;
lv_style_t styleFlat;

void init() {
  // Resting card: a barely-there vertical gradient plus a hairline border. The
  // gradient is what stops large flat fills from looking like a 1990s toolkit.
  lv_style_init(&styleCard);
  lv_style_set_radius(&styleCard, 10);
  lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
  lv_style_set_bg_color(&styleCard, col(CARD));
  lv_style_set_bg_grad_color(&styleCard, col(CARD_LOW));
  lv_style_set_bg_grad_dir(&styleCard, LV_GRAD_DIR_VER);
  lv_style_set_border_width(&styleCard, 1);
  lv_style_set_border_color(&styleCard, col(LINE));
  lv_style_set_border_opa(&styleCard, LV_OPA_COVER);
  lv_style_set_shadow_width(&styleCard, 0);
  lv_style_set_text_color(&styleCard, col(TEXT_DIM));
  lv_style_set_text_font(&styleCard, &lv_font_montserrat_12);
  lv_style_set_pad_all(&styleCard, 0);

  lv_style_init(&styleCardPressed);
  lv_style_set_bg_color(&styleCardPressed, col(CARD_HI));
  lv_style_set_bg_grad_color(&styleCardPressed, col(CARD));
  lv_style_set_border_color(&styleCardPressed, col(ACCENT_DK));

  // Selected: accent gradient with a soft glow of the same hue. The glow reads
  // as "this one is live" from across a room, which is the whole point on a
  // helmet controller.
  lv_style_init(&styleCardChecked);
  lv_style_set_bg_color(&styleCardChecked, col(ACCENT));
  lv_style_set_bg_grad_color(&styleCardChecked, col(ACCENT_DK));
  lv_style_set_bg_grad_dir(&styleCardChecked, LV_GRAD_DIR_VER);
  lv_style_set_border_color(&styleCardChecked, col(ACCENT_LT));
  lv_style_set_border_width(&styleCardChecked, 1);
  lv_style_set_shadow_width(&styleCardChecked, 12);
  lv_style_set_shadow_ofs_y(&styleCardChecked, 2);
  lv_style_set_shadow_color(&styleCardChecked, col(ACCENT));
  lv_style_set_shadow_opa(&styleCardChecked, LV_OPA_60);
  lv_style_set_text_color(&styleCardChecked, lv_color_white());

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
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *eyebrow(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, col(MUTED), 0);
  lv_obj_set_style_text_letter_space(l, 2, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_color_t contrastOn(uint8_t r, uint8_t g, uint8_t b) {
  // Rec. 601 luma is close enough and avoids a float pow() per repaint.
  const uint16_t luma = (uint16_t)((r * 299u + g * 587u + b * 114u) / 1000u);
  return luma > 150 ? lv_color_hex(0x0B0E14) : lv_color_hex(0xFFFFFF);
}

}  // namespace UiTheme
