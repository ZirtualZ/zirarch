#define _POSIX_C_SOURCE 200809L

#include "theme.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 720
#define HEIGHT FIELD_M_KEYBOARD_HEIGHT

typedef struct {
    int x, y, w, h;
    KeySym keysym;
    const char *label;
    int special;
} Key;

static Display *display;
static Window window;
static GC gc;
static XftDraw *draw;
static XftFont *font;
static XftColor color_text, color_paper;
static int page, shifted, caps, ctrl, terminal, pressed = -1;

static unsigned long pixel(const char *name) {
    XColor exact, nearest;
    XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                     name, &exact, &nearest);
    return nearest.pixel;
}

static void set_wm_hints(void)
{
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    XChangeWindowAttributes(display, window, CWOverrideRedirect, &attrs);
    XSizeHints hints;
    hints.flags = PPosition | PSize;
    hints.x = 0; hints.y = 960; hints.width = WIDTH; hints.height = HEIGHT;
    XSetWMNormalHints(display, window, &hints);
    XClassHint class_hint = {"fieldm-kbd", "fieldm-kbd"};
    XSetClassHint(display, window, &class_hint);
    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&type, 1);
    unsigned long strut[4] = {0, 0, HEIGHT, 0};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)strut, 4);
    unsigned long partial[12] = {0, 0, HEIGHT, 0,
                                0, 0, 0, 0, 0, 719, 0, 0};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)partial, 12);
    XWMHints wm;
    wm.flags = InputHint;
    wm.input = False;
    XSetWMHints(display, window, &wm);
}

static KeySym final_key(KeySym key)
{
    if (key == XK_Shift_L || key == XK_Caps_Lock ||
        key == XK_Control_L || key == XK_Escape ||
        key == XK_Tab || key == XK_Left || key == XK_Right ||
        key == XK_Up || key == XK_Down || key == XK_BackSpace)
        return key;
    if (shifted || caps) {
        KeySym lower = NoSymbol, upper = NoSymbol;
        XConvertCase(key, &lower, &upper);
        if (upper != NoSymbol) return upper;
    }
    return key;
}

static void key_action(Key *key)
{
    if (key->special == 1) {
        if (shifted) caps = !caps;
        shifted = !shifted;
        return;
    }
    if (key->special == 2) { page = !page; return; }
    if (key->special == 3) { terminal = !terminal; return; }
    if (key->special == 4) { ctrl = 1; return; }
    if (key->special == 5) { caps = !caps; return; }
    if (key->keysym == XK_Shift_L) { shifted = !shifted; return; }

    KeySym sym = final_key(key->keysym);
    KeyCode code = XKeysymToKeycode(display, sym);
    if (code == 0) return;
    Window focus = None;
    int revert = 0;
    XGetInputFocus(display, &focus, &revert);
    if (focus == None || focus == PointerRoot) {
        focus = DefaultRootWindow(display);
    }
    XKeyEvent press, release;
    memset(&press, 0, sizeof(press));
    memset(&release, 0, sizeof(release));
    press.display = release.display = display;
    press.window = release.window = focus;
    press.root = release.root = DefaultRootWindow(display);
    press.same_screen = release.same_screen = True;
    press.time = release.time = CurrentTime;
    press.keycode = release.keycode = code;
    press.type = KeyPress;
    release.type = KeyRelease;
    if (ctrl) {
        press.keycode = XKeysymToKeycode(display, XK_Control_L);
        XSendEvent(display, focus, True, KeyPressMask, (XEvent *)&press);
        press.keycode = code;
    }
    XSendEvent(display, focus, True, KeyPressMask, (XEvent *)&press);
    XSendEvent(display, focus, True, KeyReleaseMask, (XEvent *)&release);
    if (ctrl) {
        release.keycode = XKeysymToKeycode(display, XK_Control_L);
        XSendEvent(display, focus, True, KeyReleaseMask, (XEvent *)&release);
    }
    XFlush(display);
    ctrl = 0;
    if (shifted && !caps) shifted = 0;
}

