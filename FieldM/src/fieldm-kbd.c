#define _POSIX_C_SOURCE 200809L

#include "theme.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 720
#define HEIGHT FIELD_M_KEYBOARD_HEIGHT
#define KEY_HEIGHT 64
#define LONG_PRESS_MS 400
#define REPEAT_DELAY_MS 400
#define REPEAT_INTERVAL_MS 60

typedef enum {
    KEY_NORMAL,
    KEY_SHIFT,
    KEY_BACKSPACE,
    KEY_SYMBOLS,
    KEY_LETTERS,
    KEY_SPACE,
    KEY_ENTER,
    KEY_TERMINAL,
    KEY_CTRL,
    KEY_ESCAPE,
    KEY_TAB,
    KEY_LEFT,
    KEY_UP,
    KEY_DOWN,
    KEY_RIGHT
} KeyAction;

typedef struct {
    const char *label;
    const char *shift_label;
    KeySym keysym;
    KeySym shifted_keysym;
    const char *alternate;
    int units;
    KeyAction action;
} KeyDef;

typedef struct {
    KeyDef def;
    int x, y, w, h;
} Key;

static Display *display;
static Window window;
static GC gc;
static XftDraw *draw;
static XftFont *font;
static XftColor text_color, paper_color;
static Key keys[64];
static int key_count;
static int symbols_page;
static int terminal_page;
static int shifted;
static int caps;
static int sticky_ctrl;
static int pressed_index = -1;
static int repeat_index = -1;
static unsigned long long press_started;
static unsigned long long repeat_due;
static int long_press_sent;
static volatile sig_atomic_t layout_request;

static void layout_signal_handler(int signal)
{
    layout_request = signal == SIGUSR1 ? 1 : 2;
}

static unsigned long pixel(const char *name)
{
    XColor exact, nearest;
    XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                     name, &exact, &nearest);
    return nearest.pixel;
}

static unsigned long long monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000ULL +
           (unsigned long long)now.tv_nsec / 1000000ULL;
}

static void set_wm_hints(void)
{
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    XChangeWindowAttributes(display, window, CWOverrideRedirect, &attrs);

    XSizeHints hints = {0};
    hints.flags = PPosition | PSize;
    hints.x = 0;
    hints.y = 960;
    hints.width = WIDTH;
    hints.height = HEIGHT;
    XSetWMNormalHints(display, window, &hints);

    XClassHint class_hint = {"fieldm-kbd", "fieldm-kbd"};
    XSetClassHint(display, window, &class_hint);
    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&type, 1);
    unsigned long strut[4] = {0, 0, 0, HEIGHT};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)strut, 4);
    unsigned long partial[12] = {0, 0, 0, HEIGHT,
                                 0, 0, 0, 0, 0, WIDTH - 1, 0, 0};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)partial, 12);
    XWMHints wm_hints = {0};
    wm_hints.flags = InputHint;
    wm_hints.input = False;
    XSetWMHints(display, window, &wm_hints);
}

static KeySym letter_keysym(char letter)
{
    return (KeySym)(XK_a + (letter - 'a'));
}

static void add_key_row(const KeyDef *defs, int count, int y)
{
    int unit = WIDTH / 10;
    int x = 0;
    for (int i = 0; i < count; ++i) {
        int width = defs[i].units * unit;
        if (i == count - 1) {
            width = WIDTH - x;
        }
        keys[key_count].def = defs[i];
        keys[key_count].x = x;
        keys[key_count].y = y;
        keys[key_count].w = width;
        keys[key_count].h = KEY_HEIGHT;
        ++key_count;
        x += width;
    }
}

static KeyDef normal_key(const char *label, KeySym keysym,
                         const char *alternate)
{
    KeyDef def = {label, NULL, keysym, NoSymbol, alternate, 1, KEY_NORMAL};
    return def;
}

