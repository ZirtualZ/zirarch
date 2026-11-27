#include "gtk_shell_internal.h"
#include "icon_badge.h"

#include <X11/keysym.h>

#include <unistd.h>

static void launch_row(ShellState *state, GtkListBoxRow *row)
{
    guint encoded = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(row), "application-index"));
    if (encoded == 0 || encoded - 1 >= state->application_count) {
        return;
    }
    if (!application_launch_with_catalog(&state->applications[encoded - 1],
                                         state->applications,
                                         state->application_count)) {
        shell_show_error(GTK_WINDOW(state->launcher),
                         "The selected application could not be started.");
        return;
    }
    launcher_close(state);
}

static void row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    (void)list;
    launch_row(data, row);
}

static void search_focus_entered(GtkEventControllerFocus *controller,
                                 gpointer data)
{
    (void)controller;
    shell_keyboard_command("entry", "on");
    (void)data;
}

static void entry_activated(GtkEntry *entry, gpointer data)
{
    ShellState *state = data;
    GtkListBox *results = GTK_LIST_BOX(state->launcher_results);
    GtkListBoxRow *row = gtk_list_box_get_selected_row(results);
    if (row == NULL) {
        row = gtk_list_box_get_row_at_index(results, 0);
    }
    if (row != NULL) {
        launch_row(state, row);
    }
    (void)entry;
}

static void search_changed(GtkEditable *editable, gpointer data)
{
    ShellState *state = data;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->launcher_results)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(state->launcher_results), child);
    }
    const char *query = gtk_editable_get_text(editable);
    int shown = 0;
    for (size_t i = 0; i < state->application_count && shown < 12; ++i) {
        if (!application_matches(&state->applications[i], query)) {
            continue;
        }
        GtkWidget *row = gtk_list_box_row_new();
        gtk_widget_add_css_class(row, "launcher-row");
        GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(content, "launcher-card");
        const char *icon = state->applications[i].icon;
        GtkWidget *badge = shell_make_icon_badge(
            state->applications[i].name, icon);
        GtkWidget *label = gtk_label_new(state->applications[i].name);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_end(label, 12);
        gtk_widget_set_margin_top(label, 10);
        gtk_widget_set_margin_bottom(label, 10);
        gtk_box_append(GTK_BOX(content), badge);
        gtk_box_append(GTK_BOX(content), label);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        g_object_set_data(G_OBJECT(row), "application-index",
                          GUINT_TO_POINTER((guint)i + 1));
        gtk_list_box_append(GTK_LIST_BOX(state->launcher_results), row);
        ++shown;
    }
    if (shown == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new("No matching applications");
        gtk_widget_add_css_class(label, "muted");
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_append(GTK_LIST_BOX(state->launcher_results), row);
    } else {
        gtk_list_box_select_row(GTK_LIST_BOX(state->launcher_results),
                                gtk_list_box_get_row_at_index(
                                    GTK_LIST_BOX(state->launcher_results), 0));
    }
}

static gboolean key_pressed(GtkEventControllerKey *controller,
                            guint keyval, guint keycode,
                            GdkModifierType modifiers, gpointer data)
{
    ShellState *state = data;
    (void)controller;
    (void)keycode;
    (void)modifiers;
    if (keyval == GDK_KEY_Escape) {
        launcher_close(state);
        return GDK_EVENT_STOP;
    }
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down) {
        GtkListBox *list = GTK_LIST_BOX(state->launcher_results);
        GtkListBoxRow *selected = gtk_list_box_get_selected_row(list);
        int count = 0;
        for (GtkWidget *row = gtk_widget_get_first_child(GTK_WIDGET(list));
             row != NULL; row = gtk_widget_get_next_sibling(row)) {
            ++count;
        }
        if (count > 0) {
            int index = selected != NULL
                ? gtk_list_box_row_get_index(selected) : 0;
            index = keyval == GDK_KEY_Down
                ? (index + 1) % count : (index + count - 1) % count;
            gtk_list_box_select_row(list,
                                    gtk_list_box_get_row_at_index(list, index));
        }
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean window_closed(GtkWindow *window, gpointer data)
{
    ShellState *state = data;
    if (state->launcher == GTK_WIDGET(window)) {
        state->launcher = NULL;
        state->launcher_entry = NULL;
        state->launcher_results = NULL;
        shell_keyboard_command("entry", "off");
    }
    return GDK_EVENT_PROPAGATE;
}

void launcher_close(ShellState *state)
{
    if (state->launcher == NULL) {
        return;
    }
    GtkWidget *window = state->launcher;
    state->launcher = NULL;
    state->launcher_entry = NULL;
    state->launcher_results = NULL;
    shell_keyboard_command("entry", "off");
    gtk_window_destroy(GTK_WINDOW(window));
}

void launcher_open(ShellState *state)
{
    if (state->launcher != NULL) {
        gtk_window_present(GTK_WINDOW(state->launcher));
        gtk_widget_grab_focus(state->launcher_entry);
        shell_keyboard_command("entry", "on");
        return;
    }
    GtkWidget *window = gtk_application_window_new(state->application);
    state->launcher = window;
    gtk_widget_add_css_class(window, "fieldm-menu");
    gtk_window_set_title(GTK_WINDOW(window), "Applications");
    gtk_window_set_default_size(GTK_WINDOW(window), 688, 1080);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 22);
    gtk_widget_set_margin_end(content, 22);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);
    GtkWidget *heading = gtk_label_new("Applications");
    gtk_widget_add_css_class(heading, "title-2");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    state->launcher_entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(state->launcher_entry),
                                          "Search installed applications");
    GtkEventController *entry_focus = gtk_event_controller_focus_new();
    g_signal_connect(entry_focus, "enter",
                     G_CALLBACK(search_focus_entered), state);
    gtk_widget_add_controller(state->launcher_entry, entry_focus);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    state->launcher_results = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state->launcher_results),
                                    GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(
        GTK_LIST_BOX(state->launcher_results), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                  state->launcher_results);
    gtk_box_append(GTK_BOX(content), heading);
    gtk_box_append(GTK_BOX(content), state->launcher_entry);
    gtk_box_append(GTK_BOX(content), scroll);
    gtk_window_set_child(GTK_WINDOW(window), content);

    g_signal_connect(state->launcher_entry, "search-changed",
                     G_CALLBACK(search_changed), state);
    g_signal_connect(state->launcher_entry, "activate",
                     G_CALLBACK(entry_activated), state);
    g_signal_connect(state->launcher_results, "row-activated",
                     G_CALLBACK(row_activated), state);
    g_signal_connect(window, "close-request",
                     G_CALLBACK(window_closed), state);
    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), state);
    gtk_widget_add_controller(window, keys);
    desktop_view_center_window(state, window, 688, 1080);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(state->launcher_entry);
    shell_keyboard_command("entry", "on");
    search_changed(GTK_EDITABLE(state->launcher_entry), state);
}
