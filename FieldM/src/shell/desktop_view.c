#include "gtk_shell_internal.h"
#include "icon_badge.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

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
    if (state->quick_settings_open) {
        state->quick_settings_open = 0;
        gtk_stack_set_visible_child_name(GTK_STACK(state->home_stack), "home");
    } else if (state->launcher != NULL) {
        launcher_close(state);
    } else if (state->settings != NULL) {
        GtkStack *stack = g_object_get_data(G_OBJECT(state->settings),
                                            "settings-stack");
        if (stack != NULL &&
            strcmp(gtk_stack_get_visible_child_name(stack), "menu") != 0) {
            gtk_stack_set_visible_child_name(stack, "menu");
        } else {
            shell_keyboard_command("entry", "off");
            gtk_widget_set_visible(state->settings, FALSE);
        }
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
    gtk_widget_set_size_request(button, -1, 96);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_box_append(GTK_BOX(content), badge);
    gtk_box_append(GTK_BOX(content), text);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(action));
    g_signal_connect(button, "clicked", G_CALLBACK(shortcut_clicked), state);
    return button;
}

static void quick_settings_toggle(ShellState *state);

static void statusbar_clicked(GtkGestureClick *gesture, int presses,
                              double x, double y, gpointer data)
{
    quick_settings_toggle(data);
    (void)gesture; (void)presses; (void)x; (void)y;
}

