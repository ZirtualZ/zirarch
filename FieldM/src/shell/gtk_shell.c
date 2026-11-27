#define _POSIX_C_SOURCE 200809L

#include "gtk_shell.h"
#include "gtk_shell_internal.h"
#include "icon_themes.h"
#include "launcher.h"
#include "settings.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <errno.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

void shell_show_error(GtkWindow *parent, const char *message)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

void shell_update_styles(ShellState *state)
{
    static const char css[] =
        "* { all: unset; }"
        "window, dialog, popover, headerbar, notebook, scrolledwindow, "
        "viewport, list, listview, row, text, textview, grid, box, overlay, "
        "popover contents, .fieldm-menu, .desktop-surface { "
        "background-color: #F4F1EA; background-image: none; "
        "color: #111111; box-shadow: none; border: none; "
        "font-family: 'DejaVu Serif'; }"
        "window { background-color: #F4F1EA; color: #111111; }"
        "label, text, entry, button, checkbutton, radiobutton, dropdown, "
        "notebook tab, row, listview, textview { "
        "background-color: #F4F1EA; background-image: none; "
        "color: #111111; box-shadow: none; border: none; "
        "font-family: 'DejaVu Serif'; }"
        "button { min-height: 48px; min-width: 48px; "
        "border: 2px solid #111111; border-radius: 4px; "
        "padding: 8px 12px; background-image: none; "
        "background-color: #F4F1EA; color: #111111; box-shadow: none; }"
        "button:active, button:checked, button:focus, "
        "row:selected, row:active, notebook tab:checked, "
        "notebook tab:active { background-image: none; "
        "background-color: #111111; color: #F4F1EA; "
        "border: 2px solid #111111; box-shadow: none; }"
        "button:active label, button:checked label, button:focus label, "
        "row:selected label, row:active label, "
        "notebook tab:checked label, notebook tab:active label { "
        "background-color: #111111; color: #F4F1EA; }"
        "entry, textview text { border: 2px solid #111111; "
        "background-color: #F4F1EA; background-image: none; "
        "color: #111111; box-shadow: none; border-radius: 4px; "
        "caret-color: #111111; }"
        "entry:focus, textview:focus { border: 2px solid #111111; "
        "background-color: #111111; color: #F4F1EA; }"
        "entry:focus text, textview:focus text { color: #F4F1EA; }"
        "checkbutton check, radiobutton radio { "
        "background: #F4F1EA; border: 2px solid #111111; "
        "box-shadow: none; }"
        "checkbutton:checked check, radiobutton:checked radio { "
        "background: #111111; color: #F4F1EA; }"
        ".desktop-shortcut { min-width: 130px; min-height: 130px; "
        "padding: 0; border: 2px solid #111111; border-radius: 4px; "
        "background: #F4F1EA; color: #111111; box-shadow: none; }"
        ".desktop-shortcut label { font-size: 18px; font-weight: 700; "
        "color: #111111; background: transparent; }"
        ".desktop-shortcut:active, .desktop-shortcut:checked, "
        ".desktop-shortcut:focus { background: #111111; "
        "border: 2px solid #111111; color: #F4F1EA; box-shadow: none; }"
        ".desktop-shortcut:active label, .desktop-shortcut:checked label, "
        ".desktop-shortcut:focus label { color: #F4F1EA; background: transparent; }"
        ".app-icon-badge, .app-icon-image { "
        "background: #F4F1EA; border: none; border-radius: 4px; "
        "box-shadow: none; }"
        ".app-icon-letter { color: #111111; background: transparent; "
        "font-family: 'DejaVu Sans Mono'; font-size: 28px; "
        "font-weight: 800; }"
        ".home-watermark { color: #111111; background: transparent; "
        "font-size: 38px; font-weight: 800; letter-spacing: 0.16em; }"
        ".statusbar { min-height: 48px; padding: 0 12px; "
        "background: #F4F1EA; border-bottom: 2px solid #111111; "
        "box-shadow: none; }"
        ".status-clock, .status-network, .status-telemetry { "
        "font-family: 'DejaVu Sans Mono'; color: #111111; "
        "background: transparent; }"
        ".status-clock { font-size: 18px; font-weight: 800; }"
        ".status-network { font-size: 12px; }"
        ".status-telemetry { font-size: 13px; font-weight: 700; "
        "min-width: 390px; }"
        ".panel { color: #111111; background: #F4F1EA; "
        "border-bottom: 2px solid #111111; box-shadow: none; }"
        ".muted { color: #111111; background: transparent; }"
        ".launcher-row { min-height: 72px; background: transparent; "
        "border: none; border-radius: 4px; }"
        ".launcher-card { min-height: 72px; padding: 8px 12px; "
        "background: #F4F1EA; color: #111111; "
        "border: 2px solid #111111; border-radius: 4px; box-shadow: none; }"
        ".launcher-card label { color: #111111; background: transparent; "
        "font-size: 18px; }"
        ".launcher-row:selected .launcher-card, "
        ".launcher-row:active .launcher-card { "
        "background: #111111; color: #F4F1EA; border: 2px solid #111111; }"
        ".launcher-row:selected .launcher-card label, "
        ".launcher-row:active .launcher-card label { "
        "color: #F4F1EA; background: transparent; }"
        ".title-1, .title-2 { color: #111111; background: transparent; "
        "font-size: 24px; font-weight: 800; }"
        "notebook header, notebook header tabs, notebook tab { "
        "background: #F4F1EA; background-image: none; "
        "border: 1px solid #111111; border-radius: 4px; "
        "color: #111111; box-shadow: none; }"
        "notebook tab:checked { background: #111111; color: #F4F1EA; }"
        "notebook tab:checked label { color: #F4F1EA; }"
        ".primary-action, .primary-action label { "
        "background: #111111; background-image: none; "
        "color: #F4F1EA; }";
    gtk_css_provider_load_from_string(state->styles, css);
}

