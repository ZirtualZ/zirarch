#define _POSIX_C_SOURCE 200809L

#include "screen_layout.h"

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <strings.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "theme.h"

enum {
    TITLE_HEIGHT = FIELD_M_CLOSE_SIZE,
    MIN_WIDTH = 240,
    MIN_HEIGHT = 140,
    RESIZE_BORDER = 12,
    SWITCHER_ROWS = 10,
    SWITCHER_WIDTH = 680,
    SWITCHER_ROW_HEIGHT = 64,
    STATUS_HEIGHT = 48,
    TASKBAR_HEIGHT = 0,
    TASKBAR_BUTTON_MIN_WIDTH = 180,
    TASKBAR_BUTTON_MAX_WIDTH = 320,
    DEFAULT_KEYBOARD_HEIGHT = FIELD_M_KEYBOARD_HEIGHT
};

typedef struct {
    Window client;
    Window frame;
    int x;
    int y;
    int width;
    int height;
    int fullscreen;
    int terminal;
    int browser;
    int dialog;
} ManagedWindow;

typedef struct {
    Window client;
    unsigned long long deadline;
} PendingClose;

static Display *display;
static Window root;
static ManagedWindow *clients;
static size_t client_capacity;
static PendingClose *pending_closes;
static size_t pending_close_capacity;
static size_t pending_close_count;
static int client_count;
static int focused = -1;
static unsigned long frame_color;
static unsigned long active_color;
static unsigned long switcher_color;
static unsigned long panel_color;
static unsigned long button_color;
static unsigned long button_active_color;
static unsigned long outline_color;
static unsigned long close_color;
static GC gc;
static XftFont *ui_font;
static XFontStruct *core_font;
static XftColor text_xft;
static XftColor muted_xft;
static XftColor accent_xft;
static XftColor dark_xft;
static int startup_failed;
static Window switcher_window;
static Window taskbar_window;
static Window keyboard_window;
static int keyboard_height = DEFAULT_KEYBOARD_HEIGHT;
static ScreenGeometry screen_layout;
static int taskbar_enabled = 0;
static int terminal_count;
static unsigned long long back_pressed_at;

static void show_switcher(void);
static void draw_taskbar(void);
static void sync_taskbars(void);
static void arrange_client(int index);
static void maximize_client(int index, int fullscreen);

static int title_height(const ManagedWindow *managed)
{
    return managed->fullscreen ? 0 : TITLE_HEIGHT;
}

static int border_width(const ManagedWindow *managed)
{
    return managed->fullscreen ? 0 : RESIZE_BORDER;
}

static unsigned long long monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (unsigned long long)now.tv_sec * 1000ULL +
           (unsigned long long)now.tv_nsec / 1000000ULL;
}

static int ensure_client_capacity(void)
{
    if ((size_t)client_count < client_capacity) {
        return 1;
    }
    if (client_capacity > SIZE_MAX / 2) {
        fprintf(stderr,
                "simple-wm: managed-window capacity limit reached\n");
        return 0;
    }
    size_t next_capacity = client_capacity == 0 ? 128 : client_capacity * 2;
    ManagedWindow *next = realloc(
        clients, next_capacity * sizeof(*next));
    if (next == NULL) {
        fprintf(stderr,
                "simple-wm: cannot grow managed-window storage to %zu clients\n",
                next_capacity);
        return 0;
    }
    clients = next;
    client_capacity = next_capacity;
    return 1;
}

static void shrink_client_capacity(void)
{
    if (client_capacity <= 128 ||
        (size_t)client_count >= client_capacity / 2) {
        return;
    }
    size_t next_capacity = client_capacity / 2;
    if (next_capacity < 128) {
        next_capacity = 128;
    }
    ManagedWindow *next = realloc(
        clients, next_capacity * sizeof(*next));
    if (next != NULL) {
        clients = next;
        client_capacity = next_capacity;
    }
}

static int ensure_pending_close_capacity(void)
{
    if (pending_close_count < pending_close_capacity) {
        return 1;
    }
    if (pending_close_capacity > SIZE_MAX / 2) {
        fprintf(stderr,
                "simple-wm: pending-close capacity limit reached\n");
        return 0;
    }
    size_t next_capacity = pending_close_capacity == 0
        ? 128 : pending_close_capacity * 2;
    PendingClose *next = realloc(
        pending_closes, next_capacity * sizeof(*next));
    if (next == NULL) {
        fprintf(stderr,
                "simple-wm: cannot grow pending-close storage to %zu entries\n",
                next_capacity);
        return 0;
    }
    for (size_t i = pending_close_capacity; i < next_capacity; ++i) {
        next[i].client = None;
        next[i].deadline = 0;
    }
    pending_closes = next;
    pending_close_capacity = next_capacity;
    return 1;
}

static void shrink_pending_close_capacity(void)
{
    if (pending_close_capacity <= 128 ||
        pending_close_count >= pending_close_capacity / 2) {
        return;
    }
    size_t next_capacity = pending_close_capacity / 2;
    if (next_capacity < 128) {
        next_capacity = 128;
    }
    PendingClose *next = realloc(
        pending_closes, next_capacity * sizeof(*next));
    if (next != NULL) {
        pending_closes = next;
        pending_close_capacity = next_capacity;
    }
}

static void update_frame_title(int index)
{
    if (index < 0 || index >= client_count) return;
    char *name = NULL;
    if (XFetchName(display, clients[index].client, &name) && name != NULL) {
        XStoreName(display, clients[index].frame, name);
        XFree(name);
    }
}

static void terminal_keyboard_command(const char *action)
{
    pid_t child = fork();
    if (child == 0) {
        execl("/usr/bin/fieldm-keyboard", "fieldm-keyboard",
              "terminal", action, (char *)NULL);
        _exit(127);
    }
    if (child < 0) {
        perror("Could not run fieldm-keyboard");
    }
}

static void browser_keyboard_command(const char *action)
{
    pid_t child = fork();
    if (child == 0) {
        execl("/usr/bin/fieldm-keyboard", "fieldm-keyboard",
              "manual", action, (char *)NULL);
        _exit(127);
    }
    if (child < 0) {
        perror("Could not run browser keyboard");
    }
}

static void raise_keyboard_window(void)
{
    if (keyboard_window != None) {
        XRaiseWindow(display, keyboard_window);
    }
}

static void publish_workarea(void)
{
    int height = screen_layout.height;
    if (keyboard_window != None && focused >= 0 &&
        focused < client_count &&
        clients[focused].terminal) {
        height -= keyboard_height;
    }
    if (height < 1) {
        height = 1;
    }
    unsigned long workarea[4] = {
        (unsigned long)screen_layout.x,
        (unsigned long)screen_layout.y,
        (unsigned long)screen_layout.width,
        (unsigned long)height
    };
    XChangeProperty(display, root,
                    XInternAtom(display, "_NET_WORKAREA", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)workarea, 4);
    XFlush(display);
}

static int text_matches_terminal(const char *text)
{
    static const char *const names[] = {
        "terminal", "xterm", "uxterm", "foot", "kitty", "alacritty",
        "st", "urxvt", "wezterm", "konsole", "applications"
    };
    if (text == NULL) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const char *needle = names[i];
        for (const char *cursor = text; *cursor != '\0'; ++cursor) {
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
        }
    }
    return 0;
}

static int client_is_terminal(Window window)
{
    XClassHint hint = {0};
    int terminal = 0;
    if (XGetClassHint(display, window, &hint)) {
        terminal = text_matches_terminal(hint.res_name) ||
                   text_matches_terminal(hint.res_class);
        XFree(hint.res_name);
        XFree(hint.res_class);
    }
    char *name = NULL;
    if (!terminal && XFetchName(display, window, &name) && name != NULL) {
        terminal = text_matches_terminal(name);
    }
    if (name != NULL) {
        XFree(name);
    }
    return terminal;
}