static void draw_key(Key *key)
{
    XSetForeground(display, gc, pressed == key->special ? pixel(FIELD_M_FOREGROUND) :
                   pixel(FIELD_M_BACKGROUND));
    XFillRectangle(display, window, gc, key->x, key->y, key->w, key->h);
    XSetForeground(display, gc, pixel(FIELD_M_FOREGROUND));
    XSetLineAttributes(display, gc, FIELD_M_BORDER_WIDTH, LineSolid, CapButt, JoinMiter);
    XDrawRectangle(display, window, gc, key->x, key->y, key->w - 1, key->h - 1);
    XftColor *fg = pressed == key->special ? &color_paper : &color_text;
    XftDrawStringUtf8(draw, fg, font, key->x + key->w / 2 - 10,
                      key->y + key->h / 2 + 8,
                      (const FcChar8 *)key->label, (int)strlen(key->label));
}

static void build_and_draw(void)
{
    XClearWindow(display, window);
    XSetForeground(display, gc, pixel(FIELD_M_BACKGROUND));
    XFillRectangle(display, window, gc, 0, 0, WIDTH, HEIGHT);
    Key keys[40];
    int n = 0;
    if (terminal) {
        const char *labels[] = {"Esc","Tab","Ctrl","<","^","v",">","|","~","/"};
        KeySym syms[] = {XK_Escape,XK_Tab,XK_Control_L,XK_Left,XK_Up,XK_Down,XK_Right,
                         XK_bar,XK_asciitilde,XK_slash};
        for (int i = 0; i < 10; ++i) {
            keys[n++] = (Key){i * 72, 0, 72, 64, syms[i], labels[i],
                              i == 2 ? 4 : i == 0 ? 5 : 0};
        }
    }
    int row = terminal ? 64 : 0;
    const char *rows1[] = {"1","2","3","4","5","6","7","8","9","0"};
    const char *rows2[] = {"q","w","e","r","t","y","u","i","o","p"};
    const char *rows3[] = {"a","s","d","f","g","h","j","k","l",""};
    const char *rows4[] = {"Shift","z","x","c","v","b","n","m","?","Back"};
    const char *symbol3[] = {"-","/",";",":","'", "\"",",",".","?",""};
    const char *symbol4[] = {"123","z","x","c","v","b","n","m","!","Back"};
    for (int r = 0; r < 4; ++r) {
        const char **labels = r == 0 ? rows1 : r == 1 ? rows2 :
                               r == 2 ? (page ? symbol3 : rows3) :
                               (page ? symbol4 : rows4);
        for (int c = 0; c < 10; ++c) {
            if (labels[c][0] == '\0') continue;
            KeySym sym;
            if (r == 0) sym = XK_1 + c;
            else if (r == 1) sym = XK_q + c;
            else if (r == 2) sym = page ? (c == 0 ? XK_minus : XK_0 + c) : XK_a + c;
            else sym = XK_z + (c > 0 && c < 9 ? c - 1 : 0);
            if (!page && r == 0) sym = XK_1 + c;
            if (strcmp(labels[c], "Back") == 0) sym = XK_BackSpace;
            else if (strcmp(labels[c], "Shift") == 0) sym = XK_Shift_L;
            else if (strcmp(labels[c], "123") == 0) sym = XK_numbersign;
            int special = 0;
            if (strcmp(labels[c], "Shift") == 0) special = 1;
            if (strcmp(labels[c], "123") == 0) special = 2;
            if (strcmp(labels[c], "Back") == 0) special = 6;
            int w = 72;
            if (strcmp(labels[c], "Shift") == 0 || strcmp(labels[c], "Back") == 0)
                w = 144;
            int x = c * 72;
            if (strcmp(labels[c], "Shift") == 0) x = 0;
            if (strcmp(labels[c], "Back") == 0) x = 576;
            keys[n++] = (Key){x, row + r * 64, w, 64, sym, labels[c], special};
        }
    }
    for (int i = 0; i < n; ++i) draw_key(&keys[i]);
    XSync(display, False);
}

