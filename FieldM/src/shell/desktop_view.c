#include "gtk_shell_internal.h"
#include "icon_badge.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/statvfs.h>

void desktop_view_center_window(ShellState *state, GtkWidget *window,
                                int width, int height)
{
    gtk_widget_realize(window);
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
        fprintf(stderr, "Could not position the desktop dialog on X11.\n");
        return;
    }
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Window xid = gdk_x11_surface_get_xid(surface);
    G_GNUC_END_IGNORE_DEPRECATIONS
    ScreenGeometry screen = screen_layout_get(state->xdisplay);
    if (width > screen.width) {
        width = screen.width;
    }
    if (height > screen.height) {
        height = screen.height;
    }
    XMoveResizeWindow(state->xdisplay, xid,
                      screen.x + (screen.width - width) / 2,
                      screen.y + (screen.height - height) / 2,
                      (unsigned int)width, (unsigned int)height);
    XSync(state->xdisplay, False);
}

void shell_update_wallpapers(ShellState *state)
{
    GdkTexture *texture = NULL;
    if (state->appearance.image_enabled) {
        GError *error = NULL;
        texture = gdk_texture_new_from_filename(state->appearance.image_path,
                                                 &error);
        if (texture == NULL) {
            fprintf(stderr, "Could not load desktop wallpaper: %s\n",
                    error != NULL ? error->message : "unknown image error");
            g_clear_error(&error);
            return;
        }
    }
    if (state->wallpaper_picture != NULL) {
        gtk_picture_set_paintable(GTK_PICTURE(state->wallpaper_picture),
                                  texture != NULL
                                      ? GDK_PAINTABLE(texture) : NULL);
    }
    g_clear_object(&texture);
}

static gboolean desktop_key_pressed(GtkEventControllerKey *controller,
                                    guint keyval, guint keycode,
                                    GdkModifierType modifiers, gpointer data)
{
    ShellState *state = data;
    (void)controller;
    (void)keycode;
    (void)modifiers;
    if (keyval != GDK_KEY_Escape) {
        return GDK_EVENT_PROPAGATE;
    }
    if (state->launcher != NULL) {
        launcher_close(state);
    } else if (state->settings != NULL) {
        shell_keyboard_command("entry", "off");
        gtk_widget_set_visible(state->settings, FALSE);
    } else {
        /* A phone Back gesture must not terminate the desktop session. */
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_STOP;
}

static void shortcut_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    if (!shell_launch_action(state, action)) {
        shell_show_error(GTK_WINDOW(state->desktop_window),
                         "The requested application could not be started.");
    }
}

static GtkWidget *make_shortcut(ShellState *state, int action,
                                const char *label, const char *icon)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *badge = shell_make_icon_badge(label, icon);
    GtkWidget *text = gtk_label_new(label);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(button, "desktop-shortcut");
    gtk_widget_set_size_request(button, 130, 130);
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_box_append(GTK_BOX(content), badge);
    gtk_box_append(GTK_BOX(content), text);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(action));
    g_signal_connect(button, "clicked", G_CALLBACK(shortcut_clicked), state);
    return button;
}

static void add_grid_button(GtkGrid *grid, GtkWidget *button,
                            int index, int columns)
{
    gtk_grid_attach(grid, button, index % columns, index / columns, 1, 1);
}