static int client_is_browser(Window window)
{
    static const char *const names[] = {
        "netsurf", "netsurf-gtk", "netsurf-bin", "chromium",
        "google-chrome", "firefox", "librewolf", "epiphany"
    };
    XClassHint hint = {0};
    int browser = 0;
    if (XGetClassHint(display, window, &hint)) {
        char *values[] = {hint.res_name, hint.res_class};
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            if (values[i] == NULL) continue;
            for (size_t j = 0;
                 j < sizeof(names) / sizeof(names[0]); ++j) {
                const char *needle = names[j];
                for (const char *cursor = values[i]; *cursor != '\0'; ++cursor) {
                    const char *left = cursor;
                    const char *right = needle;
                    while (*left != '\0' && *right != '\0' &&
                           tolower((unsigned char)*left) ==
                               tolower((unsigned char)*right)) {
                        ++left;
                        ++right;
                    }
                    if (*right == '\0') {
                        browser = 1;
                        break;
                    }
                }
                if (browser) break;
            }
            if (browser) break;
        }
        XFree(hint.res_name);
        XFree(hint.res_class);
    }
    if (!browser) {
        char *name = NULL;
        if (XFetchName(display, window, &name) && name != NULL) {
            for (size_t i = 0;
                 i < sizeof(names) / sizeof(names[0]); ++i) {
                char lower_name[256];
                size_t length = strlen(name);
                if (length >= sizeof(lower_name)) length = sizeof(lower_name)-1;
                for (size_t j = 0; j < length; ++j) {
                    lower_name[j] = (char)tolower((unsigned char)name[j]);
                }
                lower_name[length] = '\0';
                char lower_name_match[64];
                size_t match_length = strlen(names[i]);
                if (match_length >= sizeof(lower_name_match)) continue;
                for (size_t j = 0; j < match_length; ++j) {
                    lower_name_match[j] =
                        (char)tolower((unsigned char)names[i][j]);
                }
                lower_name_match[match_length] = '\0';
                if (strstr(lower_name, lower_name_match) != NULL) {
                    browser = 1;
                    break;
                }
            }
            XFree(name);
        }
    }
    return browser;
}

static int text_matches_keyboard(const char *text)
{
    static const char *const names[] = {"svkbd", "fieldm-kbd"};
    if (text == NULL) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const char *needle = names[i];
        for (const char *cursor = text; *cursor != '\0'; ++cursor) {
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
        }
    }
    return 0;
}

static int client_is_keyboard(Window window)
{
    XClassHint hint = {0};
    int keyboard = 0;
    if (XGetClassHint(display, window, &hint)) {
        keyboard = text_matches_keyboard(hint.res_name) ||
                   text_matches_keyboard(hint.res_class);
        XFree(hint.res_name);
        XFree(hint.res_class);
    }
    char *name = NULL;
    if (!keyboard && XFetchName(display, window, &name) && name != NULL) {
        keyboard = text_matches_keyboard(name);
    }
    if (name != NULL) {
        XFree(name);
    }
    return keyboard;
}

static void place_keyboard_window(Window window, int requested_height)
{
    XWindowAttributes attributes;
    if (requested_height <= 0) {
        if (!XGetWindowAttributes(display, window, &attributes)) return;
        requested_height = attributes.height;
    }
    if (requested_height < 1) return;
    int previous_height = keyboard_height;
    Window previous_window = keyboard_window;
    keyboard_height = requested_height;
    int y = screen_layout.y + screen_layout.height - keyboard_height;
    keyboard_window = window;
    XMoveResizeWindow(display, window, screen_layout.x, y,
                      (unsigned int)screen_layout.width,
                      (unsigned int)keyboard_height);
    XSetWindowBorderWidth(display, window, 0);
    XSelectInput(display, window, StructureNotifyMask | PropertyChangeMask);
    unsigned long strut[4] = {0, 0, 0, (unsigned long)keyboard_height};
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)strut, 4);
    unsigned long partial[12] = {
        0, 0, 0, (unsigned long)keyboard_height,
        0, 0, 0, 0, 0,
        (unsigned long)(screen_layout.width - 1), 0, 0
    };
    XChangeProperty(display, window,
                    XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)partial, 12);
    XRaiseWindow(display, window);
    if (taskbar_enabled && taskbar_window != None) {
        XMapRaised(display, taskbar_window);
    }
    if ((previous_window != window || previous_height != keyboard_height) &&
        focused >= 0) {
        maximize_client(focused, clients[focused].fullscreen);
    }
    publish_workarea();
    XFlush(display);
}

static void sync_terminal_state(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    int terminal = client_is_terminal(clients[index].client);
    int browser = client_is_browser(clients[index].client);
    if (terminal == clients[index].terminal &&
        browser == clients[index].browser) {
        return;
    }
    clients[index].terminal = terminal;
    clients[index].browser = browser;
    if (terminal) {
        if (terminal_count++ == 0) {
            terminal_keyboard_command("on");
        }
    } else if (terminal_count > 0 && --terminal_count == 0) {
        terminal_keyboard_command("off");
    }
    if (focused == index) {
        maximize_client(index, clients[index].fullscreen);
    }
}

static unsigned long named_color(const char *name)
{
    XColor exact;
    XColor allocated;
    if (!XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                          name, &allocated, &exact)) {
        return BlackPixel(display, DefaultScreen(display));
    }
    return allocated.pixel;
}

static void draw_round_rect(Drawable drawable, int x, int y, int width,
                            int height, int radius, unsigned long color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    if (radius * 2 > width) {
        radius = width / 2;
    }
    if (radius * 2 > height) {
        radius = height / 2;
    }
    XSetForeground(display, gc, color);
    XFillRectangle(display, drawable, gc, x + radius, y,
                   (unsigned int)(width - radius * 2),
                   (unsigned int)height);
    XFillRectangle(display, drawable, gc, x, y + radius,
                   (unsigned int)width,
                   (unsigned int)(height - radius * 2));
    XFillArc(display, drawable, gc, x, y, (unsigned int)(radius * 2),
             (unsigned int)(radius * 2), 0, 360 * 64);
    XFillArc(display, drawable, gc, x + width - radius * 2, y,
             (unsigned int)(radius * 2), (unsigned int)(radius * 2),
             0, 360 * 64);
    XFillArc(display, drawable, gc, x, y + height - radius * 2,
             (unsigned int)(radius * 2), (unsigned int)(radius * 2),
             0, 360 * 64);
    XFillArc(display, drawable, gc, x + width - radius * 2,
             y + height - radius * 2, (unsigned int)(radius * 2),
             (unsigned int)(radius * 2), 0, 360 * 64);
}

static void draw_text(Drawable drawable, const char *text, int x, int baseline,
                      const XftColor *color, int max_width)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    char clipped[512];
    size_t length = strlen(text);
    if (length >= sizeof(clipped)) {
        length = sizeof(clipped) - 1;
    }
    memcpy(clipped, text, length);
    clipped[length] = '\0';
    if (ui_font == NULL) {
        if (core_font != NULL) {
            XSetForeground(display, gc, color->pixel);
            XDrawString(display, drawable, gc, x, baseline, clipped,
                        (int)length);
        }
        return;
    }
    XGlyphInfo extents;
    XftTextExtentsUtf8(display, ui_font, (const FcChar8 *)clipped,
                       (int)length, &extents);
    while (length > 1 && extents.xOff > max_width) {
        --length;
        while (length > 0 &&
               (((unsigned char)clipped[length] & 0xc0U) == 0x80U)) {
            --length;
        }
        clipped[length] = '\0';
        XftTextExtentsUtf8(display, ui_font, (const FcChar8 *)clipped,
                           (int)length, &extents);
    }
    XftDraw *draw = XftDrawCreate(display, drawable,
                                  DefaultVisual(display, DefaultScreen(display)),
                                  DefaultColormap(display, DefaultScreen(display)));
    if (draw != NULL) {
        XftDrawStringUtf8(draw, color, ui_font, x, baseline,
                          (const FcChar8 *)clipped, (int)length);
        XftDrawDestroy(draw);
    }
}

static void allocate_text_color(const char *name, XftColor *color)
{
    XColor exact;
    XColor allocated;
    if (!XAllocNamedColor(display,
                          DefaultColormap(display, DefaultScreen(display)),
                          name, &allocated, &exact)) {
        memset(color, 0, sizeof(*color));
        color->color.red = 0xffff;
        color->color.green = 0xffff;
        color->color.blue = 0xffff;
        color->color.alpha = 0xffff;
        return;
    }
    color->pixel = allocated.pixel;
    color->color.red = allocated.red;
    color->color.green = allocated.green;
    color->color.blue = allocated.blue;
    color->color.alpha = 0xffff;
}

static void send_launcher_command(void)
{
    Atom owner_atom = XInternAtom(display, "_FIELD_DESKTOP_COMMAND_OWNER", False);
    Atom command_atom = XInternAtom(display, "_FIELD_DESKTOP_COMMAND", False);
    Window owner = XGetSelectionOwner(display, owner_atom);
    if (owner == None) {
        return;
    }
    XEvent message;
    memset(&message, 0, sizeof(message));
    message.xclient.type = ClientMessage;
    message.xclient.window = owner;
    message.xclient.message_type = command_atom;
    message.xclient.format = 32;
    message.xclient.data.l[0] = 1;
    XSendEvent(display, owner, False, NoEventMask, &message);
    XFlush(display);
}