static void statusbar_swiped(GtkGestureSwipe *gesture,
                             double velocity_x, double velocity_y,
                             gpointer data)
{
    if (velocity_y > 0) quick_settings_toggle(data);
    (void)gesture; (void)velocity_x;
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
    gtk_box_append(GTK_BOX(left), clock);
    gtk_widget_set_halign(left, GTK_ALIGN_START);
    GtkWidget *network = gtk_image_new_from_icon_name(
        "network-wireless-signal-none-symbolic");
    gtk_widget_add_css_class(network, "status-network");
    gtk_widget_set_tooltip_text(network, "Wi-Fi status unavailable");
    GtkWidget *battery = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(battery, "status-battery");
    GtkWidget *battery_icon = gtk_image_new_from_icon_name(
        "battery-good-symbolic");
    GtkWidget *battery_percent = gtk_label_new("--%");
    gtk_widget_add_css_class(battery_percent, "status-battery-percent");
    g_object_set_data(G_OBJECT(battery_percent), "battery-icon", battery_icon);
    gtk_box_append(GTK_BOX(battery), battery_icon);
    gtk_box_append(GTK_BOX(battery), battery_percent);
    gtk_box_append(GTK_BOX(panel), left);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(right, GTK_ALIGN_END);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_box_append(GTK_BOX(right), network);
    gtk_box_append(GTK_BOX(right), battery);
    gtk_box_append(GTK_BOX(panel), right);
    GtkGesture *tap = gtk_gesture_click_new();
    g_signal_connect(tap, "released", G_CALLBACK(statusbar_clicked), state);
    gtk_widget_add_controller(panel, GTK_EVENT_CONTROLLER(tap));
    GtkGesture *swipe = gtk_gesture_swipe_new();
    g_signal_connect(swipe, "swipe", G_CALLBACK(statusbar_swiped), state);
    gtk_widget_add_controller(panel, GTK_EVENT_CONTROLLER(swipe));
    g_object_set_data(G_OBJECT(panel), "status-clock", clock);
    g_object_set_data(G_OBJECT(panel), "status-network", network);
    g_object_set_data(G_OBJECT(panel), "status-battery", battery);
    g_object_set_data(G_OBJECT(panel), "fieldm-status-panel", panel);
    state->status_clock = clock;
    state->status_network = network;
    state->status_telemetry = battery_percent;
    return panel;
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
        int connected = strstr(stdout_text, "wpa_state=COMPLETED") != NULL;
        gtk_image_set_from_icon_name(
            GTK_IMAGE(state->status_network),
            connected ? "network-wireless-signal-excellent-symbolic"
                      : "network-wireless-signal-none-symbolic");
        gtk_widget_set_tooltip_text(
            state->status_network,
            connected ? "Wi-Fi connected" : "Wi-Fi not connected");
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
    if (state->home_clock != NULL) {
        gtk_label_set_text(GTK_LABEL(state->home_clock),
                           time_text != NULL ? time_text : "--:--");
    }
    g_free(time_text);
    if (state->home_date != NULL) {
        GDateTime *date = g_date_time_new_now_local();
        char *date_text = g_date_time_format(date, "%A, %B %e");
        gtk_label_set_text(GTK_LABEL(state->home_date),
                           date_text != NULL ? date_text : "");
        g_free(date_text);
        g_date_time_unref(date);
    }

    char battery_mark[8] = "";
    int battery = status_battery(battery_mark, sizeof(battery_mark));
    if (state->status_telemetry != NULL) {
        GtkWidget *icon = g_object_get_data(
            G_OBJECT(state->status_telemetry), "battery-icon");
        char battery_text[16];
        if (battery >= 0) {
            g_snprintf(battery_text, sizeof(battery_text), "%d%%", battery);
            gtk_widget_set_tooltip_text(
                state->status_telemetry,
                battery_mark[0] != '\0' ? "Battery charging" : "Battery");
        } else {
            g_snprintf(battery_text, sizeof(battery_text), "--%%");
        }
        gtk_label_set_text(GTK_LABEL(state->status_telemetry), battery_text);
        if (icon != NULL) {
            gtk_image_set_from_icon_name(
                GTK_IMAGE(icon),
                battery_mark[0] != '\0' ? "battery-good-charging-symbolic"
                                        : "battery-good-symbolic");
        }
    }
    if (state->quick_battery != NULL) {
        char quick_text[64];
        if (battery >= 0) {
            g_snprintf(quick_text, sizeof(quick_text),
                       "Battery %d%%%s", battery,
                       battery_mark[0] != '\0' ? " - charging" : "");
        } else {
            g_snprintf(quick_text, sizeof(quick_text),
                       "Battery status unavailable");
        }
        gtk_label_set_text(GTK_LABEL(state->quick_battery), quick_text);
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
    return app->pinned;
}

static void render_home_grid(ShellState *state);

static void save_pinned_apps(ShellState *state)
{
    const char *home = g_get_home_dir();
    char directory[1200], path[1200];
    if (snprintf(directory, sizeof(directory), "%s/.config/fieldm", home) >=
            (int)sizeof(directory) ||
        snprintf(path, sizeof(path), "%s/pinned-apps", directory) >=
            (int)sizeof(path) ||
        g_mkdir_with_parents(directory, 0700) != 0) {
        shell_show_error(GTK_WINDOW(state->desktop_window),
                         "Could not save pinned apps.");
        return;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        shell_show_error(GTK_WINDOW(state->desktop_window),
                         "Could not save pinned apps.");
        return;
    }
    int failed = 0;
    for (size_t i = 0; i < state->application_count; ++i) {
        Application *app = &state->applications[i];
        if (app->pinned && fprintf(file, "%s\n", app->desktop_id) < 0) {
            failed = 1;
            break;
        }
    }
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        shell_show_error(GTK_WINDOW(state->desktop_window),
                         "Could not save pinned apps.");
    }
}

static void app_long_pressed(GtkGestureLongPress *gesture,
                             double x, double y, gpointer data)
{
    ShellState *state = data;
    GtkWidget *button = gtk_event_controller_get_widget(
        GTK_EVENT_CONTROLLER(gesture));
    guint index = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "application-index"));
    if (index >= state->application_count) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    state->applications[index].pinned = !state->applications[index].pinned;
    gtk_widget_set_tooltip_text(button,
        state->applications[index].pinned ? "Pinned to Home" : "Unpinned");
    save_pinned_apps(state);
    render_home_grid(state);
    (void)x; (void)y;
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
        if (state->launcher != NULL) launcher_close(state);
    }
}