static GtkWidget *make_panel(ShellState *state)
{
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(panel, "panel");
    gtk_widget_add_css_class(panel, "statusbar");
    gtk_widget_set_size_request(panel, -1, 48);
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *clock = gtk_label_new("00:00");
    gtk_widget_add_css_class(clock, "status-clock");
    GtkWidget *network = gtk_label_new("Wi-Fi --");
    gtk_widget_add_css_class(network, "status-network");
    gtk_box_append(GTK_BOX(left), clock);
    gtk_box_append(GTK_BOX(left), network);
    gtk_widget_set_halign(left, GTK_ALIGN_START);
    GtkWidget *battery = gtk_label_new("BAT --");
    gtk_widget_add_css_class(battery, "status-battery");
    GtkWidget *keyboard = gtk_button_new_with_label("kbd");
    gtk_widget_add_css_class(keyboard, "keyboard-toggle");
    g_signal_connect(keyboard, "clicked", G_CALLBACK(shortcut_clicked), state);
    g_object_set_data(G_OBJECT(keyboard), "action",
                      GINT_TO_POINTER(SHELL_ACTION_KEYBOARD));
    gtk_widget_set_halign(keyboard, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(panel), left);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(right, GTK_ALIGN_END);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_box_append(GTK_BOX(right), battery);
    gtk_box_append(GTK_BOX(right), keyboard);
    gtk_box_append(GTK_BOX(panel), right);
    g_object_set_data(G_OBJECT(panel), "status-clock", clock);
    g_object_set_data(G_OBJECT(panel), "status-network", network);
    g_object_set_data(G_OBJECT(panel), "status-battery", battery);
    g_object_set_data(G_OBJECT(panel), "fieldm-status-panel", panel);
    state->status_clock = clock;
    state->status_network = network;
    state->status_telemetry = battery;
    return panel;
}

static int status_cpu_sample(ShellState *state)
{
    FILE *file = fopen("/proc/stat", "r");
    if (file == NULL) {
        return -1;
    }
    guint64 user = 0, nice = 0, system = 0, idle = 0;
    guint64 iowait = 0, irq = 0, softirq = 0, steal = 0;
    char label[16];
    if (fscanf(file, "%15s %" SCNu64 " %" SCNu64 " %" SCNu64
               " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
               " %" SCNu64,
               label, &user, &nice, &system, &idle, &iowait,
               &irq, &softirq, &steal) < 5) {
        fclose(file);
        return -1;
    }
    fclose(file);
    guint64 total = user + nice + system + idle + iowait + irq + softirq + steal;
    guint64 used = total - idle;
    guint64 previous_total = state->status_cpu_total;
    guint64 previous_idle = state->status_cpu_idle;
    state->status_cpu_total = total;
    state->status_cpu_idle = idle;
    if (previous_total == 0 || total <= previous_total) {
        return -1;
    }
    guint64 delta_total = total - previous_total;
    guint64 delta_idle = idle - previous_idle;
    if (delta_idle > delta_total) {
        delta_idle = delta_total;
    }
    (void)used;
    if (delta_total == 0) {
        return -1;
    }
    return (int)((delta_total - delta_idle) * 100 / delta_total);
}

static int status_ram_percent(void)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        return -1;
    }
    unsigned long total = 0, available = 0;
    unsigned long free_kb = 0, buffers = 0, cached = 0;
    char key[32];
    unsigned long value;
    char unit[16];
    while (fscanf(file, "%31s %lu %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) {
            total = value;
        } else if (strcmp(key, "MemAvailable:") == 0) {
            available = value;
        } else if (strcmp(key, "MemFree:") == 0) {
            free_kb = value;
        } else if (strcmp(key, "Buffers:") == 0) {
            buffers = value;
        } else if (strcmp(key, "Cached:") == 0) {
            cached = value;
        }
        if (total != 0 && available != 0) {
            break;
        }
    }
    fclose(file);
    if (total == 0) {
        return -1;
    }
    if (available == 0) {
        available = free_kb + buffers + cached;
    }
    unsigned long used = total > available ? total - available : 0;
    return (int)(used * 100 / total);
}

static int status_disk_percent(void)
{
    struct statvfs info;
    if (statvfs("/", &info) != 0 || info.f_blocks == 0) {
        return -1;
    }
    unsigned long long used = info.f_blocks - info.f_bfree;
    unsigned long long total = used + info.f_bavail;
    if (total == 0) {
        return -1;
    }
    return (int)(used * 100 / total);
}

