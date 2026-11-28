#ifndef FIELD_DESKTOP_SHELL_GTK_SHELL_INTERNAL_H
#define FIELD_DESKTOP_SHELL_GTK_SHELL_INTERNAL_H

#include "../applications.h"
#include "../appearance.h"
#include "../screen_layout.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <gtk/gtk.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <X11/Xlib.h>
#include <sys/types.h>
#include <glib.h>

enum {
    SHELL_MAX_APPLICATIONS = 512,
    SHELL_ICON_THEME_ID_SIZE = 256,
    SHELL_ACTION_APPS,
    SHELL_ACTION_TERMINAL,
    SHELL_ACTION_FILES,
    SHELL_ACTION_BROWSER,
    SHELL_ACTION_KEYBOARD,
    SHELL_ACTION_SETTINGS,
    SHELL_ACTION_PACKAGES
};

typedef struct {
    char *id;
    char *name;
} ShellIconTheme;

typedef struct {
    GtkApplication *application;
    Display *xdisplay;
    Window command_window;
    Atom command_atom;
    Atom owner_atom;
    Atom desktop_type_atom;
    Application applications[SHELL_MAX_APPLICATIONS];
    size_t application_count;
    Appearance appearance;
    GPtrArray *icon_themes;
    char icon_theme_id[SHELL_ICON_THEME_ID_SIZE];
    ScreenGeometry screen;
    GtkWidget *desktop_window;
    GtkWidget *home_stack;
    GtkWidget *wallpaper_picture;
    GtkWidget *status_panel;
    GtkWidget *launcher;
    GtkWidget *settings;
    GtkWidget *launcher_entry;
    GtkWidget *launcher_results;
    GtkWidget *home_search;
    GtkWidget *home_grid;
    GtkWidget *home_pinned_title;
    GtkWidget *home_pinned_scroll;
    GtkWidget *home_clock;
    GtkWidget *home_date;
    GtkWidget *drawer_search;
    GtkWidget *drawer_grid;
    GtkWidget *recents;
    int quick_settings_open;
    GtkWidget *wallpaper_toggle;
    GtkWidget *icon_theme_dropdown;
    GtkCssProvider *styles;
    int command_selection_failed;
    int top_panel;
    int dark_theme;
    guint x_source;
    guint status_source;
    guint reap_source;
    GtkWidget *status_network;
    GtkWidget *status_clock;
    GtkWidget *status_telemetry;
    GtkWidget *quick_battery;
    GSubprocess *status_wifi_process;
    guint64 status_cpu_idle;
    guint64 status_cpu_total;
} ShellState;

void shell_show_error(GtkWindow *parent, const char *message);
void shell_update_styles(ShellState *state);
void shell_set_theme(ShellState *state, int dark);
void shell_update_wallpapers(ShellState *state);
void shell_publish_features(ShellState *state);
int shell_save_settings(ShellState *state);
void shell_refresh_desktop_windows(ShellState *state);
int shell_launch_action(ShellState *state, int action);
int shell_keyboard_command(const char *lease, const char *action);
void desktop_view_center_window(ShellState *state, GtkWidget *window,
                                int width, int height);
void launcher_open(ShellState *state);
void launcher_close(ShellState *state);
void settings_open(ShellState *state);
void settings_open_section(ShellState *state, const char *section);
void recents_open(ShellState *state);
void recents_close(ShellState *state);

#endif
