#include "gtk_shell_internal.h"
#include "icon_badge.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <stdio.h>
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
    if (state->launcher != NULL) {
        launcher_close(state);
    } else if (state->settings != NULL) {
        GtkWidget *settings = state->settings;
        state->settings = NULL;
        state->wallpaper_toggle = NULL;
        state->icon_theme_dropdown = NULL;
        gtk_window_destroy(GTK_WINDOW(settings));
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
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *badge = shell_make_icon_badge(label, icon);
    GtkWidget *text = gtk_label_new(label);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(button, "desktop-shortcut");
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
    gtk_box_set_homogeneous(GTK_BOX(panel), TRUE);
    GtkWidget *apps = gtk_button_new_with_label("Applications");
    gtk_widget_add_css_class(apps, "panel-action");
    gtk_widget_add_css_class(apps, "primary-action");
    gtk_widget_set_hexpand(apps, TRUE);
    g_object_set_data(G_OBJECT(apps), "action",
                      GINT_TO_POINTER(SHELL_ACTION_APPS));
    g_signal_connect(apps, "clicked", G_CALLBACK(shortcut_clicked), state);
    GtkWidget *keyboard = gtk_button_new_with_label("Keyboard");
    gtk_widget_add_css_class(keyboard, "panel-action");
    gtk_widget_set_hexpand(keyboard, TRUE);
    g_object_set_data(G_OBJECT(keyboard), "action",
                      GINT_TO_POINTER(SHELL_ACTION_KEYBOARD));
    g_signal_connect(keyboard, "clicked", G_CALLBACK(shortcut_clicked), state);
    GtkWidget *settings = gtk_button_new_with_label("Settings");
    gtk_widget_add_css_class(settings, "panel-action");
    gtk_widget_set_hexpand(settings, TRUE);
    g_object_set_data(G_OBJECT(settings), "action",
                      GINT_TO_POINTER(SHELL_ACTION_SETTINGS));
    g_signal_connect(settings, "clicked", G_CALLBACK(shortcut_clicked), state);
    gtk_box_append(GTK_BOX(panel), apps);
    gtk_box_append(GTK_BOX(panel), keyboard);
    gtk_box_append(GTK_BOX(panel), settings);
    return panel;
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
        {"Apps", "view-grid-symbolic", SHELL_ACTION_APPS},
        {"Terminal", "utilities-terminal-symbolic", SHELL_ACTION_TERMINAL},
        {"Files", "folder-symbolic", SHELL_ACTION_FILES},
        {"Browser", "web-browser-symbolic", SHELL_ACTION_BROWSER},
        {"Settings", "preferences-system-symbolic", SHELL_ACTION_SETTINGS}
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
        gtk_box_append(GTK_BOX(surface), make_panel(state));
    }

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_top(body, 22);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_bottom(body, 24);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_box_append(GTK_BOX(surface), body);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_halign(grid, GTK_ALIGN_START);
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