static void send_desktop_command(long command)
{
    Atom owner_atom = XInternAtom(display, "_FIELD_DESKTOP_COMMAND_OWNER", False);
    Atom command_atom = XInternAtom(display, "_FIELD_DESKTOP_COMMAND", False);
    Window owner = XGetSelectionOwner(display, owner_atom);
    if (owner == None) {
        return;
    }
    XEvent message;
    memset(&message, 0, sizeof(message));
    message.xclient.type = ClientMessage;
    message.xclient.window = owner;
    message.xclient.message_type = command_atom;
    message.xclient.format = 32;
    message.xclient.data.l[0] = command;
    XSendEvent(display, owner, False, NoEventMask, &message);
    XFlush(display);
}

static void toggle_keyboard(void)
{
    pid_t child = fork();
    if (child == 0) {
        execl("/usr/bin/fieldm-keyboard", "fieldm-keyboard",
              "manual", "toggle", (char *)NULL);
        _exit(127);
    }
}

static int find_client(Window window)
{
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].client == window || clients[i].frame == window) {
            return i;
        }
    }
    return -1;
}

static int is_desktop_surface(Window window)
{
    Atom window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP",
                                    False);
    Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom skip_taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR",
                                    False);
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *value = NULL;
    int is_desktop = 0;
    if (XGetWindowProperty(display, window, window_type, 0, 16, False, XA_ATOM,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &value) == Success &&
        value != NULL && actual_type == XA_ATOM && actual_format == 32) {
        Atom *types = (Atom *)value;
        for (unsigned long i = 0; i < item_count; ++i) {
            if (types[i] == desktop_type) {
                is_desktop = 1;
                break;
            }
        }
    }
    if (value != NULL) {
        XFree(value);
        value = NULL;
    }
    if (!is_desktop &&
        XGetWindowProperty(display, window, state_atom, 0, 16, False, XA_ATOM,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &value) == Success &&
        value != NULL && actual_type == XA_ATOM && actual_format == 32) {
        Atom *states = (Atom *)value;
        for (unsigned long i = 0; i < item_count; ++i) {
            if (states[i] == skip_taskbar) {
                is_desktop = 1;
                break;
            }
        }
    }
    if (value != NULL) {
        XFree(value);
        value = NULL;
    }
    return is_desktop;
}

static int client_has_state(Window client, const char *state_name)
{
    Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom state = XInternAtom(display, state_name, False);
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *value = NULL;
    int found = 0;
    if (XGetWindowProperty(display, client, state_atom, 0, 32, False, XA_ATOM,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &value) == Success &&
        value != NULL && actual_type == XA_ATOM && actual_format == 32) {
        Atom *states = (Atom *)value;
        for (unsigned long i = 0; i < item_count; ++i) {
            if (states[i] == state) {
                found = 1;
                break;
            }
        }
    }
    if (value != NULL) {
        XFree(value);
    }
    return found;
}

static void draw_frame(int index)
{
    ManagedWindow *managed = &clients[index];
    int title = title_height(managed);
    int border = border_width(managed);
    XSetForeground(display, gc, frame_color);
    XFillRectangle(display, managed->frame, gc, 0, 0,
                   (unsigned int)(managed->width + border),
                   (unsigned int)(managed->height + title + border));
    if (title > 0) {
        XSetForeground(display, gc,
                       index == focused ? active_color : panel_color);
        XFillRectangle(display, managed->frame, gc, 0, 0,
                       (unsigned int)(managed->width + border),
                       (unsigned int)title);

        char *name = NULL;
        if (!XFetchName(display, managed->client, &name) || name == NULL) {
            name = strdup("Application");
        }
        if (name != NULL) {
            XSetForeground(display, gc, accent_xft.pixel);
            XFillArc(display, managed->frame, gc, 18, 20, 24, 24, 0, 360 * 64);
            draw_text(managed->frame, name, 54, 41, &text_xft,
                      managed->width - 130);
            XFree(name);
        }

        XSetForeground(display, gc, outline_color);
        XDrawLine(display, managed->frame, gc, 0, title - 1,
                  managed->width + border, title - 1);
    }
    if (border > 0) {
        XSetForeground(display, gc,
                       index == focused ? active_color : panel_color);
        XFillRectangle(display, managed->frame, gc, managed->width, title,
                       (unsigned int)border,
                       (unsigned int)(managed->height + border));
        XFillRectangle(display, managed->frame, gc, 0,
                       title + managed->height,
                       (unsigned int)(managed->width + border),
                       (unsigned int)border);
        XSetForeground(display, gc, muted_xft.pixel);
        for (int offset = 3; offset <= 7; offset += 2) {
            XDrawLine(display, managed->frame, gc,
                      managed->width + border - offset,
                      title + managed->height + border - 1,
                      managed->width + border - 1,
                      title + managed->height + border - offset);
        }
    }
}

static void focus_client(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    int previous = focused;
    if (previous >= 0 && previous < client_count) {
        draw_frame(previous);
    }
    focused = index;
    if (previous != index && previous >= 0 && previous < client_count) {
        if (clients[previous].browser) {
            browser_keyboard_command("off");
        }
        maximize_client(previous, clients[previous].fullscreen);
    }
    maximize_client(index, clients[index].fullscreen);
    XRaiseWindow(display, clients[index].frame);
    XSetInputFocus(display, clients[index].client, RevertToPointerRoot,
                   CurrentTime);
    draw_frame(index);
    draw_taskbar();
    if (taskbar_enabled && !clients[index].fullscreen &&
        taskbar_window != None) {
        XMapRaised(display, taskbar_window);
    } else if (clients[index].fullscreen && taskbar_window != None) {
        XUnmapWindow(display, taskbar_window);
    }
    raise_keyboard_window();
    publish_workarea();
}

static void maximize_client(int index, int fullscreen)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    ManagedWindow *managed = &clients[index];
    managed->fullscreen = fullscreen;
    int title = title_height(managed);
    int border = border_width(managed);
    if (managed->dialog && !fullscreen) {
        int max_width = screen_layout.width - 32;
        int max_height = screen_layout.height - 32 -
            (keyboard_window != None && focused == index ? keyboard_height : 0);
        if (managed->width > max_width) managed->width = max_width;
        if (managed->height > max_height) managed->height = max_height;
        managed->x = screen_layout.x + (screen_layout.width - managed->width) / 2;
        managed->y = screen_layout.y + (screen_layout.height - managed->height) / 2;
        arrange_client(index);
        raise_keyboard_window();
        return;
    }
    Atom states[3] = {
        XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False),
        XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False),
        XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False)
    };
    XChangeProperty(display, managed->client,
                    XInternAtom(display, "_NET_WM_STATE", False),
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, fullscreen ? 3 : 2);
    managed->x = screen_layout.x;
    managed->y = screen_layout.y;
    managed->width = screen_layout.width - border;
    managed->height = screen_layout.height - title - border -
        (keyboard_window != None && focused == index
             ? keyboard_height : 0);
    arrange_client(index);
    if (taskbar_window != None) {
        if (taskbar_enabled && !fullscreen) {
            XMapRaised(display, taskbar_window);
        } else {
            XUnmapWindow(display, taskbar_window);
        }
    }
    raise_keyboard_window();
}

static void clamp_managed_geometry(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    ManagedWindow *managed = &clients[index];
    int title = title_height(managed);
    int border = border_width(managed);
    int usable_width = screen_layout.width - border;
    int usable_height = screen_layout.height - title - border;
    if (keyboard_window != None && focused == index) {
        usable_height -= keyboard_height;
    }
    if (usable_width < MIN_WIDTH) {
        usable_width = MIN_WIDTH;
    }
    if (usable_height < MIN_HEIGHT) {
        usable_height = MIN_HEIGHT;
    }
    if (managed->width < 1) {
        managed->width = 1;
    }
    if (managed->width > usable_width) {
        managed->width = usable_width;
    }
    if (managed->height < 1) {
        managed->height = 1;
    }
    if (managed->height > usable_height) {
        managed->height = usable_height;
    }
    int max_x = screen_layout.x + screen_layout.width -
                managed->width - border;
    int max_y = screen_layout.y + usable_height - managed->height;
    if (managed->x < screen_layout.x) {
        managed->x = screen_layout.x;
    }
    if (managed->x > max_x) {
        managed->x = max_x;
    }
    if (managed->y < screen_layout.y) {
        managed->y = screen_layout.y;
    }
    if (managed->y > max_y) {
        managed->y = max_y;
    }
}

