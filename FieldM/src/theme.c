#define _POSIX_C_SOURCE 200809L

#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int fieldm_theme_is_dark(void)
{
    const char *home = getenv("HOME");
    char path[1200];
    if (home == NULL || home[0] == '\0' ||
        snprintf(path, sizeof(path),
                 "%s/.config/simple-desktop/features", home) >=
            (int)sizeof(path)) {
        return 0;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) return 0;
    char line[128];
    int dark = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strcmp(line, "theme=dark\n") == 0) dark = 1;
        else if (strcmp(line, "theme=light\n") == 0) dark = 0;
    }
    fclose(file);
    return dark;
}

static void append_replacement(char **output, size_t *used, size_t *capacity,
                               const char *text)
{
    size_t length = strlen(text);
    if (*used + length + 1 > *capacity) {
        *capacity = (*used + length + 1) * 2;
        char *larger = realloc(*output, *capacity);
        if (larger == NULL) return;
        *output = larger;
    }
    memcpy(*output + *used, text, length + 1);
    *used += length;
}

char *fieldm_theme_css(const char *css, int dark)
{
    if (css == NULL) return NULL;
    size_t capacity = strlen(css) + 1;
    char *output = malloc(capacity);
    if (output == NULL) return NULL;
    size_t used = 0;
    const struct {
        const char *from;
        const char *to;
    } replacements[] = {
        {FIELD_M_LIGHT_BACKGROUND,
         dark ? FIELD_M_DARK_BACKGROUND : FIELD_M_LIGHT_BACKGROUND},
        {FIELD_M_LIGHT_FOREGROUND,
         dark ? FIELD_M_DARK_FOREGROUND : FIELD_M_LIGHT_FOREGROUND},
        {FIELD_M_LIGHT_PRESSED,
         dark ? FIELD_M_DARK_PRESSED : FIELD_M_LIGHT_PRESSED}
    };
    for (const char *cursor = css; *cursor != '\0';) {
        int replaced = 0;
        for (size_t i = 0; i < sizeof(replacements) /
                                 sizeof(replacements[0]); ++i) {
            size_t length = strlen(replacements[i].from);
            if (strncmp(cursor, replacements[i].from, length) == 0) {
                append_replacement(&output, &used, &capacity,
                                   replacements[i].to);
                cursor += length;
                replaced = 1;
                break;
            }
        }
        if (!replaced) {
            char one[2] = {*cursor++, '\0'};
            append_replacement(&output, &used, &capacity, one);
        }
    }
    return output;
}

int fieldm_apply_xresources(int dark, const char *dpi)
{
    const char *home = getenv("HOME");
    char path[1200];
    if (home == NULL || home[0] == '\0' ||
        snprintf(path, sizeof(path), "%s/.Xresources", home) >=
            (int)sizeof(path)) {
        return 0;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) return 0;
    const char *background = dark ? FIELD_M_DARK_BACKGROUND
                                  : FIELD_M_LIGHT_BACKGROUND;
    const char *foreground = dark ? FIELD_M_DARK_FOREGROUND
                                  : FIELD_M_LIGHT_FOREGROUND;
    int failed = fprintf(file,
        "Xft.dpi: %s\n"
        "Xft.antialias: 1\n"
        "Xft.hinting: 1\n"
        "XTerm*background: %s\n"
        "XTerm*foreground: %s\n"
        "XTerm*cursorColor: %s\n"
        "XTerm*faceName: DejaVu Sans Mono\n"
        "XTerm*faceSize: 11\n"
        "XTerm*scrollBar: false\n",
        dpi != NULL && dpi[0] != '\0' ? dpi : "168",
        background, foreground, foreground) < 0;
    if (fclose(file) != 0) failed = 1;
    return !failed;
}
