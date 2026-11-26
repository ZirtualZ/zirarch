#include "screen_layout.h"

ScreenGeometry screen_layout_get(Display *display)
{
    int screen = DefaultScreen(display);
    return (ScreenGeometry){
        0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen)
    };
}
