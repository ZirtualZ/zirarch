#ifndef FIELD_M_THEME_H
#define FIELD_M_THEME_H

/* Keep in sync with data/fieldm.css. */
#define FIELD_M_LIGHT_BACKGROUND "#F4F1EA"
#define FIELD_M_LIGHT_FOREGROUND "#111111"
#define FIELD_M_LIGHT_PRESSED "#E2DDD2"
#define FIELD_M_DARK_BACKGROUND "#161513"
#define FIELD_M_DARK_FOREGROUND "#EAE6DD"
#define FIELD_M_DARK_PRESSED "#2A2824"

int fieldm_theme_is_dark(void);
char *fieldm_theme_css(const char *css, int dark);
int fieldm_apply_xresources(int dark, const char *dpi);
#define FIELD_M_STATUS_HEIGHT 48
#define FIELD_M_TITLE_HEIGHT 56
#define FIELD_M_CLOSE_SIZE 64
#define FIELD_M_ROW_HEIGHT 64
#define FIELD_M_KEYBOARD_HEIGHT 320
#define FIELD_M_BORDER_WIDTH 2
#define FIELD_M_RADIUS 4
#define FIELD_M_TOUCH_MIN 64

#endif
