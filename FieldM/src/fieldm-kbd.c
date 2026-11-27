#define _POSIX_C_SOURCE 200809L

#include "theme.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 720
#define DEFAULT_KEY_HEIGHT 64

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
    KEY_CLOSE,
    KEY_ZOOM_OUT,
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
static char label_storage[256][2];
static int label_storage_count;
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
static int key_height = DEFAULT_KEY_HEIGHT;
static int number_row;
static int long_press_ms = 400;
static int repeat_delay_ms = 400;
static int repeat_interval_ms = 60;
static int highlight_enabled = 1;
static int terminal_mode = 1;
static int terminal_active;
static int keyboard_mapped;
static int window_height = FIELD_M_KEYBOARD_HEIGHT;
static volatile sig_atomic_t config_reload;
static volatile sig_atomic_t terminal_on;
static volatile sig_atomic_t terminal_off;

static void layout_signal_handler(int signal)
{
    if (signal == SIGHUP) config_reload = 1;
    else if (signal == SIGUSR1) terminal_on = 1;
    else if (signal == SIGUSR2) terminal_off = 1;
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
    XSizeHints hints = {0};
    hints.flags = PPosition | PSize;
    hints.x = 0;
    hints.y = DisplayHeight(display, DefaultScreen(display)) - window_height;
    hints.width = WIDTH;
    hints.height = window_height;
    XSetWMNormalHints(display, window, &hints);

    XClassHint class_hint = {"fieldm-kbd", "fieldm-kbd"};
    XSetClassHint(display, window, &class_hint);
    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&type, 1);
    unsigned long strut[4] = {0, 0, 0, (unsigned long)window_height};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)strut, 4);
    unsigned long partial[12] = {0, 0, 0, (unsigned long)window_height,
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

static char *single_character_label(char character)
{
    if (label_storage_count >= (int)(sizeof(label_storage) /
                                     sizeof(label_storage[0]))) {
        return NULL;
    }
    char *label = label_storage[label_storage_count++];
    label[0] = character;
    label[1] = '\0';
    return label;
}

static void load_config(void);
static void update_window_geometry(void);

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
        keys[key_count].h = key_height;
        ++key_count;
        x += width;
    }
}

static void load_config(void)
{
    key_height = DEFAULT_KEY_HEIGHT;
    number_row = 0;
    terminal_mode = 1;
    long_press_ms = 400;
    repeat_delay_ms = 400;
    repeat_interval_ms = 60;
    highlight_enabled = 1;
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') return;
    char path[1024];
    int length = snprintf(path, sizeof(path),
                          "%s/.config/fieldm/keyboard.conf", home);
    if (length < 0 || (size_t)length >= sizeof(path)) return;
    FILE *file = fopen(path, "r");
    if (file == NULL) return;
    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        char key[64], value[128];
        if (sscanf(line, "%63[^=]=%127s", key, value) != 2) continue;
        if (strcmp(key, "key_height") == 0) {
            int height = atoi(value);
            if (height == 56 || height == 64 || height == 72)
                key_height = height;
        } else if (strcmp(key, "number_row") == 0) {
            number_row = strcmp(value, "on") == 0;
        } else if (strcmp(key, "terminal_row") == 0) {
            terminal_mode = strcmp(value, "always") == 0 ? 2 :
                            strcmp(value, "off") == 0 ? 0 : 1;
        } else if (strcmp(key, "long_press_ms") == 0) {
            int delay = atoi(value);
            if (delay == 300 || delay == 400 || delay == 600)
                long_press_ms = delay;
        } else if (strcmp(key, "repeat_speed") == 0) {
            if (strcmp(value, "slow") == 0) {
                repeat_delay_ms = 500; repeat_interval_ms = 100;
            } else if (strcmp(value, "fast") == 0) {
                repeat_delay_ms = 300; repeat_interval_ms = 40;
            } else if (strcmp(value, "normal") == 0) {
                repeat_delay_ms = 400; repeat_interval_ms = 60;
            }
        } else if (strcmp(key, "highlight") == 0) {
            highlight_enabled = strcmp(value, "off") != 0;
        }
    }
    fclose(file);
}

