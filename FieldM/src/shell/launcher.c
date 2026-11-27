#include "gtk_shell_internal.h"

void launcher_close(ShellState *state)
{
    shell_keyboard_command("entry", "off");
    state->launcher = NULL;
    if (state->home_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(state->home_stack), "home");
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
    if (state->settings != NULL) {
        shell_keyboard_command("entry", "off");
        gtk_widget_set_visible(state->settings, FALSE);
    }
    state->quick_settings_open = 0;
    if (state->home_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(state->home_stack), "drawer");
    }
    if (state->drawer_search != NULL) {
        gtk_widget_grab_focus(state->drawer_search);
    }
    state->launcher = state->desktop_window;
}