static int status_battery(char *mark, size_t mark_size)
{
    GDir *dir = g_dir_open("/sys/class/power_supply", 0, NULL);
    if (dir == NULL) {
        mark[0] = '\0';
        return -1;
    }
    int percent = -1;
    const char *entry;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        char base[512];
        snprintf(base, sizeof(base), "/sys/class/power_supply/%s", entry);
        char *type_path = g_build_filename(base, "type", NULL);
        char *type = NULL;
        if (!g_file_get_contents(type_path, &type, NULL, NULL) ||
            type == NULL || !g_str_has_prefix(type, "Battery")) {
            g_free(type);
            g_free(type_path);
            continue;
        }
        g_free(type);
        g_free(type_path);
        char *capacity = NULL;
        char *status = NULL;
        char *capacity_path = g_build_filename(base, "capacity", NULL);
        char *status_path = g_build_filename(base, "status", NULL);
        g_file_get_contents(capacity_path, &capacity, NULL, NULL);
        g_file_get_contents(status_path, &status, NULL, NULL);
        if (capacity != NULL) {
            percent = atoi(g_strstrip(capacity));
        }
        snprintf(mark, mark_size, "%s",
                 status != NULL && g_str_has_prefix(g_strstrip(status), "Charging")
                     ? "*" : "");
        g_free(capacity);
        g_free(status);
        g_free(capacity_path);
        g_free(status_path);
        break;
    }
    g_dir_close(dir);
    if (percent < 0) {
        mark[0] = '\0';
    }
    return percent;
}

static void status_wifi_finished(GObject *source, GAsyncResult *result,
                                  gpointer data)
{
    ShellState *state = data;
    char *stdout_text = NULL, *stderr_text = NULL;
    GError *error = NULL;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result,
                                         &stdout_text, &stderr_text, &error);
    if (stdout_text != NULL && state->status_network != NULL) {
        gtk_label_set_text(GTK_LABEL(state->status_network),
                           strstr(stdout_text, "wpa_state=COMPLETED") != NULL
                               ? "Wi-Fi connected" : "Wi-Fi on");
    }
    g_clear_object(&state->status_wifi_process);
    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
}

static gboolean status_refresh(gpointer data)
{
    ShellState *state = data;
    if (state == NULL || state->desktop_window == NULL) {
        return G_SOURCE_CONTINUE;
    }
    GDateTime *now = g_date_time_new_now_local();
    char *time_text = g_date_time_format(now, "%H:%M");
    g_date_time_unref(now);
    if (state->status_clock != NULL) {
        gtk_label_set_text(GTK_LABEL(state->status_clock),
                           time_text != NULL ? time_text : "--:--");
    }
    g_free(time_text);

    char battery_mark[8] = "";
    int battery = status_battery(battery_mark, sizeof(battery_mark));
    char battery_text[32];
    g_snprintf(battery_text, sizeof(battery_text),
               battery >= 0 ? "BAT %d%%%s" : "BAT --",
               battery, battery_mark[0] != '\0' ? "*" : "");
    if (state->status_telemetry != NULL) {
        gtk_label_set_text(GTK_LABEL(state->status_telemetry), battery_text);
    }

    if (state->status_wifi_process == NULL) {
        GError *error = NULL;
        const char *arguments[] = {"wpa_cli", "-i", "wlan0", "status", NULL};
        GSubprocess *process = g_subprocess_newv(
            arguments, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                           G_SUBPROCESS_FLAGS_STDERR_MERGE,
            &error);
        if (process != NULL) {
            state->status_wifi_process = process;
            g_subprocess_communicate_utf8_async(
                process, NULL, NULL, status_wifi_finished, state);
        } else {
            g_clear_error(&error);
        }
    }
    return G_SOURCE_CONTINUE;
}

/*
static GtkWidget *make_legacy_settings_button(ShellState *state)
{
    GtkWidget *settings = gtk_button_new_with_label("Settings");
    gtk_widget_add_css_class(settings, "panel-action");
    gtk_widget_set_hexpand(settings, TRUE);
    g_object_set_data(G_OBJECT(settings), "action",
                      GINT_TO_POINTER(SHELL_ACTION_SETTINGS));
    g_signal_connect(settings, "clicked", G_CALLBACK(shortcut_clicked), state);
    return settings;
}
*/

static void home_search_entered(GtkEventControllerFocus *controller,
                                gpointer data)
{
    (void)controller;
    (void)data;
    shell_keyboard_command("entry", "on");
}

static int app_is_pinned(const Application *app)
{
    const char *name = app->name;
    return !g_ascii_strcasecmp(name, "Terminal") ||
           !g_ascii_strcasecmp(name, "Files") ||
           !g_ascii_strcasecmp(name, "Browser") ||
           !g_ascii_strcasecmp(name, "Settings") ||
           !g_ascii_strcasecmp(name, "Packages");
}