static void build_layout(void)
{
    key_count = 0;
    int y = 0;

    if (terminal_page) {
        KeyDef terminal[] = {
            {"Esc", NULL, XK_Escape, NoSymbol, NULL, 1, KEY_ESCAPE},
            {"Tab", NULL, XK_Tab, NoSymbol, NULL, 1, KEY_TAB},
            {"Ctrl", NULL, XK_Control_L, NoSymbol, NULL, 1, KEY_CTRL},
            {"<", NULL, XK_Left, NoSymbol, NULL, 1, KEY_LEFT},
            {"^", NULL, XK_Up, NoSymbol, NULL, 1, KEY_UP},
            {"v", NULL, XK_Down, NoSymbol, NULL, 1, KEY_DOWN},
            {" >", NULL, XK_Right, NoSymbol, NULL, 1, KEY_RIGHT},
            {"|", NULL, XK_bar, NoSymbol, NULL, 1, KEY_NORMAL},
            {"~", NULL, XK_asciitilde, NoSymbol, NULL, 1, KEY_NORMAL},
            {"/", NULL, XK_slash, NoSymbol, NULL, 1, KEY_NORMAL}
        };
        add_key_row(terminal, 10, y);
        y += KEY_HEIGHT;
    }

    if (symbols_page) {
        KeyDef digits[] = {
            normal_key("1", XK_1, NULL), normal_key("2", XK_2, NULL),
            normal_key("3", XK_3, NULL), normal_key("4", XK_4, NULL),
            normal_key("5", XK_5, NULL), normal_key("6", XK_6, NULL),
            normal_key("7", XK_7, NULL), normal_key("8", XK_8, NULL),
            normal_key("9", XK_9, NULL), normal_key("0", XK_0, NULL)
        };
        add_key_row(digits, 10, y);
        y += KEY_HEIGHT;

        KeyDef punctuation1[] = {
            normal_key("-", XK_minus, NULL), normal_key("/", XK_slash, NULL),
            normal_key(":", XK_colon, NULL), normal_key(";", XK_semicolon, NULL),
            normal_key("(", XK_parenleft, NULL), normal_key(")", XK_parenright, NULL),
            normal_key("$", XK_dollar, NULL), normal_key("&", XK_ampersand, NULL),
            normal_key("@", XK_at, NULL), normal_key("\"", XK_quotedbl, NULL)
        };
        add_key_row(punctuation1, 10, y);
        y += KEY_HEIGHT;

        KeyDef punctuation2[] = {
            normal_key(".", XK_period, NULL), normal_key(",", XK_comma, NULL),
            normal_key("?", XK_question, NULL), normal_key("!", XK_exclam, NULL),
            normal_key("'", XK_apostrophe, NULL), normal_key("#", XK_numbersign, NULL),
            normal_key("%", XK_percent, NULL), normal_key("*", XK_asterisk, NULL),
            normal_key("+", XK_plus, NULL), normal_key("=", XK_equal, NULL)
        };
        add_key_row(punctuation2, 10, y);
        y += KEY_HEIGHT;

        KeyDef bottom[] = {
            {"ABC", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_LETTERS},
            {"Term", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_TERMINAL},
            normal_key("_", XK_underscore, NULL),
            normal_key(" ", XK_space, NULL),
            normal_key("_", XK_underscore, NULL),
            {"Enter", NULL, XK_Return, NoSymbol, NULL, 2, KEY_ENTER}
        };
        bottom[3].units = 4;
        add_key_row(bottom, 6, y);
    } else {
        const char *qwerty = "qwertyuiop";
        const char *digits_alt = "1234567890";
        KeyDef top[10];
        for (int i = 0; i < 10; ++i) {
            char *base = calloc(2, 1);
            char *upper = calloc(2, 1);
            char *alternate = calloc(2, 1);
            base[0] = qwerty[i];
            upper[0] = (char)(qwerty[i] - 'a' + 'A');
            alternate[0] = digits_alt[i];
            top[i] = (KeyDef){base, upper, letter_keysym(qwerty[i]),
                              letter_keysym(upper[0]), alternate, 1, KEY_NORMAL};
        }
        add_key_row(top, 10, y);
        y += KEY_HEIGHT;

        const char *second = "asdfghjkl";
        KeyDef middle[9];
        for (int i = 0; i < 9; ++i) {
            char *base = calloc(2, 1);
            char *upper = calloc(2, 1);
            base[0] = second[i];
            upper[0] = (char)(second[i] - 'a' + 'A');
            middle[i] = (KeyDef){base, upper, letter_keysym(second[i]),
                                 letter_keysym(upper[0]), NULL, 1, KEY_NORMAL};
        }
        add_key_row(middle, 9, y);
        y += KEY_HEIGHT;

        KeyDef third[] = {
            {"Shift", "Shift*", NoSymbol, NoSymbol, NULL, 2, KEY_SHIFT},
            normal_key("z", XK_z, "0"),
            normal_key("x", XK_x, "1"),
            normal_key("c", XK_c, "2"),
            normal_key("v", XK_v, "3"),
            normal_key("b", XK_b, "4"),
            normal_key("n", XK_n, "5"),
            normal_key("m", XK_m, "6"),
            {"Back", NULL, XK_BackSpace, NoSymbol, NULL, 1, KEY_BACKSPACE}
        };
        for (int i = 1; i < 8; ++i) {
            char upper = (char)(third[i].label[0] - 'a' + 'A');
            char *upper_label = calloc(2, 1);
            upper_label[0] = upper;
            third[i].shift_label = upper_label;
            third[i].shifted_keysym = letter_keysym(upper);
        }
        add_key_row(third, 9, y);
        y += KEY_HEIGHT;

        KeyDef bottom[] = {
            {"?123", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_SYMBOLS},
            {"Term", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_TERMINAL},
            normal_key(",", XK_comma, NULL),
            normal_key(" ", XK_space, NULL),
            normal_key(".", XK_period, NULL),
            {"Enter", NULL, XK_Return, NoSymbol, NULL, 2, KEY_ENTER}
        };
        bottom[3].units = 4;
        add_key_row(bottom, 6, y);
    }
}

