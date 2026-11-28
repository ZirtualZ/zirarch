#define _POSIX_C_SOURCE 200809L

#include "gtk_shell.h"
#include "gtk_shell_internal.h"
#include "icon_themes.h"
#include "launcher.h"
#include "settings.h"
#include "../theme.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <gdk/x11/gdkx.h>

#include <errno.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

void shell_show_error(GtkWindow *parent, const char *message)
{
    GtkWidget *dialog = gtk_window_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *label = gtk_label_new(message);
    GtkWidget *close = gtk_button_new_with_label("Close");
    gtk_widget_add_css_class(dialog, "fieldm-dialog");
    gtk_window_set_title(GTK_WINDOW(dialog), "FieldM");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, -1);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(content), label);
    gtk_box_append(GTK_BOX(content), close);
    gtk_window_set_child(GTK_WINDOW(dialog), content);
    g_signal_connect_swapped(close, "clicked",
                             G_CALLBACK(gtk_window_destroy), dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

void shell_update_styles(ShellState *state)
{
    const char *path = "/usr/share/fieldm/fieldm.css";
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        char *contents = NULL;
        gsize length = 0;
        GError *error = NULL;
        if (g_file_get_contents(path, &contents, &length, &error)) {
            char *themed = fieldm_theme_css(contents, state->dark_theme);
            g_free(contents);
            if (themed != NULL) {
                GError *css_error = NULL;
                gtk_css_provider_load_from_string(state->styles, themed);
                free(themed);
                if (css_error != NULL) g_error_free(css_error);
            }
        } else {
            fprintf(stderr, "Could not load FieldM stylesheet: %s\n",
                    error != NULL ? error->message : "unknown error");
            g_clear_error(&error);
        }
        return;
    }
    fprintf(stderr, "Could not find FieldM stylesheet: %s\n", path);
}

void shell_set_theme(ShellState *state, int dark)
{
    state->dark_theme = dark ? 1 : 0;
    g_setenv("GTK_THEME", state->dark_theme ? "Adwaita:dark" : "Adwaita:light",
             TRUE);
    GdkDisplay *gdk_display = gdk_display_get_default();
    if (gdk_display != NULL) {
        g_object_set(gtk_settings_get_for_display(gdk_display),
                     "gtk-application-prefer-dark-theme",
                     state->dark_theme, NULL);
    }
    if (state->styles != NULL) shell_update_styles(state);
    shell_save_settings(state);
    fieldm_apply_xresources(state->dark_theme, NULL);
    char *arguments[] = {"/usr/bin/xrdb", "-merge",
                         g_build_filename(g_get_home_dir(), ".Xresources", NULL),
                         NULL};
    GError *error = NULL;
    g_spawn_async(NULL, arguments, NULL, G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, &error);
    g_clear_error(&error);
    g_free(arguments[2]);
    FILE *pidfile = fopen("/run/fieldm-keyboard/pid", "r");
    pid_t pid = 0;
    if (pidfile != NULL) {
        if (fscanf(pidfile, "%d", &pid) == 1 && pid > 1) kill(pid, SIGHUP);
        fclose(pidfile);
    }
}

static gboolean reap_application_children(gpointer data)
{
    (void)data;
    applications_reap_children();
    return G_SOURCE_CONTINUE;
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
        } else if (strncmp(line, "icon_theme=", 11) == 0) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strlen(line + 11) < sizeof(state->icon_theme_id)) {
                snprintf(state->icon_theme_id,
                         sizeof(state->icon_theme_id), "%s", line + 11);
            }
        } else if (strcmp(line, "theme=dark\n") == 0) {
            state->dark_theme = 1;
        } else if (strcmp(line, "theme=light\n") == 0) {
            state->dark_theme = 0;
        }
    }
    fclose(file);
}

void shell_publish_features(ShellState *state)
{
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
                         "top_panel=%d\n"
                         "icon_theme=%s\n"
                         "theme=%s\n",
                         state->top_panel,
                         state->icon_theme_id,
                         state->dark_theme ? "dark" : "light") < 0;
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