static void home_app_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),
                                                     "application-index"));
    if (index < state->application_count) {
        application_launch_with_catalog(&state->applications[index],
                                        state->applications,
                                        state->application_count);
    }
}

static void render_home_grid(ShellState *state, const char *query)
{
    if (state->home_grid == NULL) {
        return;
    }
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->home_grid)) != NULL) {
        gtk_grid_remove(GTK_GRID(state->home_grid), child);
    }
    guint shown = 0;
    for (int pass = 0; pass < 2 && shown < state->application_count; ++pass) {
        for (size_t i = 0; i < state->application_count; ++i) {
            Application *app = &state->applications[i];
            if ((pass == 0) != app_is_pinned(app)) {
                continue;
            }
            if (query != NULL && query[0] != '\0' &&
                !application_matches(app, query)) {
                continue;
            }
            GtkWidget *button = gtk_button_new();
            gtk_widget_add_css_class(button, "app-grid-button");
            gtk_widget_set_size_request(button, 190, 180);
            GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            GtkWidget *badge = shell_make_icon_badge(app->name, app->icon);
            GtkWidget *label = gtk_label_new(app->name);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
            gtk_box_append(GTK_BOX(content), badge);
            gtk_box_append(GTK_BOX(content), label);
            gtk_button_set_child(GTK_BUTTON(button), content);
            g_object_set_data(G_OBJECT(button), "application-index",
                              GUINT_TO_POINTER((guint)i));
            g_signal_connect(button, "clicked",
                             G_CALLBACK(home_app_clicked), state);
            gtk_grid_attach(GTK_GRID(state->home_grid), button,
                            (int)(shown % 3), (int)(shown / 3), 1, 1);
            ++shown;
        }
    }
}

static void home_search_changed(GtkEditable *editable, gpointer data)
{
    render_home_grid(data, gtk_editable_get_text(editable));
}

static void recents_close_one(GtkButton *button, gpointer data)
{
    (void)button;
    Window window = (Window)GPOINTER_TO_UINT(data);
    GdkDisplay *gdk_display = gdk_display_get_default();
    Display *display = gdk_x11_display_get_xdisplay(GDK_X11_DISPLAY(gdk_display));
    Atom delete = XInternAtom(display,
                              "WM_DELETE_WINDOW", False);
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display,
                                             "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = delete;
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(display, window, False, NoEventMask,
               &event);
    XFlush(display);
}

static void recents_switch(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    (void)list;
    ShellState *state = data;
    if (row == NULL) return;
    Window window = (Window)GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(row), "x-window"));
    if (window != None) {
        XRaiseWindow(state->xdisplay, window);
        XSetInputFocus(state->xdisplay, window, RevertToPointerRoot, CurrentTime);
        XFlush(state->xdisplay);
    }
    recents_close(state);
}

void recents_close(ShellState *state)
{
    if (state->recents != NULL) {
        gtk_widget_set_visible(state->recents, FALSE);
    }
}

static void recents_close_all(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    GtkWidget *list = g_object_get_data(G_OBJECT(button), "recents-list");
    if (list == NULL) return;
    for (GtkWidget *row = gtk_widget_get_first_child(list); row != NULL;
         row = gtk_widget_get_next_sibling(row)) {
        Window window = (Window)GPOINTER_TO_UINT(
            g_object_get_data(G_OBJECT(row), "x-window"));
        recents_close_one(NULL, GUINT_TO_POINTER((guint)window));
    }
    (void)state;
}