static GtkWidget *application_tile(ShellState *state, size_t index)
{
    Application *app = &state->applications[index];
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "app-grid-button");
    gtk_widget_set_size_request(button, 132, 116);
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    gtk_widget_set_valign(button, GTK_ALIGN_START);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    GtkWidget *badge = shell_make_icon_badge(app->name, app->icon);
    GtkWidget *label = gtk_label_new(app->name);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(content), badge);
    gtk_box_append(GTK_BOX(content), label);
    gtk_button_set_child(GTK_BUTTON(button), content);
    g_object_set_data(G_OBJECT(button), "application-index",
                      GUINT_TO_POINTER((guint)index));
    g_signal_connect(button, "clicked",
                     G_CALLBACK(home_app_clicked), state);
    GtkGesture *long_press = gtk_gesture_long_press_new();
    gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(long_press), TRUE);
    g_signal_connect(long_press, "pressed",
                     G_CALLBACK(app_long_pressed), state);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(long_press));
    return button;
}

static void render_home_grid(ShellState *state)
{
    if (state->home_grid == NULL) {
        return;
    }
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->home_grid)) != NULL) {
        gtk_grid_remove(GTK_GRID(state->home_grid), child);
    }
    guint shown = 0;
    for (size_t i = 0; i < state->application_count; ++i) {
        if (!app_is_pinned(&state->applications[i])) continue;
        gtk_grid_attach(GTK_GRID(state->home_grid),
                        application_tile(state, i), (int)(shown % 3),
                        (int)(shown / 3), 1, 1);
        ++shown;
    }
    gtk_widget_set_visible(state->home_pinned_title, shown > 0);
    gtk_widget_set_visible(state->home_pinned_scroll, shown > 0);
}

static void render_drawer_grid(ShellState *state, const char *query)
{
    if (state->drawer_grid == NULL) return;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->drawer_grid)) != NULL) {
        gtk_grid_remove(GTK_GRID(state->drawer_grid), child);
    }
    guint shown = 0;
    for (size_t i = 0; i < state->application_count; ++i) {
        Application *app = &state->applications[i];
        if (query != NULL && query[0] != '\0' &&
            !application_matches(app, query)) continue;
        GtkWidget *button = application_tile(state, i);
        gtk_grid_attach(GTK_GRID(state->drawer_grid), button,
                        (int)(shown % 4), (int)(shown / 4), 1, 1);
        ++shown;
    }
}

static void drawer_search_changed(GtkEditable *editable, gpointer data)
{
    render_drawer_grid(data, gtk_editable_get_text(editable));
}

static gboolean recents_refresh_after_close(gpointer data)
{
    ShellState *state = data;
    if (state->recents != NULL && gtk_widget_get_visible(state->recents)) {
        recents_open(state);
    }
    return G_SOURCE_REMOVE;
}

static gboolean recents_kill_after_timeout(gpointer data)
{
    Window client = (Window)GPOINTER_TO_UINT(data);
    GdkDisplay *gdk_display = gdk_display_get_default();
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *display = gdk_x11_display_get_xdisplay(
        GDK_X11_DISPLAY(gdk_display));
    G_GNUC_END_IGNORE_DEPRECATIONS
    XWindowAttributes attributes;
    if (XGetWindowAttributes(display, client, &attributes) &&
        attributes.map_state != IsUnmapped) {
        Atom pid_atom = XInternAtom(display, "_NET_WM_PID", False);
        Atom actual_type;
        int actual_format;
        unsigned long count, bytes_after;
        unsigned char *value = NULL;
        if (XGetWindowProperty(display, client, pid_atom, 0, 1, False,
                               XA_CARDINAL, &actual_type, &actual_format,
                               &count, &bytes_after, &value) == Success &&
            value != NULL && actual_type == XA_CARDINAL &&
            actual_format == 32 && count == 1) {
            pid_t pid = (pid_t)((unsigned long *)value)[0];
            if (pid > 1) {
                kill(pid, SIGTERM);
            }
            XFree(value);
        } else if (value != NULL) {
            XFree(value);
        }
        XKillClient(display, client);
        XFlush(display);
    }
    return G_SOURCE_REMOVE;
}