static void settings_path(char *path, size_t size)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        path[0] = '\0';
        return;
    }
    int length = snprintf(path, size, "%s/.config/simple-desktop/features",
                         home);
    if (length < 0 || (size_t)length >= size) {
        path[0] = '\0';
    }
}

static void load_settings(ShellState *state)
{
    char path[1200];
    settings_path(path, sizeof(path));
    FILE *file = path[0] != '\0' ? fopen(path, "r") : NULL;
    if (file == NULL) {
        return;
    }
    char line[512];
    int enabled;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "top_panel=%d", &enabled) == 1 &&
            (enabled == 0 || enabled == 1)) {
            state->top_panel = enabled;
        } else if (sscanf(line, "taskbar=%d", &enabled) == 1 &&
                   (enabled == 0 || enabled == 1)) {
            state->taskbar = enabled;
        } else if (strncmp(line, "icon_theme=", 11) == 0) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strlen(line + 11) < sizeof(state->icon_theme_id)) {
                snprintf(state->icon_theme_id,
                         sizeof(state->icon_theme_id), "%s", line + 11);
            }
        }
    }
    fclose(file);
}

void shell_publish_features(ShellState *state)
{
    unsigned long features = state->taskbar ? 1UL : 0UL;
    XChangeProperty(state->xdisplay, DefaultRootWindow(state->xdisplay),
                    state->feature_atom, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&features, 1);
    XFlush(state->xdisplay);
}

int shell_save_settings(ShellState *state)
{
    shell_publish_features(state);
    char path[1200];
    settings_path(path, sizeof(path));
    if (path[0] == '\0') {
        fprintf(stderr, "Cannot save desktop settings without a home directory.\n");
        return 0;
    }
    const char *home = getenv("HOME");
    char config_directory[1200];
    int config_length = snprintf(config_directory, sizeof(config_directory),
                                 "%s/.config", home);
    if (config_length < 0 || (size_t)config_length >= sizeof(config_directory) ||
        (mkdir(config_directory, 0700) != 0 && errno != EEXIST)) {
        perror("Could not create user configuration directory");
        return 0;
    }
    char directory[1200];
    int length = snprintf(directory, sizeof(directory),
                          "%s/.config/simple-desktop", home);
    if (length < 0 || (size_t)length >= sizeof(directory) ||
        (mkdir(directory, 0700) != 0 && errno != EEXIST)) {
        perror("Could not create desktop settings directory");
        return 0;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Could not save desktop settings");
        return 0;
    }
    int failed = fprintf(file,
                         "top_panel=%d\ntaskbar=%d\n"
                         "icon_theme=%s\n",
                         state->top_panel, state->taskbar,
                         state->icon_theme_id) < 0;
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        perror("Could not write desktop settings");
        return 0;
    }
    return 1;
}