static void fake_key(KeySym keysym)
{
    KeyCode code = XKeysymToKeycode(display, keysym);
    if (code == 0) return;
    XTestFakeKeyEvent(display, code, True, CurrentTime);
    XTestFakeKeyEvent(display, code, False, CurrentTime);
}

static void fake_modded_keysym(KeySym keysym)
{
    int need_shift = (shifted || caps) && keysym >= XK_a && keysym <= XK_z;
    if (need_shift) {
        KeySym lower = NoSymbol, upper = NoSymbol;
        XConvertCase(keysym, &lower, &upper);
        keysym = upper;
    }
    if (sticky_ctrl) {
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L),
                          True, CurrentTime);
    }
    if (need_shift) {
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L),
                          True, CurrentTime);
    }
    fake_key(keysym);
    if (need_shift) {
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L),
                          False, CurrentTime);
    }
    if (sticky_ctrl) {
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L),
                          False, CurrentTime);
    }
}

static void activate_key(Key *key)
{
    KeyDef *def = &key->def;
    if (def->action == KEY_SHIFT) {
        if (shifted) {
            caps = !caps;
        }
        shifted = !shifted;
        build_layout();
        return;
    }
    if (def->action == KEY_SYMBOLS) {
        symbols_page = 1;
        build_layout();
        return;
    }
    if (def->action == KEY_LETTERS) {
        symbols_page = 0;
        build_layout();
        return;
    }
    if (def->action == KEY_CTRL) {
        sticky_ctrl = !sticky_ctrl;
        return;
    }
    if (def->action == KEY_TERMINAL) {
        terminal_page = !terminal_page;
        build_layout();
        return;
    }
    if (def->action == KEY_BACKSPACE) {
        fake_key(XK_BackSpace);
        return;
    }
    if (def->action == KEY_LEFT) { fake_key(XK_Left); return; }
    if (def->action == KEY_UP) { fake_key(XK_Up); return; }
    if (def->action == KEY_DOWN) { fake_key(XK_Down); return; }
    if (def->action == KEY_RIGHT) { fake_key(XK_Right); return; }
    if (def->action == KEY_ESCAPE) { fake_key(XK_Escape); return; }
    if (def->action == KEY_TAB) { fake_key(XK_Tab); return; }
    if (def->action == KEY_ENTER) { fake_key(XK_Return); return; }
    if (def->action == KEY_SPACE) { fake_key(XK_space); return; }

    if (def->keysym != NoSymbol) {
        if ((shifted || caps) && def->shifted_keysym != NoSymbol) {
            fake_modded_keysym(def->shifted_keysym);
        } else {
            fake_modded_keysym(def->keysym);
        }
    }
    sticky_ctrl = 0;
    if (shifted && !caps) shifted = 0;
    build_layout();
}

static void send_long_alternate(Key *key)
{
    if (key->def.alternate == NULL) return;
    char text = key->def.alternate[0];
    KeySym sym = (text >= '0' && text <= '9') ? (KeySym)(XK_0 + text - '0')
                                                : NoSymbol;
    if (sym != NoSymbol) fake_key(sym);
    long_press_sent = 1;
    if (shifted && !caps) shifted = 0;
}

static void draw_key(Key *key)
{
    int index = (int)(key - keys);
    unsigned long background = index == pressed_index
        ? pixel(FIELD_M_FOREGROUND) : pixel(FIELD_M_BACKGROUND);
    XSetForeground(display, gc, background);
    XFillRectangle(display, window, gc, key->x, key->y, key->w, key->h);
    XSetForeground(display, gc, pixel(FIELD_M_FOREGROUND));
    XSetLineAttributes(display, gc, FIELD_M_BORDER_WIDTH,
                       LineSolid, CapButt, JoinMiter);
    XDrawRectangle(display, window, gc, key->x, key->y,
                   key->w - 1, key->h - 1);
    const char *label = (shifted || caps) && key->def.shift_label != NULL
        ? key->def.shift_label : key->def.label;
    if (key->def.action == KEY_SHIFT && caps)
        label = "Shift*";
    XftColor *foreground = index == pressed_index ? &paper_color : &text_color;
    XGlyphInfo extents;
    XftTextExtentsUtf8(display, font, (const FcChar8 *)label,
                       (int)strlen(label), &extents);
    int tx = key->x + (key->w - (int)extents.width) / 2 - (int)extents.x;
    int ty = key->y + (key->h + (int)font->ascent - (int)font->descent) / 2;
    XftDrawStringUtf8(draw, foreground, font, tx, ty,
                      (const FcChar8 *)label, (int)strlen(label));
}

