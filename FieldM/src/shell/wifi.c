#include "wifi.h"

#include <string.h>

typedef enum {
    WIFI_OPERATION_STATUS,
    WIFI_OPERATION_SCAN,
    WIFI_OPERATION_CONNECT,
    WIFI_OPERATION_SAVED,
    WIFI_OPERATION_FORGET
} WifiOperation;

typedef struct {
    GtkWindow *parent;
    GtkWidget *status;
    GtkWidget *result;
    GtkWidget *scan_button;
    GtkWidget *networks;
    GtkWidget *saved_networks;
    GtkWidget *password_window;
    GtkWidget *password_entry;
    char *password_ssid;
    GSubprocess *process;
    WifiOperation operation;
    int destroyed;
} WifiPageState;

static void wifi_page_maybe_free(WifiPageState *state)
{
    if (!state->destroyed || state->process != NULL) {
        return;
    }
    g_free(state->password_ssid);
    g_free(state);
}

static void wifi_page_destroyed(GtkWidget *page, gpointer data)
{
    WifiPageState *state = data;
    (void)page;
    state->destroyed = 1;
    state->parent = NULL;
    state->status = NULL;
    state->result = NULL;
    state->scan_button = NULL;
    state->networks = NULL;
    if (state->password_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->password_window));
    }
    wifi_page_maybe_free(state);
}

static void wifi_set_busy(WifiPageState *state, int busy)
{
    if (state->destroyed) {
        return;
    }
    gtk_widget_set_sensitive(state->scan_button, !busy);
    gtk_widget_set_sensitive(state->networks, !busy);
}

static void wifi_set_result(WifiPageState *state, const char *text, int error)
{
    if (state->destroyed) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(state->result),
                       text != NULL && text[0] != '\0' ? text
                       : error ? "The Wi-Fi command failed."
                               : "Wi-Fi command completed.");
    if (error) {
        gtk_widget_add_css_class(state->result, "error");
    } else {
        gtk_widget_remove_css_class(state->result, "error");
    }
}

static void wifi_add_network(WifiPageState *state, const char *signal,
                             const char *security, const char *ssid)
{
    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "wifi-network");
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    gtk_widget_set_margin_top(content, 8);
    gtk_widget_set_margin_bottom(content, 8);
    GtkWidget *name = gtk_label_new(ssid);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    GtkWidget *details = gtk_label_new(NULL);
    char description[160];
    g_snprintf(description, sizeof(description), "%s  |  %s",
               security[0] != '\0' ? security : "unknown",
               signal[0] != '\0' ? signal : "?");
    gtk_label_set_text(GTK_LABEL(details), description);
    gtk_label_set_xalign(GTK_LABEL(details), 0.0f);
    gtk_widget_add_css_class(details, "muted");
    gtk_box_append(GTK_BOX(content), name);
    gtk_box_append(GTK_BOX(content), details);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
    g_object_set_data_full(G_OBJECT(row), "ssid", g_strdup(ssid), g_free);
    g_object_set_data_full(G_OBJECT(row), "security",
                           g_strdup(security), g_free);
    gtk_list_box_append(GTK_LIST_BOX(state->networks), row);
}

static int wifi_populate_networks(WifiPageState *state, const char *output)
{
    if (state->destroyed) {
        return 0;
    }
    GtkWidget *child;
    while ((child =
                gtk_widget_get_first_child(state->networks)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(state->networks), child);
    }
    char **lines = g_strsplit(output != NULL ? output : "", "\n", -1);
    int count = 0;
    for (size_t i = 0; lines[i] != NULL; ++i) {
        const char *line = lines[i];
        if (line[0] == '\0' ||
            g_str_has_prefix(line, "SIGNAL") ||
            g_str_has_prefix(line, "------")) {
            continue;
        }
        const char *cursor = line;
        while (g_ascii_isspace(*cursor)) {
            ++cursor;
        }
        const char *signal_start = cursor;
        while (*cursor != '\0' && !g_ascii_isspace(*cursor)) {
            ++cursor;
        }
        char *signal = g_strndup(signal_start, (gsize)(cursor - signal_start));
        while (g_ascii_isspace(*cursor)) {
            ++cursor;
        }
        const char *security_start = cursor;
        while (*cursor != '\0' && !g_ascii_isspace(*cursor)) {
            ++cursor;
        }
        char *security =
            g_strndup(security_start, (gsize)(cursor - security_start));
        while (g_ascii_isspace(*cursor)) {
            ++cursor;
        }
        char *ssid = g_strdup(cursor);
        g_strstrip(ssid);
        if (ssid[0] != '\0') {
            wifi_add_network(state, signal, security, ssid);
            ++count;
        }
        g_free(signal);
        g_free(security);
        g_free(ssid);
    }
    g_strfreev(lines);
    if (count == 0) {
        GtkWidget *empty = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new("No networks found.");
        gtk_widget_set_margin_top(label, 18);
        gtk_widget_set_margin_bottom(label, 18);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(empty), label);
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(empty), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(empty), FALSE);
        gtk_list_box_append(GTK_LIST_BOX(state->networks), empty);
    }
    return count;
}

