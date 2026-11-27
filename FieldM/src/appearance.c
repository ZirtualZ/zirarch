#include "appearance.h"

#include <string.h>

static const AppearanceTheme paper = {
    "Paper", "#F4F1EA", "#111111", "#111111", "#F4F1EA"
};

void appearance_init(Appearance *appearance)
{
    if (appearance != NULL) {
        memset(appearance, 0, sizeof(*appearance));
    }
}

int appearance_save(const Appearance *appearance)
{
    (void)appearance;
    return 1;
}

size_t appearance_theme_count(void) { return 1; }

const AppearanceTheme *appearance_theme_at(size_t index)
{
    return index == 0 ? &paper : NULL;
}

size_t appearance_background_count(void) { return 1; }
const char *appearance_background_name(int index)
{
    return index == 0 ? "Paper" : NULL;
}
const char *appearance_background_color(int index)
{
    return index == 0 ? "#F4F1EA" : NULL;
}
const char *appearance_contrast_foreground(const char *background)
{
    (void)background;
    return "#111111";
}
int appearance_select_theme(Appearance *appearance, int index)
{
    if (appearance == NULL || index != 0) return 0;
    appearance->theme = 0;
    return 1;
}
int appearance_select_background(Appearance *appearance, int index)
{
    if (appearance == NULL || index != 0) return 0;
    appearance->background = 0;
    return 1;
}
int appearance_select_image(Appearance *appearance, const char *path)
{
    (void)appearance; (void)path;
    return 0;
}
