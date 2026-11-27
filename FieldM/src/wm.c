#define _POSIX_C_SOURCE 200809L

#include "screen_layout.h"

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    MAX_CLIENTS = 128,
    TITLE_HEIGHT = 48,
    MIN_WIDTH = 240,
    MIN_HEIGHT = 140,
    RESIZE_BORDER = 12,
    SWITCHER_ROWS = 10,
    SWITCHER_WIDTH = 680,
    SWITCHER_ROW_HEIGHT = 64,
    STATUS_HEIGHT = 48,
    TASKBAR_HEIGHT = 68,
    TASKBAR_BUTTON_MIN_WIDTH = 180,
    TASKBAR_BUTTON_MAX_WIDTH = 320,
    KEYBOARD_HEIGHT = 300
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
} ManagedWindow;

static Display *display;
static Window root;
static ManagedWindow clients[MAX_CLIENTS];
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
static ScreenGeometry screen_layout;
static int taskbar_enabled = 1;
static int terminal_count;

static void show_switcher(void);
static void draw_taskbar(void);
static void sync_taskbars(void);
static void arrange_client(int index);
static void maximize_client(int index, int fullscreen);

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

static void raise_keyboard_window(void)
{
    if (keyboard_window != None) {
        XRaiseWindow(display, keyboard_window);
    }
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

static int text_matches_keyboard(const char *text)
{
    static const char *const names[] = {"svkbd", "onboard"};
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

static void place_keyboard_window(Window window)
{
    int y = screen_layout.y + screen_layout.height -
            KEYBOARD_HEIGHT;
    keyboard_window = window;
    XMoveResizeWindow(display, window, screen_layout.x, y,
                      (unsigned int)screen_layout.width, KEYBOARD_HEIGHT);
    XSetWindowBorderWidth(display, window, 0);
    XSelectInput(display, window, StructureNotifyMask | PropertyChangeMask);
    XRaiseWindow(display, window);
    if (taskbar_enabled && taskbar_window != None) {
        XMapRaised(display, taskbar_window);
    }
    XFlush(display);
}

static void sync_terminal_state(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    int terminal = client_is_terminal(clients[index].client);
    if (terminal == clients[index].terminal) {
        return;
    }
    clients[index].terminal = terminal;
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
    }
    return is_desktop;
}

static void draw_frame(int index)
{
    ManagedWindow *managed = &clients[index];
    XSetForeground(display, gc, frame_color);
    XFillRectangle(display, managed->frame, gc, 0, 0,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   (unsigned int)(managed->height + TITLE_HEIGHT +
                                  RESIZE_BORDER));
    XSetForeground(display, gc,
                   index == focused ? active_color : panel_color);
    XFillRectangle(display, managed->frame, gc, 0, 0,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   TITLE_HEIGHT);

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

    draw_round_rect(managed->frame, managed->width - 58, 6, 52, 52, 18,
                    index == focused ? close_color : button_color);
    if (managed->terminal) {
        draw_round_rect(managed->frame, managed->width - 122, 6, 52, 52, 18,
                        index == focused ? accent_xft.pixel : button_color);
        draw_text(managed->frame, "< >", managed->width - 112, 41,
                  &text_xft, 32);
    }
    XSetForeground(display, gc, text_xft.pixel);
    XDrawLine(display, managed->frame, gc, managed->width - 43, 20,
              managed->width - 25, 42);
    XDrawLine(display, managed->frame, gc, managed->width - 25, 20,
              managed->width - 43, 42);
    XSetForeground(display, gc, outline_color);
    XDrawLine(display, managed->frame, gc, 0, TITLE_HEIGHT - 1,
              managed->width + RESIZE_BORDER, TITLE_HEIGHT - 1);
    XSetForeground(display, gc, index == focused ? active_color : panel_color);
    XFillRectangle(display, managed->frame, gc, managed->width, TITLE_HEIGHT,
                   RESIZE_BORDER,
                   (unsigned int)(managed->height + RESIZE_BORDER));
    XFillRectangle(display, managed->frame, gc, 0,
                   TITLE_HEIGHT + managed->height,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   RESIZE_BORDER);
    XSetForeground(display, gc, muted_xft.pixel);
    for (int offset = 3; offset <= 7; offset += 2) {
        XDrawLine(display, managed->frame, gc,
                  managed->width + RESIZE_BORDER - offset,
                  TITLE_HEIGHT + managed->height + RESIZE_BORDER - 1,
                  managed->width + RESIZE_BORDER - 1,
                  TITLE_HEIGHT + managed->height + RESIZE_BORDER - offset);
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
    if (previous != index && previous >= 0 &&
        previous < client_count && clients[previous].terminal) {
        maximize_client(previous, clients[previous].fullscreen);
    }
    if (clients[index].terminal && !clients[index].fullscreen) {
        maximize_client(index, 0);
    }
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
}

static void maximize_client(int index, int fullscreen)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    ManagedWindow *managed = &clients[index];
    managed->fullscreen = fullscreen;
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
    managed->width = screen_layout.width - RESIZE_BORDER;
    managed->height = screen_layout.height - TITLE_HEIGHT - RESIZE_BORDER -
        (managed->terminal && focused == index && !fullscreen &&
         terminal_count > 0 ? KEYBOARD_HEIGHT : 0);
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

static void arrange_client(int index)
{
    ManagedWindow *managed = &clients[index];
    XMoveResizeWindow(display, managed->frame, managed->x, managed->y,
                      (unsigned int)(managed->width + RESIZE_BORDER),
                      (unsigned int)(managed->height + TITLE_HEIGHT +
                                     RESIZE_BORDER));
    XMoveResizeWindow(display, managed->client, 0, TITLE_HEIGHT,
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
        place_keyboard_window(client);
        return;
    }
    if (client_count >= MAX_CLIENTS) {
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
    managed->x = screen_layout.x;
    managed->y = screen_layout.y;
    managed->width = screen_layout.width - RESIZE_BORDER;
    managed->height = screen_layout.height - TITLE_HEIGHT - RESIZE_BORDER;
    if (managed->width < MIN_WIDTH) {
        managed->width = MIN_WIDTH;
    }
    if (managed->height < MIN_HEIGHT) {
        managed->height = MIN_HEIGHT;
    }

    XSetWindowAttributes frame_attributes;
    frame_attributes.override_redirect = True;
    frame_attributes.background_pixel = frame_color;
    frame_attributes.event_mask = ExposureMask | ButtonPressMask;
    managed->frame = XCreateWindow(display, root, managed->x, managed->y,
                                   (unsigned int)(managed->width + RESIZE_BORDER),
                                   (unsigned int)(managed->height + TITLE_HEIGHT +
                                                  RESIZE_BORDER),
                                   1, CopyFromParent, InputOutput, CopyFromParent,
                                   CWOverrideRedirect | CWBackPixel | CWEventMask,
                                   &frame_attributes);

    XAddToSaveSet(display, client);
    XSelectInput(display, client, StructureNotifyMask | PropertyChangeMask);
    XGrabButton(display, AnyButton, AnyModifier, client, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeSync, GrabModeAsync, None, None);
    XSetWindowBorderWidth(display, client, 0);
    XReparentWindow(display, client, managed->frame, 0, TITLE_HEIGHT);
    XMapWindow(display, client);
    XMapWindow(display, managed->frame);
    maximize_client(index, 0);
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

static void close_focused(void)
{
    if (focused < 0 || focused >= client_count) {
        return;
    }

    Atom protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    Atom *supported = NULL;
    int count = 0;
    int supports_delete = XGetWMProtocols(display, clients[focused].client,
                                          &supported, &count);
    for (int i = 0; supports_delete && i < count; ++i) {
        if (supported[i] == delete_window) {
            XEvent message;
            memset(&message, 0, sizeof(message));
            message.xclient.type = ClientMessage;
            message.xclient.window = clients[focused].client;
            message.xclient.message_type = protocols;
            message.xclient.format = 32;
            message.xclient.data.l[0] = (long)delete_window;
            message.xclient.data.l[1] = CurrentTime;
            XSendEvent(display, clients[focused].client, False, NoEventMask,
                       &message);
            if (supported != NULL) {
                XFree(supported);
            }
            return;
        }
    }
    if (supported != NULL) {
        XFree(supported);
    }
    XKillClient(display, clients[focused].client);
}

static void draw_taskbar_monitor(void)
{
    Window window = taskbar_window;
    int width = screen_layout.width;
    if (window == None || !taskbar_enabled) {
        return;
    }
    XSetForeground(display, gc, named_color("#0D1015"));
    XFillRectangle(display, window, gc, 0, 0,
                   (unsigned int)width, TASKBAR_HEIGHT);
    XSetForeground(display, gc, outline_color);
    XDrawLine(display, window, gc, 0, 0, width, 0);
    int section_width = width / 3;
    static const char *labels[] = {"Apps", "Keyboard", "Windows"};
    for (int i = 0; i < 3; ++i) {
        int x = i * section_width;
        draw_round_rect(window, x + 8, 10, section_width - 16, 48, 12,
                        i == 0 ? button_active_color : button_color);
        if (i == 0) {
            XSetForeground(display, gc, accent_xft.pixel);
            XDrawRectangle(display, window, gc, x + 8, 10,
                           (unsigned int)(section_width - 16), 48);
        }
        draw_text(window, labels[i], x + 24, 41, &text_xft,
                  section_width - 48);
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
    XSetWindowAttributes attributes;
    attributes.override_redirect = True;
    attributes.background_pixel = switcher_color;
    attributes.event_mask = ExposureMask | ButtonPressMask;
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
    set_taskbar_struts();
    if (keyboard_window != None) {
        place_keyboard_window(keyboard_window);
    }
    draw_taskbar();
}

static void relayout_for_screen_size(void)
{
    sync_taskbars();
    for (int i = 0; i < client_count; ++i) {
        ManagedWindow *managed = &clients[i];
        int max_width = screen_layout.width - RESIZE_BORDER;
        int available_height = screen_layout.height;
        if (managed->terminal && focused == i && !managed->fullscreen &&
            terminal_count > 0) {
            available_height -= KEYBOARD_HEIGHT;
        }
        int max_height = available_height - TITLE_HEIGHT - RESIZE_BORDER;
        if (max_width >= MIN_WIDTH && managed->width > max_width) {
            managed->width = max_width;
        }
        if (max_height >= MIN_HEIGHT && managed->height > max_height) {
            managed->height = max_height;
        }
        int max_x = screen_layout.x + screen_layout.width - managed->width -
                    RESIZE_BORDER;
        int max_y = screen_layout.y + available_height - managed->height -
                    TITLE_HEIGHT - RESIZE_BORDER;
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
    frame_color = named_color("#0f1318");
    active_color = named_color("#202731");
    switcher_color = named_color("#171c23");
    panel_color = named_color("#171c23");
    button_color = named_color("#222933");
    button_active_color = named_color("#303d3a");
    outline_color = named_color("#303945");
    close_color = named_color("#73343d");
    gc = XCreateGC(display, root, 0, NULL);
    ui_font = XftFontOpenName(display, screen, "sans:size=15");
    if (ui_font == NULL) {
        ui_font = XftFontOpenName(display, screen, "monospace:size=15");
    }
    if (ui_font == NULL) {
        core_font = XLoadQueryFont(display, "fixed");
        if (core_font != NULL) {
            XSetFont(display, gc, core_font->fid);
        }
        fprintf(stderr, "simple-wm: using the X11 core font fallback\n");
    }
    allocate_text_color("#eef2f6", &text_xft);
    allocate_text_color("#a4afbb", &muted_xft);
    allocate_text_color("#83d6b1", &accent_xft);
    allocate_text_color("#15201c", &dark_xft);
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
            place_keyboard_window(event.xmap.window);
        } else if (event.type == ConfigureRequest) {
            int index = find_client(event.xconfigurerequest.window);
            if (index < 0) {
                if (client_is_keyboard(event.xconfigurerequest.window)) {
                    place_keyboard_window(event.xconfigurerequest.window);
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
                continue;
            }
            int index = find_client(event.xunmap.window);
            if (index >= 0 &&
                clients[index].client == event.xunmap.window &&
                !event.xunmap.send_event) {
                unmanage_client(index);
                if (switcher_window != None) {
                    show_switcher();
                }
            }
        } else if (event.type == DestroyNotify) {
            if (event.xdestroywindow.window == keyboard_window) {
                keyboard_window = None;
                continue;
            }
            int index = find_client(event.xdestroywindow.window);
            if (index >= 0 && clients[index].client == event.xdestroywindow.window) {
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
                place_keyboard_window(keyboard_window);
                continue;
            }
            int index = find_client(event.xproperty.window);
            if (index >= 0 && clients[index].client == event.xproperty.window) {
                sync_terminal_state(index);
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
            int title_click = event.xbutton.window == clients[index].frame &&
                              event.xbutton.y < TITLE_HEIGHT;
            int close_click = title_click &&
                event.xbutton.x >= clients[index].width - 64 &&
                event.xbutton.x < clients[index].width;
            int resize_click = title_click && clients[index].terminal &&
                event.xbutton.x >= clients[index].width - 128 &&
                event.xbutton.x < clients[index].width - 64;
            if (close_click && event.xbutton.button == Button1) {
                close_focused();
                continue;
            }
            if (resize_click && event.xbutton.button == Button1) {
                int next_width = 708;
                if (clients[index].width >= 650) {
                    next_width = 560;
                } else if (clients[index].width >= 500) {
                    next_width = 420;
                } else if (clients[index].width >= 380) {
                    next_width = 320;
                }
                clients[index].width = next_width - RESIZE_BORDER;
                clients[index].x = screen_layout.x +
                    (screen_layout.width - clients[index].width -
                     RESIZE_BORDER) / 2;
                arrange_client(index);
                continue;
            }
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
                        TITLE_HEIGHT + clients[index].height;
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
                    int maximum_width = screen_layout.width - RESIZE_BORDER;
                    int maximum_height = screen_layout.height - TITLE_HEIGHT -
                        RESIZE_BORDER;
                    if (clients[index].terminal && focused == index &&
                        terminal_count > 0) {
                        maximum_height -= KEYBOARD_HEIGHT;
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
                            clients[index].width - RESIZE_BORDER;
                int max_y = screen_layout.y + screen_layout.height -
                            clients[index].height - TITLE_HEIGHT - RESIZE_BORDER;
                if (clients[index].terminal && focused == index &&
                    terminal_count > 0) {
                    max_y -= KEYBOARD_HEIGHT;
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
                int next = (focused + direction + client_count) % client_count;
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