static int recents_request_close(ShellState *state, Window frame)
{
    GdkDisplay *gdk_display = gdk_display_get_default();
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *display = gdk_x11_display_get_xdisplay(GDK_X11_DISPLAY(gdk_display));
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (frame == None) {
        return 0;
    }
    Window client = frame;
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = client;
    event.xclient.message_type =
        XInternAtom(display, "_NET_CLOSE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime;
    event.xclient.data.l[1] = 2;
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    g_timeout_add(2000, recents_kill_after_timeout,
                  GUINT_TO_POINTER((guint)client));
    (void)state;
    return 1;
}

static void recents_close_one(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    Window frame = (Window)GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "x-window"));
    if (recents_request_close(state, frame)) {
        g_timeout_add(3400, recents_refresh_after_close, state);
    }
}

static void recents_switch(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    (void)list;
    ShellState *state = data;
    if (row == NULL) return;
    Window frame = (Window)GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(row), "x-window"));
    if (frame != None) {
        XEvent message;
        memset(&message, 0, sizeof(message));
        message.xclient.type = ClientMessage;
        message.xclient.window = frame;
        message.xclient.message_type =
            XInternAtom(state->xdisplay, "_NET_ACTIVE_WINDOW", False);
        message.xclient.format = 32;
        message.xclient.data.l[0] = 1;
        message.xclient.data.l[1] = CurrentTime;
        XSendEvent(state->xdisplay,
                   DefaultRootWindow(state->xdisplay), False,
                   SubstructureRedirectMask | SubstructureNotifyMask,
                   (XEvent *)&message);
        XFlush(state->xdisplay);
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
    int requested = 0;
    for (GtkWidget *row = gtk_widget_get_first_child(list); row != NULL;
         row = gtk_widget_get_next_sibling(row)) {
        Window window = (Window)GPOINTER_TO_UINT(
            g_object_get_data(G_OBJECT(row), "x-window"));
        requested |= recents_request_close(state, window);
    }
    if (requested) {
        g_timeout_add(3400, recents_refresh_after_close, state);
    }
}

static gboolean recents_activate(gpointer data)
{
    ShellState *state = data;
    GdkSurface *surface =
        gtk_native_get_surface(GTK_NATIVE(state->recents));
    if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
        return G_SOURCE_REMOVE;
    }
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Window client = gdk_x11_surface_get_xid(surface);
    G_GNUC_END_IGNORE_DEPRECATIONS
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = client;
    event.xclient.message_type =
        XInternAtom(state->xdisplay, "_NET_ACTIVE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1;
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(state->xdisplay, DefaultRootWindow(state->xdisplay),
               False, SubstructureRedirectMask | SubstructureNotifyMask,
               &event);
    XFlush(state->xdisplay);
    return G_SOURCE_REMOVE;
}

static int recents_has_window_type(Display *display, Window client,
                                   const char *type_name)
{
    Atom property = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom type = XInternAtom(display, type_name, False);
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *value = NULL;
    int found = 0;
    if (XGetWindowProperty(display, client, property, 0, 16, False, XA_ATOM,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &value) == Success &&
        value != NULL && actual_type == XA_ATOM && actual_format == 32) {
        Atom *types = (Atom *)value;
        for (unsigned long i = 0; i < item_count; ++i) {
            if (types[i] == type) {
                found = 1;
                break;
            }
        }
    }
    if (value != NULL) XFree(value);
    return found;
}

static int recents_is_shell_class(const XClassHint *class_hint)
{
    static const char *const shell_names[] = {
        "simple-desktop", "field-desktop", "fieldm-kbd"
    };
    const char *names[] = {class_hint->res_name, class_hint->res_class};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (names[i] == NULL) continue;
        for (size_t j = 0; j < sizeof(shell_names) / sizeof(shell_names[0]);
             ++j) {
            if (strstr(names[i], shell_names[j]) != NULL) return 1;
        }
    }
    return 0;
}