static void arrange_client(int index)
{
    ManagedWindow *managed = &clients[index];
    clamp_managed_geometry(index);
    int title = title_height(managed);
    int border = border_width(managed);
    XMoveResizeWindow(display, managed->frame, managed->x, managed->y,
                      (unsigned int)(managed->width + border),
                      (unsigned int)(managed->height + title + border));
    XMoveResizeWindow(display, managed->client, 0, title,
                      (unsigned int)managed->width,
                      (unsigned int)managed->height);
    draw_frame(index);
}

static void manage_window(Window client)
{
    if (find_client(client) >= 0) {
        return;
    }

    if (is_desktop_surface(client)) {
        XMapWindow(display, client);
        XLowerWindow(display, client);
        return;
    }
    if (client_is_keyboard(client)) {
        XMapWindow(display, client);
        place_keyboard_window(client, -1);
        return;
    }
    if (!ensure_client_capacity()) {
        fprintf(stderr,
                "simple-wm: rejecting mapped client 0x%lx after allocation failure\n",
                client);
        XKillClient(display, client);
        return;
    }

    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, client, &attributes) ||
        attributes.override_redirect) {
        XMapWindow(display, client);
        return;
    }

    int index = client_count++;
    ManagedWindow *managed = &clients[index];
    managed->client = client;
    managed->terminal = 0;
    managed->browser = 0;
    managed->fullscreen = client_has_state(
        client, "_NET_WM_STATE_FULLSCREEN");
    Window transient_for = None;
    managed->dialog = XGetTransientForHint(display, client, &transient_for) &&
                      transient_for != None;
    managed->x = screen_layout.x;
    managed->y = screen_layout.y;
    managed->width = managed->dialog && attributes.width > 0
        ? attributes.width : screen_layout.width - border_width(managed);
    managed->height = managed->dialog && attributes.height > 0
        ? attributes.height
        : screen_layout.height - title_height(managed) -
              border_width(managed);
    if (managed->dialog) {
        int max_width = screen_layout.width - 32;
        int max_height = screen_layout.height - 32;
        if (managed->width > max_width) managed->width = max_width;
        if (managed->height > max_height) managed->height = max_height;
        managed->x = screen_layout.x + (screen_layout.width - managed->width) / 2;
        managed->y = screen_layout.y + (screen_layout.height - managed->height) / 2;
    }
    if (managed->width < MIN_WIDTH) {
        managed->width = MIN_WIDTH;
    }
    if (managed->height < MIN_HEIGHT) {
        managed->height = MIN_HEIGHT;
    }

    int title = title_height(managed);
    int border = border_width(managed);
    XSetWindowAttributes frame_attributes;
    frame_attributes.override_redirect = True;
    frame_attributes.background_pixel = frame_color;
    frame_attributes.event_mask = ExposureMask | ButtonPressMask;
    managed->frame = XCreateWindow(display, root, managed->x, managed->y,
                                   (unsigned int)(managed->width + border),
                                   (unsigned int)(managed->height + title +
                                                  border),
                                   1, CopyFromParent, InputOutput, CopyFromParent,
                                   CWOverrideRedirect | CWBackPixel | CWEventMask,
                                   &frame_attributes);
    if (managed->frame == None) {
        fprintf(stderr,
                "simple-wm: rejecting client 0x%lx because frame creation failed\n",
                client);
        XKillClient(display, client);
        --client_count;
        shrink_client_capacity();
        return;
    }

    XAddToSaveSet(display, client);
    XSelectInput(display, client, StructureNotifyMask | PropertyChangeMask);
    XGrabButton(display, AnyButton, AnyModifier, client, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeSync, GrabModeAsync, None, None);
    XSetWindowBorderWidth(display, client, 0);
    XReparentWindow(display, client, managed->frame, 0, title);
    update_frame_title(index);
    XMapWindow(display, client);
    XMapWindow(display, managed->frame);
    maximize_client(index, managed->fullscreen);
    sync_terminal_state(index);
    focus_client(index);
    draw_taskbar();
    if (switcher_window != None) {
        show_switcher();
    }
}

static void forget_client(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    Window frame = clients[index].frame;
    int was_focused = focused == index;
    if (clients[index].terminal && --terminal_count == 0) {
        terminal_keyboard_command("off");
    }
    if (clients[index].browser) {
        browser_keyboard_command("off");
    }
    XDestroyWindow(display, frame);
    for (int i = index; i + 1 < client_count; ++i) {
        clients[i] = clients[i + 1];
    }
    --client_count;
    if (was_focused) {
        focused = index > 0 ? index - 1 : 0;
    } else if (focused > index) {
        --focused;
    }
    if (client_count > 0) {
        focus_client(focused >= 0 && focused < client_count
                         ? focused : client_count - 1);
    } else {
        focused = -1;
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
    }
    shrink_client_capacity();
    draw_taskbar();
}

static void unmanage_client(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    Window client = clients[index].client;
    int x = clients[index].x;
    int y = clients[index].y;
    XSelectInput(display, client, NoEventMask);
    XReparentWindow(display, client, root, x, y);
    XRemoveFromSaveSet(display, client);
    forget_client(index);
}

static void cancel_pending_close(Window client)
{
    for (size_t i = 0; i < pending_close_count;) {
        if (pending_closes[i].client == client) {
            for (size_t j = i + 1; j < pending_close_count; ++j) {
                pending_closes[j - 1] = pending_closes[j];
            }
            --pending_close_count;
            pending_closes[pending_close_count].client = None;
            pending_closes[pending_close_count].deadline = 0;
        } else {
            ++i;
        }
    }
    shrink_pending_close_capacity();
}

static void schedule_pending_close(Window client)
{
    for (size_t i = 0; i < pending_close_count; ++i) {
        if (pending_closes[i].client == client) {
            pending_closes[i].deadline = monotonic_ms() + 3000;
            return;
        }
    }
    if (!ensure_pending_close_capacity()) {
        XKillClient(display, client);
        return;
    }
    pending_closes[pending_close_count].client = client;
    pending_closes[pending_close_count].deadline = monotonic_ms() + 3000;
    ++pending_close_count;
}

static int pending_close_timeout(void)
{
    unsigned long long now = monotonic_ms();
    unsigned long long earliest = 0;
    for (size_t i = 0; i < pending_close_count; ++i) {
        if (pending_closes[i].client == None) continue;
        if (earliest == 0 || pending_closes[i].deadline < earliest) {
            earliest = pending_closes[i].deadline;
        }
    }
    if (earliest == 0) return -1;
    if (earliest <= now) return 0;
    unsigned long long remaining = earliest - now;
    return remaining > 2147483ULL ? 2147483 : (int)remaining;
}

static void expire_pending_closes(void)
{
    unsigned long long now = monotonic_ms();
    for (size_t i = 0; i < pending_close_count;) {
        if (pending_closes[i].client == None ||
            pending_closes[i].deadline > now) {
            ++i;
            continue;
        }
        Window client = pending_closes[i].client;
        int index = find_client(client);
        XWindowAttributes attributes;
        if (index >= 0 && clients[index].client == client &&
            XGetWindowAttributes(display, client, &attributes)) {
            fprintf(stderr, "simple-wm: WM_DELETE_WINDOW timed out; killing client 0x%lx\n",
                    client);
            XKillClient(display, client);
        }
        for (size_t j = i + 1; j < pending_close_count; ++j) {
            pending_closes[j - 1] = pending_closes[j];
        }
        --pending_close_count;
        pending_closes[pending_close_count].client = None;
        pending_closes[pending_close_count].deadline = 0;
    }
    shrink_pending_close_capacity();
}

static void prune_missing_clients(void)
{
    for (int i = 0; i < client_count;) {
        XWindowAttributes attributes;
        if (XGetWindowAttributes(display, clients[i].client, &attributes)) {
            ++i;
            continue;
        }
        fprintf(stderr,
                "simple-wm: pruning missing client 0x%lx\n",
                clients[i].client);
        cancel_pending_close(clients[i].client);
        forget_client(i);
    }
}

