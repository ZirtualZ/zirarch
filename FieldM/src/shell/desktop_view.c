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
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_size_request(left, 220, -1);
    GtkWidget *clock = gtk_label_new("00:00");
    gtk_widget_add_css_class(clock, "status-clock");
    gtk_label_set_xalign(GTK_LABEL(clock), 0.0f);
    GtkWidget *network = gtk_label_new("Wi-Fi --");
    gtk_widget_add_css_class(network, "status-network");
    gtk_label_set_xalign(GTK_LABEL(network), 0.0f);
    gtk_box_append(GTK_BOX(left), clock);
    gtk_box_append(GTK_BOX(left), network);
    gtk_widget_set_halign(left, GTK_ALIGN_START);

    GtkWidget *telemetry = gtk_label_new("CPU -- RAM -- / -- BAT --");
    gtk_widget_add_css_class(telemetry, "status-telemetry");
    gtk_label_set_xalign(GTK_LABEL(telemetry), 1.0f);
    gtk_widget_set_hexpand(telemetry, TRUE);
    gtk_widget_set_halign(telemetry, GTK_ALIGN_END);

    gtk_box_append(GTK_BOX(panel), left);
    gtk_box_append(GTK_BOX(panel), telemetry);
    g_object_set_data(G_OBJECT(panel), "status-clock", clock);
    g_object_set_data(G_OBJECT(panel), "status-network", network);
    g_object_set_data(G_OBJECT(panel), "status-telemetry", telemetry);
    g_object_set_data(G_OBJECT(panel), "fieldm-status-panel", panel);
    state->status_clock = clock;
    state->status_network = network;
    state->status_telemetry = telemetry;
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

    int cpu = status_cpu_sample(state);
    int ram = status_ram_percent();
    int disk = status_disk_percent();
    char battery_mark[8] = "";
    int battery = status_battery(battery_mark, sizeof(battery_mark));
    char telemetry[96];
    char cpu_text[16], ram_text[16], disk_text[16], battery_text[16];
    g_snprintf(cpu_text, sizeof(cpu_text),
               cpu >= 0 ? "%d%%" : "--", cpu);
    g_snprintf(ram_text, sizeof(ram_text),
               ram >= 0 ? "%d%%" : "--", ram);
    g_snprintf(disk_text, sizeof(disk_text),
               disk >= 0 ? "%d%%" : "--", disk);
    g_snprintf(battery_text, sizeof(battery_text),
               battery >= 0 ? "%d%%" : "--", battery);
    if (battery < 0) {
        g_snprintf(battery_text, sizeof(battery_text), "--");
    }
    g_snprintf(telemetry, sizeof(telemetry),
               "CPU %s RAM %s / %s BAT %s%s",
               cpu_text, ram_text, disk_text, battery_text,
               battery_mark[0] != '\0' ? " *" : "");
    if (state->status_telemetry != NULL) {
        gtk_label_set_text(GTK_LABEL(state->status_telemetry), telemetry);
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
    static const struct {
        const char *label;
        const char *icon;
        int action;
    } shortcuts[] = {
        {"Terminal", "utilities-terminal-symbolic", SHELL_ACTION_TERMINAL},
        {"Files", "folder-symbolic", SHELL_ACTION_FILES},
        {"Browser", "web-browser-symbolic", SHELL_ACTION_BROWSER},
        {"Settings", "preferences-system-symbolic", SHELL_ACTION_SETTINGS},
        {"Packages", "system-software-install-symbolic", SHELL_ACTION_PACKAGES}
    };

    GtkWidget *window = gtk_application_window_new(state->application);
    gtk_window_set_title(GTK_WINDOW(window), "Field Desktop");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window),
                                state->screen.width,
                                state->screen.height);

    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *surface = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(surface, "desktop-surface");
    GtkWidget *wallpaper = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(wallpaper), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(wallpaper), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_hexpand(wallpaper, TRUE);
    gtk_widget_set_vexpand(wallpaper, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), wallpaper);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), surface);
    gtk_window_set_child(GTK_WINDOW(window), overlay);
    state->wallpaper_picture = wallpaper;

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

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 36);
    gtk_widget_set_margin_start(body, 50);
    gtk_widget_set_margin_top(body, 40);
    gtk_widget_set_margin_end(body, 50);
    gtk_widget_set_margin_bottom(body, 24);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_box_append(GTK_BOX(surface), body);

    GtkWidget *watermark = gtk_label_new("ZIRARCH");
    gtk_widget_add_css_class(watermark, "home-watermark");
    gtk_label_set_xalign(GTK_LABEL(watermark), 0.0f);
    gtk_widget_set_margin_bottom(watermark, 8);
    gtk_box_append(GTK_BOX(body), watermark);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 40);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 50);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    for (int i = 0; i < (int)(sizeof(shortcuts) / sizeof(shortcuts[0])); ++i) {
        GtkWidget *button = make_shortcut(state, shortcuts[i].action,
                                          shortcuts[i].label,
                                          shortcuts[i].icon);
        add_grid_button(GTK_GRID(grid), button, i, 3);
    }
    gtk_box_append(GTK_BOX(body), grid);

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
