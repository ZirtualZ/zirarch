#ifndef FIELD_DESKTOP_SCREEN_LAYOUT_H
#define FIELD_DESKTOP_SCREEN_LAYOUT_H

#include <X11/Xlib.h>

typedef struct {
    int x;
    int y;
    int width;
    int height;
} ScreenGeometry;

ScreenGeometry screen_layout_get(Display *display);

#endif
