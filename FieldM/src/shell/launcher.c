#include "gtk_shell_internal.h"

void launcher_close(ShellState *state)
{
    shell_keyboard_command("entry", "off");
    state->launcher = NULL;
    state->launcher_entry = NULL;
    state->launcher_results = NULL;
    if (state->desktop_window != NULL) {
        gtk_window_present(GTK_WINDOW(state->desktop_window));
    }
}

void launcher_open(ShellState *state)
{
    if (state->desktop_window == NULL) {
        shell_refresh_desktop_windows(state);
    }
    if (state->recents != NULL) {
        gtk_widget_set_visible(state->recents, FALSE);
    }
    if (state->desktop_window != NULL) {
        gtk_window_present(GTK_WINDOW(state->desktop_window));
    }
    if (state->home_search != NULL) {
        gtk_widget_grab_focus(state->home_search);
        shell_keyboard_command("entry", "on");
    }
    state->launcher = state->desktop_window;
}