static void draw_layout(void)
{
    XClearWindow(display, window);
    XSetForeground(display, gc, pixel(FIELD_M_BACKGROUND));
    XFillRectangle(display, window, gc, 0, 0, WIDTH, HEIGHT);
    for (int i = 0; i < key_count; ++i) draw_key(&keys[i]);
    XSync(display, False);
}

static int hit_key(int x, int y)
{
    for (int i = 0; i < key_count; ++i) {
        if (x >= keys[i].x && x < keys[i].x + keys[i].w &&
            y >= keys[i].y && y < keys[i].y + keys[i].h)
            return i;
    }
    return -1;
}

int main(void)
{
    signal(SIGUSR1, layout_signal_handler);
    signal(SIGUSR2, layout_signal_handler);
    display = XOpenDisplay(NULL);
    if (display == NULL) return 1;
    int screen = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                 0, 960, WIDTH, HEIGHT, 0,
                                 pixel(FIELD_M_FOREGROUND),
                                 pixel(FIELD_M_BACKGROUND));
    XSelectInput(display, window,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask);
    set_wm_hints();
    XMapRaised(display, window);
    XRaiseWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);
    font = XftFontOpenName(display, screen, "DejaVu Sans Mono:size=16");
    draw = XftDrawCreate(display, window, DefaultVisual(display, screen),
                         DefaultColormap(display, screen));
    XftColorAllocName(display, DefaultVisual(display, screen),
                      DefaultColormap(display, screen),
                      FIELD_M_FOREGROUND, &text_color);
    XftColorAllocName(display, DefaultVisual(display, screen),
                      DefaultColormap(display, screen),
                      FIELD_M_BACKGROUND, &paper_color);

    build_layout();
    draw_layout();

    struct pollfd fd = {ConnectionNumber(display), POLLIN, 0};
    for (;;) {
        if (layout_request != 0) {
            if (layout_request == 1) symbols_page = !symbols_page;
            else terminal_page = !terminal_page;
            layout_request = 0;
            build_layout();
            draw_layout();
        }
        unsigned long long now = monotonic_ms();
        int timeout = -1;
        if (pressed_index >= 0) {
            unsigned long long due = press_started + LONG_PRESS_MS;
            if (repeat_index >= 0) due = repeat_due;
            long long remaining = (long long)due - (long long)now;
            timeout = remaining > 0 ? (int)remaining : 0;
        }
        if (poll(&fd, 1, timeout) < 0) continue;

        if (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == Expose) {
                draw_layout();
            } else if (event.type == ButtonPress) {
                pressed_index = hit_key(event.xbutton.x, event.xbutton.y);
                if (pressed_index >= 0) {
                    press_started = monotonic_ms();
                    repeat_due = press_started + REPEAT_DELAY_MS;
                    repeat_index = (keys[pressed_index].def.action == KEY_BACKSPACE ||
                                    keys[pressed_index].def.action == KEY_LEFT ||
                                    keys[pressed_index].def.action == KEY_UP ||
                                    keys[pressed_index].def.action == KEY_DOWN ||
                                    keys[pressed_index].def.action == KEY_RIGHT)
                        ? pressed_index : -1;
                    long_press_sent = 0;
                    draw_layout();
                }
            } else if (event.type == ButtonRelease) {
                int released = hit_key(event.xbutton.x, event.xbutton.y);
                if (released >= 0 && released == pressed_index) {
                    if (!long_press_sent) activate_key(&keys[released]);
                }
                pressed_index = -1;
                repeat_index = -1;
                draw_layout();
            }
        }

        now = monotonic_ms();
        if (pressed_index >= 0 &&
            !long_press_sent &&
            repeat_index < 0 &&
            now - press_started >= LONG_PRESS_MS) {
            send_long_alternate(&keys[pressed_index]);
            draw_layout();
        }
        if (repeat_index >= 0 && now >= repeat_due) {
            activate_key(&keys[repeat_index]);
            repeat_due = now + REPEAT_INTERVAL_MS;
            draw_layout();
        }
    }
}