static void update_window_geometry(void)
{
    window_height = key_count > 0
        ? keys[key_count - 1].y + key_height : 0;
    if (window == None) return;
    int y = DisplayHeight(display, DefaultScreen(display)) - window_height;
    XMoveResizeWindow(display, window, 0, y, WIDTH, (unsigned int)window_height);
    unsigned long strut[4] = {0, 0, 0, (unsigned long)window_height};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)strut, 4);
    unsigned long partial[12] = {0, 0, 0, (unsigned long)window_height,
                                 0, 0, 0, 0, 0, WIDTH - 1, 0, 0};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)partial, 12);
    XFlush(display);
}

static int keyboard_requested(void)
{
    static const char *const leases[] = {
        "/run/fieldm-keyboard/manual",
        "/run/fieldm-keyboard/entry",
        "/run/fieldm-keyboard/terminal"
    };
    for (size_t i = 0; i < sizeof(leases) / sizeof(leases[0]); ++i) {
        if (access(leases[i], F_OK) == 0) return 1;
    }
    return 0;
}

static void update_keyboard_visibility(void)
{
    int requested = keyboard_requested();
    if (requested == keyboard_mapped) return;
    keyboard_mapped = requested;
    if (requested) {
        XMapRaised(display, window);
        XRaiseWindow(display, window);
    } else {
        XUnmapWindow(display, window);
    }
    XFlush(display);
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
    label_storage_count = 0;
    int y = 0;

    if (terminal_page && (terminal_mode == 2 ||
                          (terminal_mode == 1 && terminal_active))) {
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
            {"A−", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_ZOOM_OUT}
        };
        add_key_row(terminal, 10, y);
        y += key_height;
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
        y += key_height;

        KeyDef punctuation1[] = {
            normal_key("-", XK_minus, NULL), normal_key("/", XK_slash, NULL),
            normal_key(":", XK_colon, NULL), normal_key(";", XK_semicolon, NULL),
            normal_key("(", XK_parenleft, NULL), normal_key(")", XK_parenright, NULL),
            normal_key("$", XK_dollar, NULL), normal_key("&", XK_ampersand, NULL),
            normal_key("@", XK_at, NULL), normal_key("\"", XK_quotedbl, NULL)
        };
        add_key_row(punctuation1, 10, y);
        y += key_height;

        KeyDef punctuation2[] = {
            normal_key(".", XK_period, NULL), normal_key(",", XK_comma, NULL),
            normal_key("?", XK_question, NULL), normal_key("!", XK_exclam, NULL),
            normal_key("'", XK_apostrophe, NULL), normal_key("#", XK_numbersign, NULL),
            normal_key("%", XK_percent, NULL), normal_key("*", XK_asterisk, NULL),
            normal_key("+", XK_plus, NULL), normal_key("=", XK_equal, NULL)
        };
        add_key_row(punctuation2, 10, y);
        y += key_height;

        KeyDef punctuation3[] = {
            normal_key("_", XK_underscore, NULL), normal_key("\\", XK_backslash, NULL),
            normal_key("|", XK_bar, NULL), normal_key("~", XK_asciitilde, NULL),
            normal_key("<", XK_less, NULL), normal_key(">", XK_greater, NULL),
            normal_key("[", XK_bracketleft, NULL), normal_key("]", XK_bracketright, NULL),
            normal_key("{", XK_braceleft, NULL), normal_key("}", XK_braceright, NULL)
        };
        add_key_row(punctuation3, 10, y);
        y += key_height;

        KeyDef bottom[] = {
            {"ABC", NULL, NoSymbol, NoSymbol, NULL, 2, KEY_LETTERS},
            {">_", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_TERMINAL},
            normal_key(",", XK_comma, NULL),
            {"space", NULL, XK_space, NoSymbol, NULL, 3, KEY_SPACE},
            normal_key(".", XK_period, NULL),
            {"×", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_CLOSE},
            {"↵", NULL, XK_Return, NoSymbol, NULL, 1, KEY_ENTER}
        };
        add_key_row(bottom, 7, y);
    } else {
        if (number_row) {
            KeyDef digits[] = {
                normal_key("1", XK_1, NULL), normal_key("2", XK_2, NULL),
                normal_key("3", XK_3, NULL), normal_key("4", XK_4, NULL),
                normal_key("5", XK_5, NULL), normal_key("6", XK_6, NULL),
                normal_key("7", XK_7, NULL), normal_key("8", XK_8, NULL),
                normal_key("9", XK_9, NULL), normal_key("0", XK_0, NULL)
            };
            add_key_row(digits, 10, y);
            y += key_height;
        }
        const char *qwerty = "qwertyuiop";
        const char *digits_alt = "1234567890";
        KeyDef top[10];
        for (int i = 0; i < 10; ++i) {
            char *base = single_character_label(qwerty[i]);
            char *upper = single_character_label(
                (char)(qwerty[i] - 'a' + 'A'));
            char *alternate = single_character_label(digits_alt[i]);
            top[i] = (KeyDef){base, upper, letter_keysym(qwerty[i]),
                              letter_keysym(upper[0]), alternate, 1, KEY_NORMAL};
        }
        add_key_row(top, 10, y);
        y += key_height;

        const char *second = "asdfghjkl";
        KeyDef middle[9];
        for (int i = 0; i < 9; ++i) {
            char *base = single_character_label(second[i]);
            char *upper = single_character_label(
                (char)(second[i] - 'a' + 'A'));
            middle[i] = (KeyDef){base, upper, letter_keysym(second[i]),
                                 letter_keysym(upper[0]), NULL, 1, KEY_NORMAL};
        }
        add_key_row(middle, 9, y);
        y += key_height;

        KeyDef third[] = {
            {"⇧", "⇧*", NoSymbol, NoSymbol, NULL, 2, KEY_SHIFT},
            normal_key("z", XK_z, NULL),
            normal_key("x", XK_x, NULL),
            normal_key("c", XK_c, NULL),
            normal_key("v", XK_v, NULL),
            normal_key("b", XK_b, NULL),
            normal_key("n", XK_n, NULL),
            normal_key("m", XK_m, NULL),
            {"⌫", NULL, XK_BackSpace, NoSymbol, NULL, 1, KEY_BACKSPACE}
        };
        for (int i = 1; i < 8; ++i) {
            char upper = (char)(third[i].label[0] - 'a' + 'A');
            third[i].shift_label = single_character_label(upper);
            third[i].shifted_keysym = letter_keysym(upper);
        }
        add_key_row(third, 9, y);
        y += key_height;

        KeyDef bottom[] = {
            {"?123", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_SYMBOLS},
            {">_", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_TERMINAL},
            normal_key(",", XK_comma, NULL),
            {"space", NULL, XK_space, NoSymbol, NULL, 3, KEY_SPACE},
            normal_key(".", XK_period, NULL),
            {"×", NULL, NoSymbol, NoSymbol, NULL, 1, KEY_CLOSE},
            {"↵", NULL, XK_Return, NoSymbol, NULL, 2, KEY_ENTER}
        };
        add_key_row(bottom, 7, y);
    }
    update_window_geometry();
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
    KeyCode keycode = XKeysymToKeycode(display, keysym);
    int need_shift = keycode != 0 &&
        XkbKeycodeToKeysym(display, keycode, 0, 0) != keysym &&
        XkbKeycodeToKeysym(display, keycode, 0, 1) == keysym;
    if ((shifted || caps) && keysym >= XK_a && keysym <= XK_z) {
        KeySym lower = NoSymbol, upper = NoSymbol;
        XConvertCase(keysym, &lower, &upper);
        keysym = upper;
        need_shift = 1;
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

static void fake_chord(KeySym modifier, KeySym keysym)
{
    KeyCode modifier_code = XKeysymToKeycode(display, modifier);
    if (modifier_code == 0 || XKeysymToKeycode(display, keysym) == 0) {
        return;
    }
    XTestFakeKeyEvent(display, modifier_code, True, CurrentTime);
    fake_key(keysym);
    XTestFakeKeyEvent(display, modifier_code, False, CurrentTime);
    XFlush(display);
}

static int activate_key(Key *key)
{
    KeyDef *def = &key->def;
    if (def->action == KEY_SHIFT) {
        if (shifted) {
            caps = !caps;
        }
        shifted = !shifted;
        build_layout();
        return 1;
    }
    if (def->action == KEY_SYMBOLS) {
        symbols_page = 1;
        build_layout();
        return 1;
    }
    if (def->action == KEY_LETTERS) {
        symbols_page = 0;
        build_layout();
        return 1;
    }
    if (def->action == KEY_CTRL) {
        sticky_ctrl = !sticky_ctrl;
        return 0;
    }
    if (def->action == KEY_CLOSE) {
        fake_chord(XK_Alt_L, XK_F4);
        return 0;
    }
    if (def->action == KEY_ZOOM_OUT) {
        fake_chord(XK_Shift_L, XK_KP_Subtract);
        return 0;
    }
    if (def->action == KEY_TERMINAL) {
        terminal_page = !terminal_page;
        build_layout();
        return 1;
    }
    int was_shifted = shifted;
    if (def->keysym != NoSymbol) {
        KeySym sym = (shifted || caps) && def->shifted_keysym != NoSymbol
            ? def->shifted_keysym : def->keysym;
        fake_modded_keysym(sym);
    }
    sticky_ctrl = 0;
    if (shifted && !caps) shifted = 0;
    if (was_shifted != shifted) {
        build_layout();
        return 1;
    }
    return 0;
}

static int send_long_alternate(Key *key)
{
    if (key->def.alternate == NULL) return 0;
    char text = key->def.alternate[0];
    KeySym sym = (text >= '0' && text <= '9') ? (KeySym)(XK_0 + text - '0')
                                                : NoSymbol;
    if (sym != NoSymbol) fake_key(sym);
    long_press_sent = 1;
    int was_shifted = shifted;
    if (shifted && !caps) shifted = 0;
    if (was_shifted != shifted) {
        build_layout();
        return 1;
    }
    return 0;
}

static void draw_key(Key *key)
{
    int index = (int)(key - keys);
    unsigned long background = highlight_enabled && index == pressed_index
        ? text_color.pixel : paper_color.pixel;
    XSetForeground(display, gc, background);
    XFillRectangle(display, window, gc, key->x, key->y, key->w, key->h);
    XSetForeground(display, gc, text_color.pixel);
    XSetLineAttributes(display, gc, FIELD_M_BORDER_WIDTH,
                       LineSolid, CapButt, JoinMiter);
    XDrawRectangle(display, window, gc, key->x, key->y,
                   key->w - 1, key->h - 1);
    const char *label = (shifted || caps) && key->def.shift_label != NULL
        ? key->def.shift_label : key->def.label;
    if (key->def.action == KEY_SHIFT && caps)
        label = "⇧*";
    XftColor *foreground = highlight_enabled && index == pressed_index
        ? &paper_color : &text_color;
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
    for (int i = 0; i < key_count; ++i) draw_key(&keys[i]);
    XFlush(display);
}

static void draw_key_and_flush(int index)
{
    if (index >= 0 && index < key_count) {
        draw_key(&keys[index]);
        XFlush(display);
    }
}

static int font_fits_key_labels(XftFont *candidate)
{
    for (int i = 0; i < key_count; ++i) {
        const char *labels[] = {keys[i].def.label, keys[i].def.shift_label};
        for (size_t j = 0; j < sizeof(labels) / sizeof(labels[0]); ++j) {
            if (labels[j] == NULL) continue;
            XGlyphInfo extents;
            XftTextExtentsUtf8(display, candidate,
                               (const FcChar8 *)labels[j],
                               (int)strlen(labels[j]), &extents);
            if (extents.xOff > keys[i].w - 16 ||
                candidate->ascent + candidate->descent > keys[i].h - 12) {
                return 0;
            }
        }
    }
    return 1;
}

static void update_font(void)
{
    if (font != NULL) {
        XftFontClose(display, font);
        font = NULL;
    }
    double size = (double)key_height * 0.22;
    for (; size >= 6.0; size -= 0.5) {
        char pattern[96];
        snprintf(pattern, sizeof(pattern), "DejaVu Sans:size=%.1f", size);
        XftFont *candidate = XftFontOpenName(
            display, DefaultScreen(display), pattern);
        if (candidate == NULL) continue;
        if (font_fits_key_labels(candidate)) {
            font = candidate;
            return;
        }
        XftFontClose(display, candidate);
    }
    font = XftFontOpenName(display, DefaultScreen(display),
                           "DejaVu Sans:size=6");
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

static int release_is_near_key(const Key *key, int x, int y)
{
    int dx = x < key->x ? key->x - x :
             x >= key->x + key->w ? x - (key->x + key->w - 1) : 0;
    int dy = y < key->y ? key->y - y :
             y >= key->y + key->h ? y - (key->y + key->h - 1) : 0;
    return dx * dx + dy * dy <= 24 * 24;
}

int main(void)
{
    signal(SIGUSR1, layout_signal_handler);
    signal(SIGUSR2, layout_signal_handler);
    signal(SIGHUP, layout_signal_handler);
    display = XOpenDisplay(NULL);
    if (display == NULL) return 1;
    terminal_active = access("/run/fieldm-keyboard/terminal", F_OK) == 0;
    load_config();
    build_layout();
    int screen = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                 0, DisplayHeight(display, screen) - window_height,
                                 WIDTH, (unsigned int)window_height, 0,
                                 pixel(FIELD_M_FOREGROUND),
                                 pixel(FIELD_M_BACKGROUND));
    XSelectInput(display, window,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask);
    set_wm_hints();
    keyboard_mapped = keyboard_requested();
    if (keyboard_mapped) {
        XMapRaised(display, window);
        XRaiseWindow(display, window);
    }
    gc = XCreateGC(display, window, 0, NULL);
    draw = XftDrawCreate(display, window, DefaultVisual(display, screen),
                         DefaultColormap(display, screen));
    XftColorAllocName(display, DefaultVisual(display, screen),
                      DefaultColormap(display, screen),
                      FIELD_M_FOREGROUND, &text_color);
    XftColorAllocName(display, DefaultVisual(display, screen),
                      DefaultColormap(display, screen),
                      FIELD_M_BACKGROUND, &paper_color);
    update_font();

    draw_layout();

    struct pollfd fd = {ConnectionNumber(display), POLLIN, 0};
    for (;;) {
        if (config_reload) {
            config_reload = 0;
            pressed_index = -1;
            repeat_index = -1;
            load_config();
            build_layout();
            update_font();
            update_keyboard_visibility();
            draw_layout();
        }
        if (terminal_on || terminal_off) {
            if (terminal_on) terminal_active = 1;
            if (terminal_off) terminal_active = 0;
            terminal_on = terminal_off = 0;
            build_layout();
            draw_layout();
        }
        unsigned long long now = monotonic_ms();
        int timeout = -1;
        if (pressed_index >= 0) {
            unsigned long long due = press_started + (unsigned long long)long_press_ms;
            if (repeat_index >= 0) due = repeat_due;
            long long remaining = (long long)due - (long long)now;
            timeout = remaining > 0 ? (int)remaining : 0;
        }
        int ready = 0;
        if (XPending(display) == 0) {
            ready = poll(&fd, 1, timeout);
            if (ready < 0 && errno != EINTR) continue;
        }
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == Expose) {
                draw_layout();
            } else if (event.type == ButtonPress) {
                pressed_index = hit_key(event.xbutton.x, event.xbutton.y);
                if (pressed_index >= 0) {
                    KeyAction action = keys[pressed_index].def.action;
                    if (action == KEY_SYMBOLS || action == KEY_LETTERS) {
                        int page_key = pressed_index;
                        pressed_index = -1;
                        repeat_index = -1;
                        activate_key(&keys[page_key]);
                        draw_layout();
                        continue;
                    }
                    press_started = monotonic_ms();
                    repeat_due = press_started + (unsigned long long)repeat_delay_ms;
                    repeat_index = (keys[pressed_index].def.action == KEY_BACKSPACE ||
                                    keys[pressed_index].def.action == KEY_LEFT ||
                                    keys[pressed_index].def.action == KEY_UP ||
                                    keys[pressed_index].def.action == KEY_DOWN ||
                                    keys[pressed_index].def.action == KEY_RIGHT)
                        ? pressed_index : -1;
                    long_press_sent = 0;
                    draw_key_and_flush(pressed_index);
                }
            } else if (event.type == ButtonRelease) {
                int pressed = pressed_index;
                int should_activate = pressed >= 0 &&
                    release_is_near_key(&keys[pressed],
                                        event.xbutton.x, event.xbutton.y);
                int redraw_all = 0;
                pressed_index = -1;
                repeat_index = -1;
                if (should_activate && !long_press_sent) {
                    redraw_all = activate_key(&keys[pressed]);
                }
                if (redraw_all) {
                    draw_layout();
                } else {
                    draw_key_and_flush(pressed);
                }
            }
        }

        now = monotonic_ms();
        if (pressed_index >= 0 &&
            !long_press_sent &&
            repeat_index < 0 &&
            now - press_started >= (unsigned long long)long_press_ms) {
            if (send_long_alternate(&keys[pressed_index])) {
                draw_layout();
            } else {
                draw_key_and_flush(pressed_index);
            }
        }
        if (repeat_index >= 0 && now >= repeat_due) {
            int redraw_all = activate_key(&keys[repeat_index]);
            repeat_due = now + (unsigned long long)repeat_interval_ms;
            if (redraw_all) {
                repeat_index = -1;
                draw_layout();
            }
        }
        (void)ready;
    }
}