static int wifi_run(WifiPageState *state, WifiOperation operation,
                    const char * const *arguments, const char *stdin_text);
static void wifi_saved_refresh(WifiPageState *state);
static void wifi_populate_saved(WifiPageState *state, const char *output);

static void wifi_operation_finished(GObject *source, GAsyncResult *result,
                                    gpointer data)
{
    WifiPageState *state = data;
    GSubprocess *process = G_SUBPROCESS(source);
    char *output = NULL;
    GError *error = NULL;
    int communicated =
        g_subprocess_communicate_utf8_finish(
            process, result, &output, NULL, &error);
    int succeeded = communicated &&
                    g_subprocess_get_if_exited(process) &&
                    g_subprocess_get_successful(process);
    int exited = g_subprocess_get_if_exited(process);
    int exit_status = exited ? g_subprocess_get_exit_status(process) : -1;
    WifiOperation operation = state->operation;
    state->process = NULL;
    g_object_unref(process);
    wifi_set_busy(state, 0);

    if (!succeeded) {
        char *message;
        if (output != NULL && output[0] != '\0') {
            message = g_strdup(output);
        } else if (error != NULL) {
            message = g_strdup(error->message);
        } else {
            message = g_strdup_printf("Command failed (exit status %d).",
                                      exit_status);
        }
        wifi_set_result(state, message, 1);
        g_free(message);
    } else if (operation == WIFI_OPERATION_SCAN) {
        int count = wifi_populate_networks(state, output);
        char *summary = g_strdup_printf("Found %d network%s.",
                                        count, count == 1 ? "" : "s");
        wifi_set_result(state, summary, 0);
        g_free(summary);
    } else if (operation == WIFI_OPERATION_STATUS) {
        if (!state->destroyed) {
            gtk_label_set_text(GTK_LABEL(state->status),
                               output != NULL && output[0] != '\0'
                                   ? output : "Wi-Fi status is unavailable.");
        }
        wifi_set_result(state, "Status refreshed.", 0);
        wifi_saved_refresh(state);
    } else if (operation == WIFI_OPERATION_SAVED) {
        wifi_populate_saved(state, output);
        wifi_set_result(state, "Saved networks refreshed.", 0);
    } else if (operation == WIFI_OPERATION_FORGET) {
        wifi_set_result(state, "Saved network forgotten.", 0);
        wifi_saved_refresh(state);
    } else {
        wifi_set_result(state, output, 0);
        if (!state->destroyed) {
            const char *const status_args[] = {
                "/usr/bin/wifi", "status", NULL
            };
            wifi_run(state, WIFI_OPERATION_STATUS, status_args, NULL);
        }
    }

    g_free(output);
    g_clear_error(&error);
    wifi_page_maybe_free(state);
}

static int wifi_run(WifiPageState *state, WifiOperation operation,
                    const char * const *arguments, const char *stdin_text)
{
    if (state->destroyed || state->process != NULL) {
        return 0;
    }
    GError *error = NULL;
    GSubprocess *process = g_subprocess_newv(
        arguments,
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error);
    if (process == NULL) {
        wifi_set_result(state,
                        error != NULL ? error->message
                                      : "Could not start the Wi-Fi command.",
                        1);
        g_clear_error(&error);
        return 0;
    }
    state->operation = operation;
    state->process = process;
    wifi_set_busy(state, 1);
    wifi_set_result(state, "Working...", 0);
    g_subprocess_communicate_utf8_async(
        process, stdin_text, NULL, wifi_operation_finished, state);
    return 1;
}

static void wifi_scan(GtkButton *button, gpointer data)
{
    WifiPageState *state = data;
    const char *const arguments[] = {"/usr/bin/wifi", "scan", NULL};
    wifi_run(state, WIFI_OPERATION_SCAN, arguments, NULL);
    (void)button;
}

