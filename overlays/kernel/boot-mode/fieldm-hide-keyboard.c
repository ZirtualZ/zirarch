#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int text_matches(const char *text)
{
    if (text == NULL) {
        return 0;
    }
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *needle = "onboard";
        const char *left = cursor;
        const char *right = needle;
        while (*left != '\0' && *right != '\0' &&
               tolower((unsigned char)*left) ==
                   tolower((unsigned char)*right)) {
            ++left;
            ++right;
        }
        if (*right == '\0') {
            return 1;
        }
        ++cursor;
    }
    return 0;
}

static int window_is_keyboard(Display *display, Window window)
{
    XClassHint hint = {0};
    int match = 0;
    if (XGetClassHint(display, window, &hint)) {
        match = text_matches(hint.res_name) ||
                text_matches(hint.res_class);
        XFree(hint.res_name);
        XFree(hint.res_class);
    }
    if (!match) {
        char *name = NULL;
        if (XFetchName(display, window, &name) && name != NULL) {
            match = text_matches(name);
            XFree(name);
        }
    }
    return match;
}

static void hide_tree(Display *display, Window window)
{
    if (window_is_keyboard(display, window)) {
        XUnmapWindow(display, window);
    }
    Window root_return, parent_return, *children = NULL;
    unsigned int count = 0;
    if (XQueryTree(display, window, &root_return, &parent_return,
                   &children, &count)) {
        for (unsigned int i = 0; i < count; ++i) {
            hide_tree(display, children[i]);
        }
        if (children != NULL) {
            XFree(children);
        }
    }
}

int main(void)
{
    if (getenv("DISPLAY") == NULL) {
        setenv("DISPLAY", ":0", 0);
    }
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        return 1;
    }
    hide_tree(display, DefaultRootWindow(display));
    XSync(display, False);
    XCloseDisplay(display);
    return 0;
}