void recents_open(ShellState *state)
{
    GtkWidget *list = NULL;
    if (state->recents == NULL) {
        GtkWidget *window = gtk_application_window_new(state->application);
        state->recents = window;
        gtk_window_set_title(GTK_WINDOW(window), "Open apps");
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        gtk_window_set_default_size(GTK_WINDOW(window),
                                    state->screen.width,
                                    state->screen.height);
        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_add_css_class(root, "recents-screen");
        gtk_widget_set_margin_start(root, 12);
        gtk_widget_set_margin_end(root, 12);
        gtk_widget_set_margin_top(root, 12);
        gtk_widget_set_margin_bottom(root, 12);
        GtkWidget *heading = gtk_label_new("Open apps");
        gtk_widget_add_css_class(heading, "title-2");
        gtk_box_append(GTK_BOX(root), heading);
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(scroll, TRUE);
        list = gtk_list_box_new();
        gtk_widget_set_name(list, "recents-list");
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
        gtk_box_append(GTK_BOX(root), scroll);
        GtkWidget *close_all = gtk_button_new_with_label("Close all");
        gtk_widget_set_size_request(close_all, -1, 64);
        g_object_set_data(G_OBJECT(close_all), "recents-list", list);
        g_signal_connect(close_all, "clicked",
                         G_CALLBACK(recents_close_all), state);
        gtk_box_append(GTK_BOX(root), close_all);
        gtk_window_set_child(GTK_WINDOW(window), root);
        g_signal_connect(list, "row-activated",
                         G_CALLBACK(recents_switch), state);
        g_object_set_data(G_OBJECT(window), "recents-list", list);
    } else {
        list = g_object_get_data(G_OBJECT(state->recents), "recents-list");
    }
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(list)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(list), child);
    }
    Window root_return, parent_return, *children = NULL;
    unsigned int count = 0;
    XQueryTree(state->xdisplay, DefaultRootWindow(state->xdisplay),
               &root_return, &parent_return, &children, &count);
    for (unsigned int i = 0; i < count; ++i) {
        Window window = children[i];
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(state->xdisplay, window, &attrs) ||
            attrs.map_state == IsUnmapped) continue;
        char *name = NULL;
        XFetchName(state->xdisplay, window, &name);
        if (name == NULL) continue;
        if (strstr(name, "fieldm-kbd") != NULL ||
            strstr(name, "simple-desktop") != NULL ||
            strstr(name, "ZIRARCH") != NULL) {
            XFree(name);
            continue;
        }
        GtkWidget *row = gtk_list_box_row_new();
        gtk_widget_add_css_class(row, "recents-row");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *label = gtk_label_new(name);
        gtk_widget_set_hexpand(label, TRUE);
        GtkWidget *close = gtk_button_new_with_label("X");
        gtk_widget_set_size_request(close, 64, 64);
        g_object_set_data(G_OBJECT(close), "x-window",
                          GUINT_TO_POINTER((guint)window));
        g_signal_connect(close, "clicked",
                         G_CALLBACK(recents_close_one), GUINT_TO_POINTER(window));
        gtk_box_append(GTK_BOX(box), label);
        gtk_box_append(GTK_BOX(box), close);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        g_object_set_data(G_OBJECT(row), "x-window",
                          GUINT_TO_POINTER((guint)window));
        gtk_list_box_append(GTK_LIST_BOX(list), row);
        XFree(name);
    }
    if (children != NULL) XFree(children);
    gtk_window_present(GTK_WINDOW(state->recents));
}

static int set_desktop_window_type(ShellState *state, GtkWidget *window)
{
    gtk_widget_realize(window);
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
        fprintf(stderr, "Desktop surface is not backed by X11.\n");
        return 0;
    }
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Window xid = gdk_x11_surface_get_xid(surface);
    G_GNUC_END_IGNORE_DEPRECATIONS
    XChangeProperty(state->xdisplay, xid,
                    XInternAtom(state->xdisplay, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&state->desktop_type_atom, 1);
    Atom skip_taskbar = XInternAtom(
        state->xdisplay, "_NET_WM_STATE_SKIP_TASKBAR", False);
    XChangeProperty(state->xdisplay, xid,
                    XInternAtom(state->xdisplay, "_NET_WM_STATE", False),
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&skip_taskbar, 1);
    unsigned long strut[4] = {0, 0, 48, 0};
    XChangeProperty(state->xdisplay, xid,
                    XInternAtom(state->xdisplay, "_NET_WM_STRUT", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)strut, 4);
    unsigned long partial[12] = {0, 0, 48, 0,
                                 0, 0, 0, 0,
                                 state->screen.x,
                                 state->screen.x + state->screen.width - 1,
                                 0, 0};
    XChangeProperty(state->xdisplay, xid,
                    XInternAtom(state->xdisplay, "_NET_WM_STRUT_PARTIAL", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)partial, 12);
    XSync(state->xdisplay, False);
    return 1;
}