void recents_open(ShellState *state)
{
    shell_keyboard_command("entry", "off");
    if (state->launcher != NULL) launcher_close(state);
    if (state->quick_settings_open) {
        state->quick_settings_open = 0;
        if (state->home_stack != NULL) {
            gtk_stack_set_visible_child_name(
                GTK_STACK(state->home_stack), "home");
        }
    }
    if (state->settings != NULL) {
        shell_keyboard_command("entry", "off");
        gtk_widget_set_visible(state->settings, FALSE);
    }
    GtkWidget *list = NULL;
    if (state->recents == NULL) {
        GtkWidget *window = gtk_application_window_new(state->application);
        state->recents = window;
        gtk_window_set_title(GTK_WINDOW(window), "Recents");
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
        GtkWidget *heading = gtk_label_new("Recents");
        gtk_widget_add_css_class(heading, "title-2");
        gtk_box_append(GTK_BOX(root), heading);
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(scroll, TRUE);
        list = gtk_list_box_new();
        gtk_widget_set_name(list, "recents-list");
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
        gtk_box_append(GTK_BOX(root), scroll);
        GtkWidget *close_all = gtk_button_new_with_label("Clear all");
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
    int recents_count = 0;
    Atom client_list_atom =
        XInternAtom(state->xdisplay, "_NET_CLIENT_LIST", False);
    Atom actual_type;
    int actual_format;
    unsigned long count = 0;
    unsigned long bytes_after = 0;
    unsigned char *value = NULL;
    if (XGetWindowProperty(state->xdisplay,
                           DefaultRootWindow(state->xdisplay),
                           client_list_atom, 0, 1024, False, XA_WINDOW,
                           &actual_type, &actual_format, &count, &bytes_after,
                           &value) != Success || value == NULL ||
        actual_type != XA_WINDOW || actual_format != 32) {
        if (value != NULL) XFree(value);
        gtk_widget_set_visible(state->recents, TRUE);
        gtk_window_present(GTK_WINDOW(state->recents));
        g_timeout_add(100, recents_activate, state);
        return;
    }
    Window *clients = (Window *)value;
    for (unsigned long i = 0; i < count; ++i) {
        Window client = clients[i];
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(state->xdisplay, client, &attrs) ||
            attrs.map_state == IsUnmapped) continue;
        XClassHint class_hint = {0};
        int have_class = XGetClassHint(state->xdisplay, client, &class_hint);
        if (attrs.override_redirect ||
            recents_has_window_type(state->xdisplay, client,
                                    "_NET_WM_WINDOW_TYPE_DOCK") ||
            recents_has_window_type(state->xdisplay, client,
                                    "_NET_WM_WINDOW_TYPE_DESKTOP") ||
            (have_class && recents_is_shell_class(&class_hint))) {
            if (have_class) {
                if (class_hint.res_name != NULL) XFree(class_hint.res_name);
                if (class_hint.res_class != NULL) XFree(class_hint.res_class);
            }
            continue;
        }
        char *title_property = NULL;
        char *title = NULL;
        XFetchName(state->xdisplay, client, &title_property);
        if (title_property != NULL) {
            g_strstrip(title_property);
            if (title_property[0] != '\0') title = g_strdup(title_property);
            XFree(title_property);
            title_property = NULL;
        }
        char *app_name = NULL;
        if (have_class) {
            const char *fallback = class_hint.res_name;
            if (fallback == NULL || fallback[0] == '\0') {
                fallback = class_hint.res_class;
            }
            if (fallback != NULL && fallback[0] != '\0') {
                app_name = g_strdup(fallback);
            }
        }
        if (app_name == NULL) app_name = g_strdup("App");
        if (title == NULL) title = g_strdup(app_name);
        if (have_class) {
            if (class_hint.res_name != NULL) XFree(class_hint.res_name);
            if (class_hint.res_class != NULL) XFree(class_hint.res_class);
        }
        if (title == NULL || title[0] == '\0' ||
            strstr(title, "ZIRARCH") != NULL ||
            strstr(title, "FieldM") != NULL) {
            g_free(app_name);
            g_free(title);
            continue;
        }
        GtkWidget *row = gtk_list_box_row_new();
        gtk_widget_add_css_class(row, "recents-row");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(text, TRUE);
        GtkWidget *app_label = gtk_label_new(app_name);
        gtk_label_set_xalign(GTK_LABEL(app_label), 0.0f);
        GtkWidget *title_label = gtk_label_new(title);
        gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
        gtk_widget_add_css_class(app_label, "title-3");
        gtk_widget_add_css_class(title_label, "muted");
        gtk_box_append(GTK_BOX(text), app_label);
        gtk_box_append(GTK_BOX(text), title_label);
        GtkWidget *close = gtk_button_new_with_label("X");
        gtk_widget_set_size_request(close, 64, 64);
        g_signal_connect(close, "clicked",
                         G_CALLBACK(recents_close_one), state);
        g_object_set_data(G_OBJECT(close), "x-window",
                          GUINT_TO_POINTER((guint)client));
        gtk_box_append(GTK_BOX(box), text);
        gtk_box_append(GTK_BOX(box), close);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        g_object_set_data(G_OBJECT(row), "x-window",
                          GUINT_TO_POINTER((guint)client));
        gtk_list_box_append(GTK_LIST_BOX(list), row);
        ++recents_count;
        g_free(app_name);
        g_free(title);
    }
    if (recents_count == 0) {
        GtkWidget *empty = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(empty), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(empty), FALSE);
        GtkWidget *label = gtk_label_new("No open apps");
        gtk_widget_set_margin_top(label, 24);
        gtk_widget_set_margin_bottom(label, 24);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(empty), label);
        gtk_list_box_append(GTK_LIST_BOX(list), empty);
    }
    XFree(clients);
    gtk_widget_set_visible(state->recents, TRUE);
    gtk_window_present(GTK_WINDOW(state->recents));
    g_timeout_add(100, recents_activate, state);
}