static void close_client(int index)
{
    if (index < 0 || index >= client_count) return;

    Atom protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    Atom *supported = NULL;
    int count = 0;
    Window client = clients[index].client;
    int supports_delete = XGetWMProtocols(display, client,
                                          &supported, &count);
    for (int i = 0; supports_delete && i < count; ++i) {
        if (supported[i] == delete_window) {
            XEvent message;
            memset(&message, 0, sizeof(message));
            message.xclient.type = ClientMessage;
            message.xclient.window = client;
            message.xclient.message_type = protocols;
            message.xclient.format = 32;
            message.xclient.data.l[0] = (long)delete_window;
            message.xclient.data.l[1] = CurrentTime;
            XSendEvent(display, client, False, NoEventMask, &message);
            if (supported != NULL) {
                XFree(supported);
            }
            XFlush(display);
            schedule_pending_close(client);
            return;
        }
    }
    if (supported != NULL) {
        XFree(supported);
    }
    XKillClient(display, client);
}

static void close_focused(void)
{
    close_client(focused);
}

static void close_focused_immediately(void)
{
    if (focused < 0 || focused >= client_count) {
        browser_keyboard_command("off");
        for (int i = 0; i < client_count; ++i) {
            Window client = clients[i].client;
            cancel_pending_close(client);
            XKillClient(display, client);
        }
        XFlush(display);
        return;
    }
    Window client = clients[focused].client;
    cancel_pending_close(client);
    browser_keyboard_command("off");
    XKillClient(display, client);
    XFlush(display);
}

static void send_key_to_focused(KeySym keysym)
{
    if (focused < 0 || focused >= client_count) {
        return;
    }
    Window focus = clients[focused].client;
    KeyCode keycode = XKeysymToKeycode(display, keysym);
    XKeyEvent event;
    memset(&event, 0, sizeof(event));
    event.display = display;
    event.window = focus;
    event.root = root;
    event.subwindow = None;
    event.time = CurrentTime;
    event.same_screen = True;
    event.keycode = keycode;
    event.type = KeyPress;
    XSendEvent(display, focus, True, KeyPressMask, (XEvent *)&event);
    event.type = KeyRelease;
    XSendEvent(display, focus, True, KeyReleaseMask, (XEvent *)&event);
    XFlush(display);
}

static void send_alt_left_to_focused(void)
{
    if (focused < 0 || focused >= client_count) {
        return;
    }
    Window focus = clients[focused].client;
    XKeyEvent event;
    memset(&event, 0, sizeof(event));
    event.display = display;
    event.window = focus;
    event.root = root;
    event.subwindow = None;
    event.time = CurrentTime;
    event.same_screen = True;
    event.state = Mod1Mask;
    event.keycode = XKeysymToKeycode(display, XK_Alt_L);
    event.type = KeyPress;
    XSendEvent(display, focus, True, KeyPressMask, (XEvent *)&event);
    event.keycode = XKeysymToKeycode(display, XK_Left);
    event.type = KeyPress;
    XSendEvent(display, focus, True, KeyPressMask, (XEvent *)&event);
    event.type = KeyRelease;
    XSendEvent(display, focus, True, KeyReleaseMask, (XEvent *)&event);
    event.keycode = XKeysymToKeycode(display, XK_Alt_L);
    XSendEvent(display, focus, True, KeyReleaseMask, (XEvent *)&event);
    XFlush(display);
}

static void draw_taskbar_monitor(void)
{
    Window window = taskbar_window;
    int width = screen_layout.width;
    if (window == None || !taskbar_enabled) {
        return;
    }
    XSetForeground(display, gc, named_color(FIELD_M_BACKGROUND));
    XFillRectangle(display, window, gc, 0, 0,
                   (unsigned int)width, TASKBAR_HEIGHT);
    XSetForeground(display, gc, outline_color);
    XDrawLine(display, window, gc, 0, 0, width, 0);
    int section_width = width / 3;
    static const char *labels[] = {"Apps", "Keyboard", "Windows"};
    for (int i = 0; i < 3; ++i) {
        int x = i * section_width;
        draw_round_rect(window, x + 8, 12, section_width - 16, 64, 4,
                        button_color);
        if (i == 0) {
            XSetForeground(display, gc, accent_xft.pixel);
            XDrawRectangle(display, window, gc, x + 8, 10,
                           (unsigned int)(section_width - 16), 48);
        }
        draw_text(window, labels[i], x + 8, 52, &text_xft,
                  section_width - 16);
    }
}

static void draw_taskbar(void)
{
    draw_taskbar_monitor();
}

static void set_taskbar_struts(void)
{
    if (taskbar_window == None) {
        return;
    }
    unsigned long strut[4] = {0, 0, 0,
                              taskbar_enabled ? TASKBAR_HEIGHT : 0};
    XChangeProperty(display, taskbar_window,
                    XInternAtom(display, "_NET_WM_STRUT", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)strut, 4);
    unsigned long partial[12] = {0, 0, 0,
                                 taskbar_enabled ? TASKBAR_HEIGHT : 0,
                                 0, 0, 0, 0,
                                 0, 0,
                                 screen_layout.x,
                                 screen_layout.x + screen_layout.width - 1};
    XChangeProperty(display, taskbar_window,
                    XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)partial, 12);
}