static GtkWidget *make_desktop_window(ShellState *state)
{
    GtkWidget *window = gtk_application_window_new(state->application);
    gtk_window_set_title(GTK_WINDOW(window), "ZIRARCH");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window),
                                state->screen.width,
                                state->screen.height);

    GtkWidget *surface = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(surface, "home-screen");
    gtk_widget_add_css_class(surface, "desktop-surface");
    gtk_window_set_child(GTK_WINDOW(window), surface);

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(desktop_key_pressed), state);
    gtk_widget_add_controller(window, keys);

    if (state->top_panel) {
        state->status_panel = make_panel(state);
        gtk_box_append(GTK_BOX(surface), state->status_panel);
        status_refresh(state);
        if (state->status_source == 0) {
            state->status_source =
                g_timeout_add_seconds(3, status_refresh, state);
        }
    } else {
        state->status_panel = NULL;
    }

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(body, 20);
    gtk_widget_set_margin_top(body, 18);
    gtk_widget_set_margin_end(body, 20);
    gtk_widget_set_margin_bottom(body, 12);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_box_append(GTK_BOX(surface), body);

    GtkWidget *watermark = gtk_label_new("ZIRARCH");
    gtk_widget_add_css_class(watermark, "home-watermark");
    gtk_label_set_xalign(GTK_LABEL(watermark), 0.0f);
    gtk_widget_set_margin_bottom(watermark, 8);
    gtk_box_append(GTK_BOX(body), watermark);

    state->home_search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(
        GTK_SEARCH_ENTRY(state->home_search), "Search apps");
    GtkEventController *entry_focus = gtk_event_controller_focus_new();
    g_signal_connect(entry_focus, "enter",
                     G_CALLBACK(home_search_entered), state);
    gtk_widget_add_controller(state->home_search, entry_focus);
    g_signal_connect(state->home_search, "search-changed",
                     G_CALLBACK(home_search_changed), state);
    gtk_box_append(GTK_BOX(body), state->home_search);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_halign(grid, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(grid, TRUE);
    state->home_grid = grid;
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_box_append(GTK_BOX(body), scroll);
    render_home_grid(state, "");

    if (!set_desktop_window_type(state, window)) {
        gtk_window_destroy(GTK_WINDOW(window));
        return NULL;
    }
    gtk_window_present(GTK_WINDOW(window));
    GdkSurface *native_surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (native_surface != NULL && GDK_IS_X11_SURFACE(native_surface)) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        Window xid = gdk_x11_surface_get_xid(native_surface);
        G_GNUC_END_IGNORE_DEPRECATIONS
        XMoveResizeWindow(state->xdisplay, xid, state->screen.x,
                          state->screen.y,
                          (unsigned int)state->screen.width,
                          (unsigned int)state->screen.height);
        XLowerWindow(state->xdisplay, xid);
        XFlush(state->xdisplay);
    }
    return window;
}

void shell_refresh_desktop_windows(ShellState *state)
{
    if (state->settings != NULL) {
        gtk_window_set_transient_for(GTK_WINDOW(state->settings), NULL);
    }
    if (state->launcher != NULL) {
        gtk_window_set_transient_for(GTK_WINDOW(state->launcher), NULL);
    }
    state->wallpaper_picture = NULL;
    if (state->status_source != 0) {
        g_source_remove(state->status_source);
        state->status_source = 0;
    }
    state->status_panel = NULL;
    if (state->desktop_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->desktop_window));
        state->desktop_window = NULL;
    }
    state->screen = screen_layout_get(state->xdisplay);
    state->desktop_window = make_desktop_window(state);
    if (state->settings != NULL && state->desktop_window != NULL) {
        gtk_window_set_transient_for(
            GTK_WINDOW(state->settings),
            GTK_WINDOW(state->desktop_window));
    }
    if (state->launcher != NULL && state->desktop_window != NULL) {
        gtk_window_set_transient_for(
            GTK_WINDOW(state->launcher),
            GTK_WINDOW(state->desktop_window));
    }
    shell_update_wallpapers(state);
}