static void quick_settings_close(ShellState *state)
{
    shell_keyboard_command("entry", "off");
    state->quick_settings_open = 0;
    if (state->home_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(state->home_stack), "home");
    }
}

static void quick_settings_toggle(ShellState *state)
{
    if (state->home_stack == NULL) return;
    if (state->quick_settings_open) {
        quick_settings_close(state);
        return;
    }
    if (state->launcher != NULL) launcher_close(state);
    state->quick_settings_open = 1;
    gtk_stack_set_visible_child_name(GTK_STACK(state->home_stack), "quick");
}

static void quick_wifi_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    quick_settings_close(state);
    settings_open_section(state, "wifi");
    (void)button;
}

static void quick_keyboard_clicked(GtkButton *button, gpointer data)
{
    shell_keyboard_command("manual", "toggle");
    (void)button; (void)data;
}

static void quick_gear_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    quick_settings_close(state);
    settings_open(state);
    (void)button;
}

static void quick_theme_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    shell_set_theme(state, !state->dark_theme);
    gtk_button_set_label(button, state->dark_theme ? "Theme\nDark" : "Theme\nLight");
}

static void quick_power_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    char mode[32] = "normal";
    FILE *file = fopen("/var/lib/zirarch/power-mode", "r");
    if (file != NULL) {
        fscanf(file, "%31s", mode);
        fclose(file);
    }
    const char *next = strcmp(mode, "normal") == 0 ? "medium"
                     : strcmp(mode, "medium") == 0 ? "extreme" : "normal";
    char *arguments[] = {(char *)"/usr/bin/zirarch-power",
                         (char *)next, NULL};
    g_spawn_async(NULL, arguments, NULL, G_SPAWN_DEFAULT,
                  NULL, NULL, NULL, NULL);
    gtk_button_set_label(button, strcmp(next, "normal") == 0
                                     ? "Power\nNormal"
                                     : strcmp(next, "medium") == 0
                                     ? "Power\nMedium" : "Power\nExtreme");
}

static void quick_brightness_changed(GtkRange *range, gpointer data)
{
    int brightness = (int)(gtk_range_get_value(range) * 134.0 / 100.0);
    FILE *file = fopen("/sys/class/backlight/panel/brightness", "w");
    if (file == NULL) {
        shell_show_error(GTK_WINDOW(((ShellState *)data)->desktop_window),
                         "Could not apply display brightness.");
        return;
    }
    int failed = fprintf(file, "%d", brightness) < 0;
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        shell_show_error(GTK_WINDOW(((ShellState *)data)->desktop_window),
                         "Could not apply display brightness.");
    }
}

static void quick_page_swiped(GtkGestureSwipe *gesture,
                              double velocity_x, double velocity_y,
                              gpointer data)
{
    if (velocity_y < 0) quick_settings_close(data);
    (void)gesture; (void)velocity_x;
}