static void wifi_password_destroyed(GtkWidget *window, gpointer data)
{
    WifiPageState *state = data;
    if (state->password_window != GTK_WIDGET(window)) {
        return;
    }
    state->password_window = NULL;
    state->password_entry = NULL;
    g_clear_pointer(&state->password_ssid, g_free);
    shell_keyboard_command("entry", "off");
}

static void wifi_password_cancel(GtkButton *button, gpointer data)
{
    WifiPageState *state = data;
    if (state->password_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->password_window));
    }
    (void)button;
}

static void wifi_password_connect(GtkButton *button, gpointer data)
{
    WifiPageState *state = data;
    const char *password =
        gtk_editable_get_text(GTK_EDITABLE(state->password_entry));
    if (state->password_ssid == NULL || password[0] == '\0') {
        gtk_widget_grab_focus(state->password_entry);
        return;
    }
    char *input = g_strdup_printf("%s\n", password);
    char *ssid = g_strdup(state->password_ssid);
    const char *const arguments[] = {
        "/usr/bin/wifi", "connect", ssid, NULL
    };
    gtk_window_destroy(GTK_WINDOW(state->password_window));
    wifi_run(state, WIFI_OPERATION_CONNECT, arguments, input);
    g_free(input);
    g_free(ssid);
    (void)button;
}

static void wifi_password_focus_entered(GtkEventControllerFocus *controller,
                                        gpointer data)
{
    shell_keyboard_command("entry", "on");
    (void)controller;
    (void)data;
}

static void wifi_password_focus_left(GtkEventControllerFocus *controller,
                                     gpointer data)
{
    shell_keyboard_command("entry", "off");
    (void)controller;
    (void)data;
}

static void wifi_show_password(WifiPageState *state, const char *ssid)
{
    if (state->password_window != NULL || state->destroyed) {
        return;
    }
    GtkWidget *window = gtk_window_new();
    state->password_window = window;
    gtk_widget_add_css_class(window, "fieldm-menu");
    state->password_ssid = g_strdup(ssid);
    gtk_window_set_title(GTK_WINDOW(window), "Connect to Wi-Fi");
    gtk_window_set_transient_for(GTK_WINDOW(window), state->parent);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 300);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(content, 20);
    gtk_widget_set_margin_end(content, 20);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);
    GtkWidget *heading = gtk_label_new(ssid);
    gtk_widget_add_css_class(heading, "title-2");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    GtkWidget *entry = gtk_password_entry_new();
    state->password_entry = entry;
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter",
                     G_CALLBACK(wifi_password_focus_entered), state);
    g_signal_connect(focus, "leave",
                     G_CALLBACK(wifi_password_focus_left), state);
    gtk_widget_add_controller(entry, focus);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(actions, TRUE);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *connect = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(connect, "primary-action");
    gtk_widget_set_hexpand(cancel, TRUE);
    gtk_widget_set_hexpand(connect, TRUE);
    g_signal_connect(cancel, "clicked",
                     G_CALLBACK(wifi_password_cancel), state);
    g_signal_connect(connect, "clicked",
                     G_CALLBACK(wifi_password_connect), state);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(wifi_password_connect), state);
    gtk_box_append(GTK_BOX(actions), cancel);
    gtk_box_append(GTK_BOX(actions), connect);
    gtk_box_append(GTK_BOX(content), heading);
    gtk_box_append(GTK_BOX(content), entry);
    gtk_box_append(GTK_BOX(content), actions);
    gtk_window_set_child(GTK_WINDOW(window), content);
    g_signal_connect(window, "destroy",
                     G_CALLBACK(wifi_password_destroyed), state);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
}

static void wifi_network_activated(GtkListBox *list, GtkListBoxRow *row,
                                   gpointer data)
{
    WifiPageState *state = data;
    const char *ssid = g_object_get_data(G_OBJECT(row), "ssid");
    const char *security = g_object_get_data(G_OBJECT(row), "security");
    if (ssid == NULL || state->process != NULL) {
        return;
    }
    if (security != NULL && strcmp(security, "open") == 0) {
        const char *const arguments[] = {
            "/usr/bin/wifi", "connect", "--open", ssid, NULL
        };
        wifi_run(state, WIFI_OPERATION_CONNECT, arguments, NULL);
    } else {
        wifi_show_password(state, ssid);
    }
    (void)list;
}