static void show_desktop(ShellState *state)
{
    if (state->recents != NULL) recents_close(state);
    if (state->launcher != NULL) launcher_close(state);
    if (state->settings != NULL) {
        gtk_widget_set_visible(state->settings, FALSE);
    }
    shell_keyboard_command("entry", "off");
    shell_keyboard_command("manual", "off");
    unsigned long showing = 1;
    XChangeProperty(state->xdisplay,
                    DefaultRootWindow(state->xdisplay),
                    XInternAtom(state->xdisplay, "_NET_SHOWING_DESKTOP", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&showing, 1);
    if (state->desktop_window != NULL) {
        GdkSurface *surface =
            gtk_native_get_surface(GTK_NATIVE(state->desktop_window));
        if (surface != NULL && GDK_IS_X11_SURFACE(surface)) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            Window desktop = gdk_x11_surface_get_xid(surface);
            G_GNUC_END_IGNORE_DEPRECATIONS
            XRaiseWindow(state->xdisplay, desktop);
            XSetInputFocus(state->xdisplay, desktop,
                           RevertToPointerRoot, CurrentTime);
        }
    }
    XFlush(state->xdisplay);
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
            if (state->recents != NULL) recents_close(state);
            shell_keyboard_command("entry", "off");
            if (state->launcher != NULL) {
                launcher_close(state);
            } else if (state->quick_settings_open) {
                state->quick_settings_open = 0;
                if (state->home_stack != NULL) {
                    gtk_stack_set_visible_child_name(
                        GTK_STACK(state->home_stack), "home");
                }
            } else if (state->settings != NULL) {
                GtkStack *stack = g_object_get_data(
                    G_OBJECT(state->settings), "settings-stack");
                if (stack != NULL &&
                    strcmp(gtk_stack_get_visible_child_name(stack), "menu") != 0) {
                    gtk_stack_set_visible_child_name(stack, "menu");
                } else {
                    shell_keyboard_command("entry", "off");
                    gtk_widget_set_visible(state->settings, FALSE);
                }
            }
        } else if (event.type == ClientMessage &&
                   event.xclient.window == state->command_window &&
                   event.xclient.message_type == state->command_atom &&
                   event.xclient.data.l[0] == 6) {
            applications_launch_kind(state->applications,
                                     state->application_count,
                                     APPLICATION_BROWSER);
        } else if (event.type == ClientMessage &&
                   event.xclient.window == state->command_window &&
                   event.xclient.message_type == state->command_atom &&
                   event.xclient.data.l[0] == 7) {
            fprintf(stderr, "FieldM command: open settings\n");
            settings_open(state);
        } else if (event.type == ClientMessage &&
                   event.xclient.window == state->command_window &&
                   event.xclient.message_type == state->command_atom &&
                   event.xclient.data.l[0] == 3) {
            show_desktop(state);
        } else if (event.type == ClientMessage &&
                   event.xclient.window == state->command_window &&
                   event.xclient.message_type == state->command_atom &&
                   event.xclient.data.l[0] >= 3 &&
                   event.xclient.data.l[0] <= 5) {
            if (state->launcher != NULL) {
                launcher_close(state);
            }
            if (state->quick_settings_open) {
                state->quick_settings_open = 0;
                if (state->home_stack != NULL) {
                    gtk_stack_set_visible_child_name(
                        GTK_STACK(state->home_stack), "home");
                }
            }
            if (event.xclient.data.l[0] == 3 && state->recents != NULL) {
                recents_close(state);
            }
            if (event.xclient.data.l[0] == 4) {
                recents_open(state);
            }
            if (state->settings != NULL) {
                gtk_widget_set_visible(state->settings, FALSE);
            }
            if (event.xclient.data.l[0] == 3 &&
                state->desktop_window != NULL) {
                if (state->home_stack != NULL) {
                    gtk_stack_set_visible_child_name(
                        GTK_STACK(state->home_stack), "home");
                }
                gtk_window_present(GTK_WINDOW(state->desktop_window));
            }
            if (event.xclient.data.l[0] != 4) {
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
    GtkSettings *gtk_settings =
        gtk_settings_get_for_display(gdk_display_get_default());
    g_object_set(gtk_settings, "gtk-application-prefer-dark-theme",
                 state->dark_theme, NULL);
    state->application = application;
    g_application_hold(G_APPLICATION(application));
    state->styles = gtk_css_provider_new();
    shell_update_styles(state);
    icon_themes_initialize(state);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(state->styles),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    state->desktop_type_atom = XInternAtom(
        state->xdisplay, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    shell_refresh_desktop_windows(state);
    create_command_window(state);
    state->x_source = g_unix_fd_add(
        ConnectionNumber(state->xdisplay),
        G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, x_events_ready, state);
}

int gtk_shell_run(int argc, char **argv)
{
    gdk_set_allowed_backends("x11");
    ShellState *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        perror("Could not allocate desktop shell state");
        return 1;
    }
    state->top_panel = 1;
    state->xdisplay = XOpenDisplay(NULL);
    if (state->xdisplay == NULL) {
        fprintf(stderr, "Could not connect to the X11 display.\n");
        free(state);
        return 1;
    }
    load_settings(state);
    g_setenv("GTK_THEME", state->dark_theme ? "Adwaita:dark" : "Adwaita:light",
             TRUE);
    appearance_init(&state->appearance);
    state->application_count = applications_load(
        state->applications, SHELL_MAX_APPLICATIONS);
    GtkApplication *application = gtk_application_new(
        "org.zirtualz.field-desktop", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(application, "activate",
                     G_CALLBACK(application_activate), state);
    char *gtk_argv[] = {argv[0], NULL};
    state->reap_source = g_timeout_add_seconds(1,
                                               reap_application_children,
                                               state);
    int result = g_application_run(G_APPLICATION(application), 1, gtk_argv);

    if (state->x_source != 0) {
        g_source_remove(state->x_source);
    }
    if (state->status_source != 0) {
        g_source_remove(state->status_source);
    }
    if (state->reap_source != 0) {
        g_source_remove(state->reap_source);
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
    result = state->command_selection_failed ? 1 : result;
    free(state);
    (void)argc;
    return result;
}