static GtkWidget *make_quick_settings(ShellState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);
    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    GtkWidget *title = gtk_label_new("Quick settings");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(page), title);
    GtkWidget *tiles = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(tiles), 12);
    gtk_grid_set_column_spacing(GTK_GRID(tiles), 12);
    GtkWidget *wifi = gtk_button_new_with_label("Wi-Fi\nSettings");
    GtkWidget *keyboard = gtk_button_new_with_label("Keyboard\nShow / hide");
    GtkWidget *gear = gtk_button_new_with_label("Settings");
    GtkWidget *theme = gtk_button_new_with_label(
        state->dark_theme ? "Theme\nDark" : "Theme\nLight");
    GtkWidget *power = gtk_button_new_with_label("Power\nNormal");
    gtk_widget_set_size_request(wifi, 150, 104);
    gtk_widget_set_size_request(keyboard, 150, 104);
    gtk_widget_set_size_request(gear, 150, 104);
    gtk_widget_set_size_request(theme, 150, 104);
    gtk_widget_set_size_request(power, 150, 104);
    gtk_grid_attach(GTK_GRID(tiles), wifi, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(tiles), keyboard, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(tiles), gear, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(tiles), theme, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(tiles), power, 0, 1, 1, 1);
    g_signal_connect(wifi, "clicked", G_CALLBACK(quick_wifi_clicked), state);
    g_signal_connect(keyboard, "clicked",
                     G_CALLBACK(quick_keyboard_clicked), state);
    g_signal_connect(gear, "clicked", G_CALLBACK(quick_gear_clicked), state);
    g_signal_connect(theme, "clicked", G_CALLBACK(quick_theme_clicked), state);
    g_signal_connect(power, "clicked", G_CALLBACK(quick_power_clicked), state);
    gtk_box_append(GTK_BOX(page), tiles);
    GtkWidget *brightness_label = gtk_label_new("Brightness");
    gtk_label_set_xalign(GTK_LABEL(brightness_label), 0.0f);
    gtk_box_append(GTK_BOX(page), brightness_label);
    GtkWidget *brightness = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 5, 100, 5);
    gtk_range_set_value(GTK_RANGE(brightness), 50);
    gtk_widget_set_size_request(brightness, -1, 56);
    g_signal_connect(brightness, "value-changed",
                     G_CALLBACK(quick_brightness_changed), state);
    gtk_box_append(GTK_BOX(page), brightness);
    GtkWidget *battery = gtk_label_new("Battery status unavailable");
    state->quick_battery = battery;
    gtk_widget_add_css_class(battery, "muted");
    gtk_widget_set_halign(battery, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), battery);
    GtkWidget *notifications = gtk_label_new("Notifications");
    gtk_widget_add_css_class(notifications, "muted");
    gtk_label_set_xalign(GTK_LABEL(notifications), 0.0f);
    char *notification_text = NULL;
    gsize notification_length = 0;
    if (g_file_get_contents("/run/fieldm-notifications.tsv",
                             &notification_text,
                             &notification_length, NULL) &&
        notification_length > 0) {
        gtk_label_set_text(GTK_LABEL(notifications), notification_text);
    }
    g_free(notification_text);
    gtk_widget_set_halign(notifications, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), notifications);
    GtkGesture *swipe = gtk_gesture_swipe_new();
    g_signal_connect(swipe, "swipe", G_CALLBACK(quick_page_swiped), state);
    gtk_widget_add_controller(page, GTK_EVENT_CONTROLLER(swipe));
    return page;
}

static void home_swiped(GtkGestureSwipe *gesture,
                        double velocity_x, double velocity_y, gpointer data)
{
    if (velocity_y < 0) launcher_open(data);
    (void)gesture; (void)velocity_x;
}

static void drawer_swiped(GtkGestureSwipe *gesture,
                         double velocity_x, double velocity_y, gpointer data)
{
    if (velocity_y > 0) launcher_close(data);
    (void)gesture; (void)velocity_x;
}