static void wifi_forget_clicked(GtkButton *button, gpointer data)
{
    WifiPageState *state = data;
    const char *ssid = g_object_get_data(G_OBJECT(button), "ssid");
    if (ssid == NULL || state->process != NULL) return;
    char *ssid_copy = g_strdup(ssid);
    const char *const arguments[] = {"/usr/bin/wifi", "forget", ssid_copy, NULL};
    wifi_run(state, WIFI_OPERATION_FORGET, arguments, NULL);
    g_free(ssid_copy);
}

static void wifi_populate_saved(WifiPageState *state, const char *output)
{
    if (state->destroyed || state->saved_networks == NULL) return;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->saved_networks)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(state->saved_networks), child);
    char **lines = g_strsplit(output != NULL ? output : "", "\n", -1);
    int count = 0;
    for (size_t i = 0; lines[i] != NULL; ++i) {
        char **fields = g_strsplit(lines[i], "\t", 2);
        if (g_strv_length(fields) >= 2 && fields[1][0] != '\0') {
            GtkWidget *row = gtk_list_box_row_new();
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *name = gtk_label_new(fields[1]);
            gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
            gtk_widget_set_hexpand(name, TRUE);
            GtkWidget *forget = gtk_button_new_with_label("Forget");
            g_object_set_data_full(G_OBJECT(forget), "ssid",
                                   g_strdup(fields[1]), g_free);
            g_signal_connect(forget, "clicked",
                             G_CALLBACK(wifi_forget_clicked), state);
            gtk_box_append(GTK_BOX(box), name);
            gtk_box_append(GTK_BOX(box), forget);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
            gtk_list_box_append(GTK_LIST_BOX(state->saved_networks), row);
            ++count;
        }
        g_strfreev(fields);
    }
    g_strfreev(lines);
    if (count == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new("No saved networks.");
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_append(GTK_LIST_BOX(state->saved_networks), row);
    }
}

static void wifi_saved_refresh(WifiPageState *state)
{
    const char *const arguments[] = {"/usr/bin/wifi", "saved", NULL};
    wifi_run(state, WIFI_OPERATION_SAVED, arguments, NULL);
}

GtkWidget *wifi_page_new(GtkWindow *parent)
{
    WifiPageState *state = g_new0(WifiPageState, 1);
    state->parent = parent;

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(page, 20);
    gtk_widget_set_margin_end(page, 20);
    gtk_widget_set_margin_top(page, 18);
    gtk_widget_set_margin_bottom(page, 18);
    GtkWidget *heading = gtk_label_new("Wi-Fi");
    gtk_widget_add_css_class(heading, "title-2");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    GtkWidget *status = gtk_label_new("Checking Wi-Fi status...");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(status), TRUE);
    gtk_label_set_selectable(GTK_LABEL(status), TRUE);
    state->status = status;
    GtkWidget *scan = gtk_button_new_with_label("Scan");
    state->scan_button = scan;
    g_signal_connect(scan, "clicked", G_CALLBACK(wifi_scan), state);
    GtkWidget *networks = gtk_list_box_new();
    state->networks = networks;
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(networks),
                                    GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(networks), TRUE);
    g_signal_connect(networks, "row-activated",
                     G_CALLBACK(wifi_network_activated), state);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), networks);
    GtkWidget *saved_heading = gtk_label_new("Saved networks");
    gtk_widget_add_css_class(saved_heading, "title-3");
    gtk_label_set_xalign(GTK_LABEL(saved_heading), 0.0f);
    GtkWidget *saved_networks = gtk_list_box_new();
    state->saved_networks = saved_networks;
    GtkWidget *saved_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(saved_scroll, -1, 160);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(saved_scroll),
                                   saved_networks);
    GtkWidget *result = gtk_label_new("");
    state->result = result;
    gtk_label_set_xalign(GTK_LABEL(result), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(result), TRUE);
    gtk_label_set_selectable(GTK_LABEL(result), TRUE);
    gtk_widget_add_css_class(result, "muted");

    gtk_box_append(GTK_BOX(page), heading);
    gtk_box_append(GTK_BOX(page), status);
    gtk_box_append(GTK_BOX(page), scan);
    gtk_box_append(GTK_BOX(page), scroll);
    gtk_box_append(GTK_BOX(page), saved_heading);
    gtk_box_append(GTK_BOX(page), saved_scroll);
    gtk_box_append(GTK_BOX(page), result);
    g_signal_connect(page, "destroy", G_CALLBACK(wifi_page_destroyed), state);

    const char *const status_args[] = {"/usr/bin/wifi", "status", NULL};
    wifi_run(state, WIFI_OPERATION_STATUS, status_args, NULL);
    return page;
}