static void sync_taskbars(void)
{
    screen_layout = screen_layout_get(display);
    screen_layout.y += STATUS_HEIGHT;
    screen_layout.height -= STATUS_HEIGHT;
    if (taskbar_enabled) {
        screen_layout.height -= TASKBAR_HEIGHT;
    }
    unsigned long workarea[4] = {
        (unsigned long)screen_layout.x,
        (unsigned long)screen_layout.y,
        (unsigned long)screen_layout.width,
        (unsigned long)screen_layout.height
    };
    XChangeProperty(display, root,
                    XInternAtom(display, "_NET_WORKAREA", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)workarea, 4);
    publish_workarea();
    XSetWindowAttributes attributes;
    attributes.override_redirect = True;
    attributes.background_pixel = switcher_color;
    attributes.event_mask = ExposureMask | ButtonPressMask;
    if (TASKBAR_HEIGHT > 0) {
        if (taskbar_window == None) {
            taskbar_window = XCreateWindow(
                display, root, screen_layout.x,
                screen_layout.y + screen_layout.height,
                (unsigned int)screen_layout.width, TASKBAR_HEIGHT, 0,
                CopyFromParent, InputOutput, CopyFromParent,
                CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
        } else {
            XMoveResizeWindow(
                display, taskbar_window, screen_layout.x,
                screen_layout.y + screen_layout.height,
                (unsigned int)screen_layout.width, TASKBAR_HEIGHT);
        }
        if (taskbar_enabled) {
            XMapRaised(display, taskbar_window);
        } else {
            XUnmapWindow(display, taskbar_window);
        }
    } else {
        taskbar_window = None;
    }
    set_taskbar_struts();
    if (keyboard_window != None) {
        place_keyboard_window(keyboard_window, -1);
    }
    draw_taskbar();
}

static void relayout_for_screen_size(void)
{
    sync_taskbars();
    for (int i = 0; i < client_count; ++i) {
        ManagedWindow *managed = &clients[i];
        if (managed->dialog && !managed->fullscreen) {
            int max_width = screen_layout.width - 32;
            int max_height = screen_layout.height - 32 -
                (keyboard_window != None && focused == i
                     ? keyboard_height : 0);
            if (managed->width > max_width) managed->width = max_width;
            if (managed->height > max_height) managed->height = max_height;
            managed->x = screen_layout.x +
                         (screen_layout.width - managed->width) / 2;
            managed->y = screen_layout.y +
                         (screen_layout.height - managed->height) / 2;
            arrange_client(i);
            continue;
        }
        int max_width = screen_layout.width - border_width(managed);
        int available_height = screen_layout.height;
        if (keyboard_window != None && focused == i) {
            available_height -= keyboard_height;
        }
        int max_height = available_height - title_height(managed) -
                         border_width(managed);
        if (max_width >= MIN_WIDTH && managed->width > max_width) {
            managed->width = max_width;
        }
        if (max_height >= MIN_HEIGHT && managed->height > max_height) {
            managed->height = max_height;
        }
        int max_x = screen_layout.x + screen_layout.width - managed->width -
                    border_width(managed);
        int max_y = screen_layout.y + available_height - managed->height -
                    title_height(managed) - border_width(managed);
        if (managed->x > max_x) {
            managed->x = max_x > screen_layout.x ? max_x : screen_layout.x;
        }
        if (managed->y > max_y) {
            managed->y = max_y > screen_layout.y ? max_y : screen_layout.y;
        }
        if (managed->x < screen_layout.x) {
            managed->x = screen_layout.x;
        }
        if (managed->y < screen_layout.y) {
            managed->y = screen_layout.y;
        }
        arrange_client(i);
    }
    draw_taskbar();
    if (switcher_window != None) {
        show_switcher();
    }
}

static int taskbar_preference_from_root(void)
{
    Atom property = XInternAtom(display, "_FIELD_DESKTOP_FEATURES", False);
    Atom cardinal = XInternAtom(display, "CARDINAL", False);
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *value = NULL;
    int enabled = taskbar_enabled;
    if (XGetWindowProperty(display, root, property, 0, 1, False, cardinal,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &value) == Success &&
        value != NULL && actual_format == 32 && item_count == 1) {
        enabled = (((unsigned long *)value)[0] & 1UL) != 0;
    }
    if (value != NULL) {
        XFree(value);
    }
    return enabled;
}

static int taskbar_window_index(Window window)
{
    return window == taskbar_window ? 0 : -1;
}

static void draw_switcher(void)
{
    if (switcher_window == None) {
        return;
    }
    int visible = client_count < SWITCHER_ROWS ? client_count : SWITCHER_ROWS;
    int first = focused >= SWITCHER_ROWS ? focused - SWITCHER_ROWS + 1 : 0;
    int height = visible * SWITCHER_ROW_HEIGHT + 72;
    XSetForeground(display, gc, switcher_color);
    XFillRectangle(display, switcher_window, gc, 0, 0,
                   SWITCHER_WIDTH, (unsigned int)height);
    draw_text(switcher_window, "Window switcher", 22, 31, &text_xft, 220);
    draw_text(switcher_window, "Alt+Tab to cycle  |  Alt+Esc to close",
              22, 52, &muted_xft, SWITCHER_WIDTH - 44);

    for (int row = 0; row < visible; ++row) {
        int index = first + row;
        int y = 68 + row * SWITCHER_ROW_HEIGHT;
        if (index >= client_count) {
            break;
        }
        draw_round_rect(switcher_window, 12, y - 19,
                        SWITCHER_WIDTH - 24, SWITCHER_ROW_HEIGHT - 4, 8,
                        index == focused ? button_active_color : switcher_color);
        char *name = NULL;
        if (!XFetchName(display, clients[index].client, &name) || name == NULL) {
            name = strdup("Application");
        }
        if (name != NULL) {
            XSetForeground(display, gc,
                           index == focused ? accent_xft.pixel : muted_xft.pixel);
            XFillArc(display, switcher_window, gc, 23, y - 10, 12, 12,
                     0, 360 * 64);
            draw_text(switcher_window, name, 48, y, &text_xft,
                      SWITCHER_WIDTH - 124);
            XFree(name);
        }
        draw_round_rect(switcher_window, SWITCHER_WIDTH - 60, y - 25,
                        48, 48, 10, close_color);
        XSetForeground(display, gc, text_xft.pixel);
        XDrawLine(display, switcher_window, gc,
                  SWITCHER_WIDTH - 48, y - 13,
                  SWITCHER_WIDTH - 24, y + 11);
        XDrawLine(display, switcher_window, gc,
                  SWITCHER_WIDTH - 24, y - 13,
                  SWITCHER_WIDTH - 48, y + 11);
    }
}

static void close_switcher(void)
{
    if (switcher_window != None) {
        XDestroyWindow(display, switcher_window);
        switcher_window = None;
    }
}

static void show_switcher(void)
{
    if (client_count == 0) {
        close_switcher();
        return;
    }
    int visible = client_count < SWITCHER_ROWS ? client_count : SWITCHER_ROWS;
    int height = visible * SWITCHER_ROW_HEIGHT + 72;
    ScreenGeometry monitor = screen_layout;
    if (switcher_window == None) {
        XSetWindowAttributes attributes;
        attributes.override_redirect = True;
        attributes.background_pixel = switcher_color;
        attributes.event_mask = ExposureMask | ButtonPressMask;
        switcher_window = XCreateWindow(
            display, root,
            monitor.x + (monitor.width - SWITCHER_WIDTH) / 2,
            monitor.y + (monitor.height - height) / 2,
            SWITCHER_WIDTH, (unsigned int)height, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
        XMapWindow(display, switcher_window);
    }
    XMoveResizeWindow(display, switcher_window,
                      monitor.x + (monitor.width - SWITCHER_WIDTH) / 2,
                      monitor.y + (monitor.height - height) / 2,
                      SWITCHER_WIDTH, (unsigned int)height);
    XRaiseWindow(display, switcher_window);
    draw_switcher();
}

static int report_x_error(Display *connection, XErrorEvent *error)
{
    (void)connection;
    if (error->error_code == BadAccess) {
        fprintf(stderr, "simple-wm: another window manager owns this X display\n");
        startup_failed = 1;
    } else {
        fprintf(stderr, "simple-wm: X11 error %u\n", error->error_code);
    }
    return 0;
}

static void grab_key_with_locks(KeySym key, unsigned int modifiers)
{
    static const unsigned int lock_modifiers[] = {
        0, LockMask, Mod2Mask, LockMask | Mod2Mask
    };
    KeyCode keycode = XKeysymToKeycode(display, key);
    for (size_t i = 0; i < sizeof(lock_modifiers) / sizeof(lock_modifiers[0]);
         ++i) {
        XGrabKey(display, keycode, modifiers | lock_modifiers[i], root,
                 False, GrabModeAsync, GrabModeAsync);
    }
}

int main(int argc, char **argv)
{
    int ready_fd = -1;
    if (argc == 3 && strcmp(argv[1], "--ready-fd") == 0) {
        ready_fd = atoi(argv[2]);
    }
    signal(SIGCHLD, SIG_IGN);

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "simple-wm: could not connect to the X11 display\n");
        return 1;
    }
    XSetErrorHandler(report_x_error);
    int screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    Cursor pointer_cursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, root, pointer_cursor);
    XFreeCursor(display, pointer_cursor);
    frame_color = named_color(FIELD_M_BACKGROUND);
    active_color = named_color(FIELD_M_FOREGROUND);
    switcher_color = named_color(FIELD_M_BACKGROUND);
    panel_color = named_color(FIELD_M_BACKGROUND);
    button_color = named_color(FIELD_M_BACKGROUND);
    button_active_color = named_color(FIELD_M_FOREGROUND);
    outline_color = named_color(FIELD_M_FOREGROUND);
    close_color = named_color(FIELD_M_PRESSED);
    gc = XCreateGC(display, root, 0, NULL);
    ui_font = XftFontOpenName(display, screen, "DejaVu Serif:size=18");
    if (ui_font == NULL) {
        ui_font = XftFontOpenName(display, screen, "DejaVu Serif:size=18");
    }
    if (ui_font == NULL) {
        core_font = XLoadQueryFont(display, "fixed");
        if (core_font != NULL) {
            XSetFont(display, gc, core_font->fid);
        }
        fprintf(stderr, "simple-wm: using the X11 core font fallback\n");
    }
    allocate_text_color(FIELD_M_FOREGROUND, &text_xft);
    allocate_text_color(FIELD_M_FOREGROUND, &muted_xft);
    allocate_text_color(FIELD_M_FOREGROUND, &accent_xft);
    allocate_text_color(FIELD_M_BACKGROUND, &dark_xft);
    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask |
                 StructureNotifyMask | ButtonPressMask | KeyPressMask |
                 PropertyChangeMask);
    grab_key_with_locks(XK_F4, Mod1Mask);
    grab_key_with_locks(XK_Tab, Mod1Mask);
    grab_key_with_locks(XK_Tab, Mod1Mask | ShiftMask);
    grab_key_with_locks(XK_space, Mod1Mask);
    grab_key_with_locks(XK_Escape, Mod1Mask);
    XSync(display, False);
    if (ready_fd >= 0) {
        char ready = startup_failed ? '0' : '1';
        (void)write(ready_fd, &ready, 1);
        close(ready_fd);
    }
    if (startup_failed) {
        XCloseDisplay(display);
        return 1;
    }
    taskbar_enabled = taskbar_preference_from_root();
    sync_taskbars();

    Window root_return, parent_return, *existing_children = NULL;
    unsigned int existing_count = 0;
    if (XQueryTree(display, root, &root_return, &parent_return,
                   &existing_children, &existing_count)) {
        for (unsigned int i = 0; i < existing_count; ++i) {
            XWindowAttributes attributes;
            if (!XGetWindowAttributes(display, existing_children[i],
                                      &attributes) ||
                attributes.override_redirect ||
                attributes.map_state != IsViewable) {
                continue;
            }
            if (existing_children[i] == taskbar_window ||
                client_is_keyboard(existing_children[i])) {
                continue;
            }
            manage_window(existing_children[i]);
        }
        if (existing_children != NULL) {
            XFree(existing_children);
        }
    }

    XEvent event;
    Window moving_frame = None;
    int drag_mode = 0;
    int drag_root_x = 0;
    int drag_root_y = 0;
    int origin_x = 0;
    int origin_y = 0;
    int origin_width = 0;
    int origin_height = 0;

    for (;;) {
        if (XPending(display) == 0) {
            struct pollfd connection = {
                ConnectionNumber(display), POLLIN, 0
            };
            int ready = poll(&connection, 1, pending_close_timeout());
            if (ready < 0 && errno != EINTR) {
                fprintf(stderr, "simple-wm: X11 poll failed: %s\n",
                        strerror(errno));
            }
        }
        expire_pending_closes();
        prune_missing_clients();
        if (XPending(display) == 0) {
            continue;
        }
        XNextEvent(display, &event);
        if (event.type == ConfigureNotify &&
            event.xconfigure.window == root) {
            relayout_for_screen_size();
        } else if (event.type == PropertyNotify &&
                   event.xproperty.window == root &&
                   event.xproperty.atom ==
                       XInternAtom(display, "_FIELD_DESKTOP_FEATURES", False)) {
            int enabled = taskbar_preference_from_root();
            if (enabled != taskbar_enabled) {
                taskbar_enabled = enabled;
                relayout_for_screen_size();
            }
        } else if (event.type == MapRequest) {
            manage_window(event.xmaprequest.window);
        } else if (event.type == MapNotify &&
                   event.xmap.window != taskbar_window &&
                   client_is_keyboard(event.xmap.window)) {
            place_keyboard_window(event.xmap.window, -1);
        } else if (event.type == ConfigureRequest) {
            int index = find_client(event.xconfigurerequest.window);
            if (index < 0) {
                if (client_is_keyboard(event.xconfigurerequest.window)) {
                    place_keyboard_window(event.xconfigurerequest.window,
                                          (event.xconfigurerequest.value_mask &
                                           CWHeight) != 0
                                              ? event.xconfigurerequest.height
                                              : -1);
                } else {
                    XWindowChanges changes;
                    changes.x = event.xconfigurerequest.x;
                    changes.y = event.xconfigurerequest.y;
                    changes.width = event.xconfigurerequest.width;
                    changes.height = event.xconfigurerequest.height;
                    changes.border_width =
                        event.xconfigurerequest.border_width;
                    changes.sibling = event.xconfigurerequest.above;
                    changes.stack_mode = event.xconfigurerequest.detail;
                    XConfigureWindow(
                        display, event.xconfigurerequest.window,
                        event.xconfigurerequest.value_mask, &changes);
                }
            } else {
                ManagedWindow *managed = &clients[index];
                if (event.xconfigurerequest.value_mask & CWWidth) {
                    managed->width = event.xconfigurerequest.width < MIN_WIDTH
                        ? MIN_WIDTH : event.xconfigurerequest.width;
                }
                if (event.xconfigurerequest.value_mask & CWHeight) {
                    managed->height = event.xconfigurerequest.height < MIN_HEIGHT
                        ? MIN_HEIGHT : event.xconfigurerequest.height;
                }
                arrange_client(index);
            }
        } else if (event.type == ClientMessage) {
            Atom desktop_command =
                XInternAtom(display, "_FIELD_DESKTOP_COMMAND", False);
            if (event.xclient.message_type == desktop_command) {
                long command = event.xclient.data.l[0];
                if (command == 3) {
                    close_switcher();
                    for (int i = 0; i < client_count; ++i) {
                        XLowerWindow(display, clients[i].frame);
                    }
                    focused = -1;
                    XSetInputFocus(display, root, RevertToPointerRoot,
                                   CurrentTime);
                    draw_taskbar();
                    publish_workarea();
                } else if (command == 4) {
                    browser_keyboard_command("off");
                    if (switcher_window != None) {
                        close_switcher();
                    } else {
                        show_switcher();
                    }
                } else if (command == 5) {
                    send_launcher_command();
                } else if (command == 7) {
                    close_switcher();
                    close_focused_immediately();
                    send_desktop_command(3);
                } else if (command == 8) {
                    if (focused < 0 || focused >= client_count) {
                        send_desktop_command(3);
                        continue;
                    }
                    unsigned long long now = monotonic_ms();
                    int browser = clients[focused].browser;
                    char *name = NULL;
                    int no_history = 0;
                    if (browser &&
                        XFetchName(display, clients[focused].client, &name) &&
                        name != NULL) {
                        no_history = strstr(name, "about:blank") != NULL;
                        XFree(name);
                    }
                    if (browser && no_history) {
                        back_pressed_at = 0;
                        close_focused_immediately();
                        send_desktop_command(3);
                        continue;
                    }
                    if (back_pressed_at != 0 &&
                        now - back_pressed_at < 1000) {
                        back_pressed_at = 0;
                        close_focused_immediately();
                        send_desktop_command(3);
                    } else {
                        back_pressed_at = now;
                        if (clients[focused].browser) {
                            send_alt_left_to_focused();
                        } else {
                            send_key_to_focused(XK_Escape);
                        }
                    }
                }
                continue;
            }
            Atom active_window_atom =
                XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
            int active_index = find_client(event.xclient.window);
            if (event.xclient.message_type == active_window_atom &&
                event.xclient.format == 32 && active_index >= 0) {
                focus_client(active_index);
                continue;
            }
            Atom close_window_atom =
                XInternAtom(display, "_NET_CLOSE_WINDOW", False);
            int close_index = find_client(event.xclient.window);
            if (event.xclient.message_type == close_window_atom &&
                event.xclient.format == 32 && close_index >= 0 &&
                clients[close_index].client == event.xclient.window) {
                close_client(close_index);
                continue;
            }
            Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
            Atom fullscreen_atom =
                XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
            Atom max_h_atom =
                XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
            Atom max_v_atom =
                XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
            int index = find_client(event.xclient.window);
            if (index >= 0 && event.xclient.message_type == state_atom &&
                event.xclient.format == 32) {
                long action = event.xclient.data.l[0];
                Atom first = (Atom)event.xclient.data.l[1];
                Atom second = (Atom)event.xclient.data.l[2];
                int fullscreen = first == fullscreen_atom ||
                                 second == fullscreen_atom;
                int maximize = (first == max_h_atom || second == max_h_atom ||
                                first == max_v_atom || second == max_v_atom);
                if (fullscreen) {
                    if (action == 2) {
                        action = clients[index].fullscreen ? 0 : 1;
                    }
                    if (action != 0) {
                        maximize_client(index, 1);
                    } else {
                        maximize_client(index, 0);
                    }
                } else if (maximize && action != 0) {
                    maximize_client(index, 0);
                }
            }
        } else if (event.type == UnmapNotify) {
            if (event.xunmap.window == keyboard_window) {
                keyboard_window = None;
                keyboard_height = 0;
                if (focused >= 0) {
                    maximize_client(focused, clients[focused].fullscreen);
                }
                publish_workarea();
                continue;
            }
            int index = find_client(event.xunmap.window);
            if (index >= 0 &&
                clients[index].client == event.xunmap.window &&
                !event.xunmap.send_event) {
                cancel_pending_close(event.xunmap.window);
                unmanage_client(index);
                if (switcher_window != None) {
                    show_switcher();
                }
            }
        } else if (event.type == DestroyNotify) {
            if (event.xdestroywindow.window == keyboard_window) {
                keyboard_window = None;
                keyboard_height = 0;
                if (focused >= 0) {
                    maximize_client(focused, clients[focused].fullscreen);
                }
                publish_workarea();
                continue;
            }
            int index = find_client(event.xdestroywindow.window);
            if (index >= 0 && clients[index].client == event.xdestroywindow.window) {
                cancel_pending_close(event.xdestroywindow.window);
                forget_client(index);
                if (switcher_window != None) {
                    show_switcher();
                }
            }
        } else if (event.type == Expose && event.xexpose.count == 0 &&
                   event.xexpose.window == switcher_window) {
            draw_switcher();
        } else if (event.type == Expose && event.xexpose.count == 0 &&
                   taskbar_window_index(event.xexpose.window) >= 0) {
            draw_taskbar_monitor();
        } else if (event.type == Expose && event.xexpose.count == 0) {
            int index = find_client(event.xexpose.window);
            if (index >= 0) {
                draw_frame(index);
            }
        } else if (event.type == PropertyNotify) {
            if (event.xproperty.window == keyboard_window) {
                place_keyboard_window(keyboard_window, -1);
                continue;
            }
            int index = find_client(event.xproperty.window);
            if (index >= 0 && clients[index].client == event.xproperty.window) {
                sync_terminal_state(index);
                update_frame_title(index);
                draw_frame(index);
                draw_taskbar();
            }
        } else if (event.type == ButtonPress) {
            if (switcher_window != None &&
                event.xbutton.window != switcher_window) {
                close_switcher();
            }
            int index = find_client(event.xbutton.window);
            int taskbar_index = taskbar_window_index(event.xbutton.window);
            if (taskbar_index >= 0) {
                if (event.xbutton.button != Button1) {
                    continue;
                }
                int section_width = screen_layout.width / 3;
                int section = event.xbutton.x / section_width;
                if (section < 0) {
                    section = 0;
                } else if (section > 2) {
                    section = 2;
                }
                if (section == 0) {
                    send_launcher_command();
                } else if (section == 1) {
                    toggle_keyboard();
                } else {
                    show_switcher();
                }
                continue;
            }
            if (event.xbutton.window == switcher_window &&
                switcher_window != None) {
                if (event.xbutton.button != Button1 ||
                    event.xbutton.y < 49) {
                    continue;
                }
                int visible = client_count < SWITCHER_ROWS
                    ? client_count : SWITCHER_ROWS;
                int first = focused >= SWITCHER_ROWS
                    ? focused - SWITCHER_ROWS + 1 : 0;
                int row = (event.xbutton.y - 49) / SWITCHER_ROW_HEIGHT;
                int selected = first + row;
                if (row >= 0 && row < visible && selected < client_count) {
                    if (event.xbutton.x >= SWITCHER_WIDTH - 64) {
                        focus_client(selected);
                        close_focused();
                        close_switcher();
                        continue;
                    }
                    focus_client(selected);
                    close_switcher();
                }
                continue;
            }
            if (index < 0) {
                continue;
            }
            focus_client(index);
            if (event.xbutton.button == Button1 &&
                clients[index].browser &&
                event.xbutton.window == clients[index].client) {
                browser_keyboard_command("on");
            }
            if (event.xbutton.window == clients[index].client) {
                int alt_drag = (event.xbutton.state & Mod1Mask) != 0 &&
                               (event.xbutton.button == Button1 ||
                                event.xbutton.button == Button3);
                if (alt_drag) {
                    if (clients[index].fullscreen) {
                        maximize_client(index, 0);
                    }
                    moving_frame = clients[index].frame;
                    drag_mode = event.xbutton.button == Button3 ? 3 : 0;
                    drag_root_x = event.xbutton.x_root;
                    drag_root_y = event.xbutton.y_root;
                    origin_x = clients[index].x;
                    origin_y = clients[index].y;
                    origin_width = clients[index].width;
                    origin_height = clients[index].height;
                    XAllowEvents(display, AsyncPointer, event.xbutton.time);
                } else {
                    XAllowEvents(display, ReplayPointer, event.xbutton.time);
                }
                continue;
            }
            int title_click = !clients[index].fullscreen &&
                event.xbutton.window == clients[index].frame &&
                event.xbutton.y < title_height(&clients[index]);
            int alt_drag = (event.xbutton.state & Mod1Mask) != 0 &&
                           (event.xbutton.button == Button1 ||
                            event.xbutton.button == Button3);
            if (title_click || alt_drag ||
                event.xbutton.window == clients[index].frame) {
                if (!alt_drag && event.xbutton.button != Button1) {
                    continue;
                }
                if (clients[index].fullscreen) {
                    maximize_client(index, 0);
                }
                moving_frame = clients[index].frame;
                if (alt_drag) {
                    drag_mode = event.xbutton.button == Button3 ? 3 : 0;
                } else if (!title_click) {
                    int resize_width = event.xbutton.x >= clients[index].width;
                    int resize_height = event.xbutton.y >=
                        title_height(&clients[index]) + clients[index].height;
                    drag_mode = (resize_width ? 1 : 0) |
                                (resize_height ? 2 : 0);
                } else {
                    drag_mode = 0;
                }
                drag_root_x = event.xbutton.x_root;
                drag_root_y = event.xbutton.y_root;
                origin_x = clients[index].x;
                origin_y = clients[index].y;
                origin_width = clients[index].width;
                origin_height = clients[index].height;
                XGrabPointer(display, event.xbutton.window, False,
                             PointerMotionMask | ButtonReleaseMask,
                             GrabModeAsync, GrabModeAsync, None, None,
                             event.xbutton.time);
            }
        } else if (event.type == MotionNotify && moving_frame != None) {
            int index = find_client(moving_frame);
            if (index >= 0) {
                int dx = event.xmotion.x_root - drag_root_x;
                int dy = event.xmotion.y_root - drag_root_y;
                if (drag_mode != 0) {
                    int maximum_width = screen_layout.width -
                                        border_width(&clients[index]);
                    int maximum_height = screen_layout.height -
                        title_height(&clients[index]) -
                        border_width(&clients[index]);
                    if (keyboard_window != None && focused == index) {
                        maximum_height -= keyboard_height;
                    }
                    if ((drag_mode & 1) != 0) {
                        clients[index].width = origin_width + dx < MIN_WIDTH
                            ? MIN_WIDTH : origin_width + dx;
                        if (clients[index].width > maximum_width) {
                            clients[index].width = maximum_width;
                        }
                    }
                    if ((drag_mode & 2) != 0) {
                        clients[index].height = origin_height + dy < MIN_HEIGHT
                            ? MIN_HEIGHT : origin_height + dy;
                        if (clients[index].height > maximum_height) {
                            clients[index].height = maximum_height;
                        }
                    }
                } else {
                    clients[index].x = origin_x + dx;
                    clients[index].y = origin_y + dy;
                }
                int max_x = screen_layout.x + screen_layout.width -
                            clients[index].width -
                            border_width(&clients[index]);
                int max_y = screen_layout.y + screen_layout.height -
                            clients[index].height -
                            title_height(&clients[index]) -
                            border_width(&clients[index]);
                if (keyboard_window != None && focused == index) {
                    max_y -= keyboard_height;
                }
                if (clients[index].x < screen_layout.x) {
                    clients[index].x = screen_layout.x;
                }
                if (clients[index].x > max_x) {
                    clients[index].x = max_x;
                }
                if (clients[index].y < screen_layout.y) {
                    clients[index].y = screen_layout.y;
                }
                if (clients[index].y > max_y) {
                    clients[index].y = max_y;
                }
                arrange_client(index);
            }
        } else if (event.type == ButtonRelease) {
            if (moving_frame != None) {
                XUngrabPointer(display, event.xbutton.time);
                moving_frame = None;
            }
        } else if (event.type == KeyPress) {
            if (event.xkey.keycode == XKeysymToKeycode(display, XK_F4)) {
                close_focused();
            } else if (event.xkey.keycode == XKeysymToKeycode(display, XK_Tab) &&
                       client_count > 0) {
                int direction = (event.xkey.state & ShiftMask) != 0 ? -1 : 1;
                int base = focused >= 0 ? focused : 0;
                int next = (base + direction + client_count) % client_count;
                focus_client(next);
                show_switcher();
            } else if (event.xkey.keycode ==
                       XKeysymToKeycode(display, XK_space)) {
                close_switcher();
                Atom owner = XInternAtom(display,
                                         "_FIELD_DESKTOP_COMMAND_OWNER", False);
                Atom command = XInternAtom(display,
                                           "_FIELD_DESKTOP_COMMAND", False);
                Window desktop = XGetSelectionOwner(display, owner);
                if (desktop != None) {
                    XEvent message;
                    memset(&message, 0, sizeof(message));
                    message.xclient.type = ClientMessage;
                    message.xclient.window = desktop;
                    message.xclient.message_type = command;
                    message.xclient.format = 32;
                    message.xclient.data.l[0] = 1;
                    XSendEvent(display, desktop, False, NoEventMask, &message);
                }
            } else if (event.xkey.keycode ==
                       XKeysymToKeycode(display, XK_Escape)) {
                close_switcher();
            }
        }
    }

    close_switcher();
    return 0;
}