static int launch_terminal(ShellState *state)
{
    return applications_launch_terminal(
        state->applications, state->application_count);
}

int shell_launch_action(ShellState *state, int action)
{
    switch (action) {
    case SHELL_ACTION_APPS:
        launcher_open(state);
        return 1;
    case SHELL_ACTION_TERMINAL:
        return launch_terminal(state);
    case SHELL_ACTION_FILES:
        return applications_launch_kind(state->applications,
                                        state->application_count,
                                        APPLICATION_FILE_MANAGER);
    case SHELL_ACTION_BROWSER:
        return applications_launch_kind(state->applications,
                                        state->application_count,
                                        APPLICATION_BROWSER);
    case SHELL_ACTION_KEYBOARD:
        return shell_keyboard_command("manual", "toggle");
    case SHELL_ACTION_SETTINGS:
        settings_open(state);
        return 1;
    case SHELL_ACTION_PACKAGES: {
        GError *error = NULL;
        char *arguments[] = {"/usr/bin/fieldm-packages", NULL};
        if (!g_spawn_async(NULL, arguments, NULL, G_SPAWN_DEFAULT,
                           NULL, NULL, NULL, &error)) {
            fprintf(stderr, "Could not start fieldm-packages: %s\n",
                    error != NULL ? error->message : "unknown error");
            g_clear_error(&error);
            return 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

int shell_keyboard_command(const char *lease, const char *action)
{
    GError *error = NULL;
    char *arguments[] = {
        "/usr/bin/fieldm-keyboard", (char *)lease, (char *)action, NULL
    };
    if (!g_spawn_async(NULL, arguments, NULL,
                       G_SPAWN_DEFAULT, NULL, NULL, NULL, &error)) {
        fprintf(stderr, "Could not run fieldm-keyboard %s %s: %s\n",
                lease, action, error->message);
        g_clear_error(&error);
        return 0;
    }
    return 1;
}

static void process_x_events(ShellState *state)
{
    while (XPending(state->xdisplay) > 0) {
        XEvent event;
        XNextEvent(state->xdisplay, &event);
        if (event.type == ClientMessage &&
            event.xclient.window == state->command_window &&
            event.xclient.message_type == state->command_atom &&
            event.xclient.data.l[0] == 1) {
            launcher_open(state);
        } else if (event.type == ClientMessage &&
                   event.xclient.window == state->command_window &&
                   event.xclient.message_type == state->command_atom &&
                   event.xclient.data.l[0] == 2) {
            if (state->launcher != NULL) {
                launcher_close(state);
            } else if (state->settings != NULL) {
                shell_keyboard_command("entry", "off");
                gtk_widget_set_visible(state->settings, FALSE);
            }
        } else if (event.type == ClientMessage &&
                   event.xclient.window == state->command_window &&
                   event.xclient.message_type == state->command_atom &&
                   event.xclient.data.l[0] >= 3 &&
                   event.xclient.data.l[0] <= 5) {
            if (state->launcher != NULL) {
                launcher_close(state);
            }
            if (state->settings != NULL) {
                gtk_widget_set_visible(state->settings, FALSE);
            }
            if (event.xclient.data.l[0] == 3 &&
                state->desktop_window != NULL) {
                gtk_window_present(GTK_WINDOW(state->desktop_window));
            }
            XClientMessageEvent message;
            memset(&message, 0, sizeof(message));
            message.type = ClientMessage;
            message.window = DefaultRootWindow(state->xdisplay);
            message.message_type = state->command_atom;
            message.format = 32;
            message.data.l[0] = event.xclient.data.l[0];
            XSendEvent(state->xdisplay,
                       DefaultRootWindow(state->xdisplay),
                       False, SubstructureNotifyMask,
                       (XEvent *)&message);
            XFlush(state->xdisplay);
        }
    }
}

static gboolean x_events_ready(gint fd, GIOCondition condition, gpointer data)
{
    ShellState *state = data;
    if ((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0) {
        fprintf(stderr, "X11 connection closed.\n");
        state->x_source = 0;
        g_application_quit(G_APPLICATION(state->application));
        return G_SOURCE_REMOVE;
    }
    process_x_events(state);
    (void)fd;
    return G_SOURCE_CONTINUE;
}

static void window_manager_exited(GPid pid, gint status, gpointer data)
{
    ShellState *state = data;
    state->window_manager = -1;
    state->window_manager_failed = 1;
    state->wm_source = 0;
    if (WIFEXITED(status)) {
        fprintf(stderr, "The window manager exited with status %d.\n",
                WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "The window manager stopped on signal %d.\n",
                WTERMSIG(status));
    } else {
        fprintf(stderr, "The window manager stopped unexpectedly.\n");
    }
    g_spawn_close_pid(pid);
    g_application_quit(G_APPLICATION(state->application));
}

static void create_command_window(ShellState *state)
{
    int screen = DefaultScreen(state->xdisplay);
    Window root = RootWindow(state->xdisplay, screen);
    state->command_window = XCreateSimpleWindow(
        state->xdisplay, root, -2, -2, 1, 1, 0, 0, 0);
    state->command_atom = XInternAtom(state->xdisplay,
                                      "_FIELD_DESKTOP_COMMAND", False);
    state->owner_atom = XInternAtom(state->xdisplay,
                                    "_FIELD_DESKTOP_COMMAND_OWNER", False);
    state->feature_atom = XInternAtom(state->xdisplay,
                                      "_FIELD_DESKTOP_FEATURES", False);
    state->desktop_type_atom = XInternAtom(
        state->xdisplay, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XSetSelectionOwner(state->xdisplay, state->owner_atom,
                       state->command_window, CurrentTime);
    if (XGetSelectionOwner(state->xdisplay, state->owner_atom) !=
        state->command_window) {
        fprintf(stderr, "Could not claim the desktop command selection.\n");
        state->command_selection_failed = 1;
        g_application_quit(G_APPLICATION(state->application));
    }
    XSelectInput(state->xdisplay, root, PropertyChangeMask |
                 StructureNotifyMask);
    shell_publish_features(state);
}

static void application_activate(GtkApplication *application, gpointer data)
{
    ShellState *state = data;
    state->application = application;
    g_application_hold(G_APPLICATION(application));
    state->styles = gtk_css_provider_new();
    shell_update_styles(state);
    icon_themes_initialize(state);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(state->styles),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    shell_refresh_desktop_windows(state);
    create_command_window(state);
    state->x_source = g_unix_fd_add(
        ConnectionNumber(state->xdisplay),
        G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, x_events_ready, state);
    if (state->window_manager > 0) {
        state->wm_source = g_child_watch_add(
            state->window_manager, window_manager_exited, state);
    }
}

int gtk_shell_run(int argc, char **argv, pid_t window_manager)
{
    gdk_set_allowed_backends("x11");
    ShellState *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        perror("Could not allocate desktop shell state");
        return 1;
    }
    state->top_panel = 1;
    state->taskbar = 1;
    state->window_manager = window_manager;
    state->xdisplay = XOpenDisplay(NULL);
    if (state->xdisplay == NULL) {
        fprintf(stderr, "Could not connect to the X11 display.\n");
        free(state);
        return 1;
    }
    load_settings(state);
    appearance_init(&state->appearance);
    state->application_count = applications_load(
        state->applications, SHELL_MAX_APPLICATIONS);
    GtkApplication *application = gtk_application_new(
        "org.zirtualz.field-desktop", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(application, "activate",
                     G_CALLBACK(application_activate), state);
    char *gtk_argv[] = {argv[0], NULL};
    int result = g_application_run(G_APPLICATION(application), 1, gtk_argv);

    if (state->x_source != 0) {
        g_source_remove(state->x_source);
    }
    if (state->wm_source != 0) {
        g_source_remove(state->wm_source);
    }
    if (state->status_source != 0) {
        g_source_remove(state->status_source);
    }
    if (state->status_wifi_process != NULL) {
        g_object_unref(state->status_wifi_process);
        state->status_wifi_process = NULL;
    }
    launcher_close(state);
    if (state->settings != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->settings));
    }
    if (state->desktop_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->desktop_window));
    }
    if (state->command_window != None) {
        if (XGetSelectionOwner(state->xdisplay, state->owner_atom) ==
            state->command_window) {
            XSetSelectionOwner(state->xdisplay, state->owner_atom, None,
                               CurrentTime);
        }
        XDestroyWindow(state->xdisplay, state->command_window);
    }
    g_clear_object(&state->styles);
    icon_themes_destroy(state);
    g_clear_object(&application);
    XCloseDisplay(state->xdisplay);
    result = state->window_manager_failed || state->command_selection_failed
        ? 1 : result;
    free(state);
    (void)argc;
    return result;
}