static GtkWidget *make_home_page(ShellState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);
    gtk_widget_set_margin_top(page, 20);
    gtk_widget_set_margin_bottom(page, 12);
    state->home_clock = gtk_label_new("--:--");
    gtk_widget_add_css_class(state->home_clock, "home-clock");
    gtk_label_set_xalign(GTK_LABEL(state->home_clock), 0.0f);
    state->home_date = gtk_label_new("");
    gtk_widget_add_css_class(state->home_date, "home-date");
    gtk_label_set_xalign(GTK_LABEL(state->home_date), 0.0f);
    gtk_box_append(GTK_BOX(page), state->home_clock);
    gtk_box_append(GTK_BOX(page), state->home_date);
    GtkWidget *dock = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(dock), TRUE);
    gtk_widget_add_css_class(dock, "home-dock");
    gtk_widget_set_halign(dock, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(dock, TRUE);
    gtk_box_append(GTK_BOX(dock), make_shortcut(
        state, SHELL_ACTION_TERMINAL, "Terminal", NULL));
    if (applications_kind_available(state->applications,
                                    state->application_count,
                                    APPLICATION_FILE_MANAGER)) {
        gtk_box_append(GTK_BOX(dock), make_shortcut(
            state, SHELL_ACTION_FILES, "Files", NULL));
    }
    if (applications_kind_available(state->applications,
                                    state->application_count,
                                    APPLICATION_BROWSER)) {
        gtk_box_append(GTK_BOX(dock), make_shortcut(
            state, SHELL_ACTION_BROWSER, "Browser", NULL));
    }
    gtk_box_append(GTK_BOX(dock), make_shortcut(
        state, SHELL_ACTION_SETTINGS, "Settings", NULL));
    gtk_box_append(GTK_BOX(dock), make_shortcut(
        state, SHELL_ACTION_APPS, "Apps", NULL));
    gtk_box_append(GTK_BOX(page), dock);
    GtkGesture *swipe = gtk_gesture_swipe_new();
    g_signal_connect(swipe, "swipe", G_CALLBACK(home_swiped), state);
    gtk_widget_add_controller(page, GTK_EVENT_CONTROLLER(swipe));
    return page;
}

static GtkWidget *make_drawer_page(ShellState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);
    gtk_widget_set_margin_top(page, 12);
    gtk_widget_set_margin_bottom(page, 12);
    GtkWidget *title = gtk_label_new("All apps");
    gtk_widget_add_css_class(title, "title-2");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(page), title);
    state->drawer_search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(
        GTK_SEARCH_ENTRY(state->drawer_search), "Search apps");
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(home_search_entered), state);
    gtk_widget_add_controller(state->drawer_search, focus);
    g_signal_connect(state->drawer_search, "search-changed",
                     G_CALLBACK(drawer_search_changed), state);
    gtk_box_append(GTK_BOX(page), state->drawer_search);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    state->drawer_grid = grid;
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_box_append(GTK_BOX(page), scroll);
    render_drawer_grid(state, "");
    GtkGesture *swipe = gtk_gesture_swipe_new();
    g_signal_connect(swipe, "swipe", G_CALLBACK(drawer_swiped), state);
    gtk_widget_add_controller(page, GTK_EVENT_CONTROLLER(swipe));
    return page;
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

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_box_append(GTK_BOX(surface), body);
    state->home_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(state->home_stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_vexpand(state->home_stack, TRUE);
    gtk_stack_add_named(GTK_STACK(state->home_stack),
                        make_home_page(state), "home");
    gtk_stack_add_named(GTK_STACK(state->home_stack),
                        make_drawer_page(state), "drawer");
    gtk_stack_add_named(GTK_STACK(state->home_stack),
                        make_quick_settings(state), "quick");
    gtk_stack_set_visible_child_name(GTK_STACK(state->home_stack), "home");
    gtk_box_append(GTK_BOX(body), state->home_stack);

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
    state->home_stack = NULL;
    state->home_grid = NULL;
    state->home_search = NULL;
    state->drawer_search = NULL;
    state->drawer_grid = NULL;
    state->home_clock = NULL;
    state->home_date = NULL;
    state->quick_battery = NULL;
    state->launcher = NULL;
    state->quick_settings_open = 0;
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