int main(void)
{
    display = XOpenDisplay(NULL);
    if (display == NULL) return 1;
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    window = XCreateSimpleWindow(display, root, 0, 960, WIDTH, HEIGHT, 0,
                                 pixel(FIELD_M_FOREGROUND),
                                 pixel(FIELD_M_BACKGROUND));
    XSelectInput(display, window, ExposureMask | ButtonPressMask | ButtonReleaseMask);
    set_wm_hints();
    XMapRaised(display, window);
    XRaiseWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);
    font = XftFontOpenName(display, screen, "DejaVu Sans Mono:size=16");
    draw = XftDrawCreate(display, window,
                         DefaultVisual(display, screen),
                         DefaultColormap(display, screen));
    XftColorAllocName(display, DefaultVisual(display, screen),
                      DefaultColormap(display, screen),
                      FIELD_M_FOREGROUND, &color_text);
    XftColorAllocName(display, DefaultVisual(display, screen),
                      DefaultColormap(display, screen),
                      FIELD_M_BACKGROUND, &color_paper);
    build_and_draw();
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == Expose) build_and_draw();
        if (event.type == ButtonPress) {
            int x = event.xbutton.x, y = event.xbutton.y;
            if (y < 64 && terminal && x < 720) pressed = x / 72;
            build_and_draw();
        } else if (event.type == ButtonRelease) {
            int x = event.xbutton.x, y = event.xbutton.y;
            pressed = -1;
            int row = terminal ? 64 : 0;
            if (y < row) {
                int c = x / 72;
                if (c == 2) ctrl = 1;
                else if (c >= 0 && c < 10) {
                    KeySym syms[] = {XK_Escape,XK_Tab,XK_Control_L,XK_Left,XK_Up,
                                     XK_Down,XK_Right,XK_bar,XK_asciitilde,XK_slash};
                    Key key = {x, y, 72, 64, syms[c], "", c == 2 ? 4 : 0};
                    key_action(&key);
                }
            } else {
                int r = (y - row) / 64, c = x / 72;
                if (r == 0) {
                    Key key = {x,y,72,64,(KeySym)(XK_1+c),"",0}; key_action(&key);
                } else if (r == 1) {
                    Key key = {x,y,72,64,(KeySym)(XK_q+c),"",0}; key_action(&key);
                } else if (r == 2) {
                    KeySym sym = page ? (c == 0 ? XK_minus : XK_0+c) : XK_a+c;
                    Key key = {x,y,72,64,sym,"",0}; key_action(&key);
                } else {
                    if (c == 0) { shifted = !shifted; }
                    else if (c == 9) {
                        KeyCode code = XKeysymToKeycode(display, XK_BackSpace);
                        Window focus = None;
                        int revert = 0;
                        XGetInputFocus(display, &focus, &revert);
                        if (focus == None || focus == PointerRoot)
                            focus = DefaultRootWindow(display);
                        XKeyEvent press, release;
                        memset(&press, 0, sizeof(press));
                        memset(&release, 0, sizeof(release));
                        press.type = KeyPress; release.type = KeyRelease;
                        press.display = release.display = display;
                        press.window = release.window = focus;
                        press.root = release.root = DefaultRootWindow(display);
                        press.keycode = release.keycode = code;
                        press.time = release.time = CurrentTime;
                        press.same_screen = release.same_screen = True;
                        XSendEvent(display, focus, True, KeyPressMask, (XEvent *)&press);
                        XSendEvent(display, focus, True, KeyReleaseMask, (XEvent *)&release);
                        XFlush(display);
                    } else {
                        Key key = {x,y,72,64,(KeySym)(XK_z+c-1),"",0}; key_action(&key);
                    }
                }
            }
            build_and_draw();
        }
    }
    XftDrawDestroy(draw);
    XftFontClose(display, font);
    XCloseDisplay(display);
    return 0;
}
