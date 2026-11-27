#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { MAX_SEARCH_RESULTS = 200 };

typedef struct {
    char *name;
    char *description;
    int installed;
} PackageResult;

typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GtkWidget *query;
    GtkWidget *search_button;
    GtkWidget *update_button;
    GtkWidget *results;
    GtkWidget *status;
    GtkTextView *log;
    GPtrArray *packages;
    GSubprocess *apt_process;
    GDataInputStream *apt_output;
    int apt_process_done;
    int apt_output_done;
    int operation_active;
} PackageState;

static void set_busy(PackageState *state, int busy, const char *status)
{
    state->operation_active = busy;
    gtk_widget_set_sensitive(state->query, !busy);
    gtk_widget_set_sensitive(state->search_button, !busy);
    gtk_widget_set_sensitive(state->update_button, !busy);
    gtk_widget_set_sensitive(state->results, !busy);
    gtk_label_set_text(GTK_LABEL(state->status), status);
}

static void append_log(PackageState *state, const char *text)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(state->log);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
    gtk_text_view_scroll_to_iter(state->log, &end, 0.0, FALSE, 0.0, 1.0);
}

static void package_result_free(gpointer data)
{
    PackageResult *package = data;
    g_free(package->name);
    g_free(package->description);
    g_free(package);
}

static void packages_keyboard(const char *action)
{
    GError *error = NULL;
    char *arguments[] = {
        "/usr/bin/fieldm-keyboard", "entry", (char *)action, NULL
    };
    if (!g_spawn_async(NULL, arguments, NULL, G_SPAWN_DEFAULT,
                       NULL, NULL, NULL, &error)) {
        fprintf(stderr, "Could not update keyboard lease: %s\n",
                error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
    }
}

static void package_search_focus_entered(GtkEventControllerFocus *controller,
                                         gpointer data)
{
    packages_keyboard("on");
    (void)controller;
    (void)data;
}

static void package_search_focus_left(GtkEventControllerFocus *controller,
                                      gpointer data)
{
    packages_keyboard("off");
    (void)controller;
    (void)data;
}

static void clear_results(PackageState *state)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->results)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(state->results), child);
    }
    g_ptr_array_set_size(state->packages, 0);
}

static void package_action(GtkButton *button, gpointer data);

static void render_results(PackageState *state)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->results)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(state->results), child);
    }
    for (guint i = 0; i < state->packages->len; ++i) {
        PackageResult *package = g_ptr_array_index(state->packages, i);
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(content, 12);
        gtk_widget_set_margin_end(content, 12);
        gtk_widget_set_margin_top(content, 8);
        gtk_widget_set_margin_bottom(content, 8);
        GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_hexpand(text, TRUE);
        GtkWidget *name = gtk_label_new(package->name);
        gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
        gtk_widget_add_css_class(name, "package-name");
        GtkWidget *description = gtk_label_new(package->description);
        gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(description), TRUE);
        gtk_widget_add_css_class(description, "muted");
        GtkWidget *status = gtk_label_new(
            package->installed ? "Installed" : "Not installed");
        gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
        gtk_widget_add_css_class(status, "package-state");
        gtk_box_append(GTK_BOX(text), name);
        gtk_box_append(GTK_BOX(text), description);
        gtk_box_append(GTK_BOX(text), status);
        GtkWidget *action = gtk_button_new_with_label(
            package->installed ? "Remove" : "Install");
        gtk_widget_add_css_class(action, "package-action");
        if (!package->installed) {
            gtk_widget_add_css_class(action, "suggested-action");
        }
        g_object_set_data_full(G_OBJECT(action), "package-name",
                               g_strdup(package->name), g_free);
        g_object_set_data(G_OBJECT(action), "package-installed",
                          GINT_TO_POINTER(package->installed));
        g_signal_connect(action, "clicked",
                         G_CALLBACK(package_action), state);
        gtk_box_append(GTK_BOX(content), text);
        gtk_box_append(GTK_BOX(content), action);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        gtk_list_box_append(GTK_LIST_BOX(state->results), row);
    }
}

static void search_installed_finished(GObject *source, GAsyncResult *result,
                                      gpointer data)
{
    PackageState *state = data;
    char *output = NULL;
    GError *error = NULL;
    gboolean communicated = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), result, &output, NULL, &error);
    if (communicated) {
        char **lines = g_strsplit(output != NULL ? output : "", "\n", -1);
        for (size_t i = 0; lines[i] != NULL; ++i) {
            char **fields = g_strsplit(lines[i], "\t", 2);
            if (fields[0] != NULL && fields[0][0] != '\0' &&
                fields[1] != NULL && strcmp(fields[1], "installed") == 0) {
                for (guint j = 0; j < state->packages->len; ++j) {
                    PackageResult *package =
                        g_ptr_array_index(state->packages, j);
                    if (strcmp(package->name, fields[0]) == 0) {
                        package->installed = 1;
                        break;
                    }
                }
            }
            g_strfreev(fields);
        }
        g_strfreev(lines);
    }
    g_object_unref(source);
    g_free(output);
    g_clear_error(&error);
    render_results(state);
    char *status = g_strdup_printf("%u package%s found.",
                                   state->packages->len,
                                   state->packages->len == 1 ? "" : "s");
    set_busy(state, 0, status);
    g_free(status);
}

static void search_finished(GObject *source, GAsyncResult *result,
                            gpointer data)
{
    PackageState *state = data;
    char *output = NULL;
    GError *error = NULL;
    gboolean communicated = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), result, &output, NULL, &error);
    if (!communicated || !g_subprocess_get_successful(G_SUBPROCESS(source))) {
        set_busy(state, 0,
                 error != NULL ? error->message : "Package search failed.");
        g_object_unref(source);
        g_free(output);
        g_clear_error(&error);
        return;
    }
    g_object_unref(source);

    clear_results(state);
    char **lines = g_strsplit(output != NULL ? output : "", "\n", -1);
    for (size_t i = 0; lines[i] != NULL &&
                       state->packages->len < MAX_SEARCH_RESULTS; ++i) {
        char *separator = strstr(lines[i], " - ");
        if (separator == NULL || separator == lines[i]) {
            continue;
        }
        *separator = '\0';
        PackageResult *package = g_new0(PackageResult, 1);
        package->name = g_strdup(g_strstrip(lines[i]));
        package->description = g_strdup(g_strstrip(separator + 3));
        g_ptr_array_add(state->packages, package);
    }
    g_strfreev(lines);
    g_free(output);
    g_clear_error(&error);

    if (state->packages->len == 0) {
        render_results(state);
        set_busy(state, 0, "No packages found.");
        return;
    }
    GPtrArray *arguments =
        g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(arguments, g_strdup("dpkg-query"));
    g_ptr_array_add(arguments, g_strdup("-W"));
    g_ptr_array_add(arguments,
                    g_strdup("-f=${binary:Package}\t${db:Status-Status}\n"));
    for (guint i = 0; i < state->packages->len; ++i) {
        PackageResult *package = g_ptr_array_index(state->packages, i);
        g_ptr_array_add(arguments, g_strdup(package->name));
    }
    g_ptr_array_add(arguments, NULL);
    GSubprocess *installed = g_subprocess_newv(
        (const char * const *)arguments->pdata,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &error);
    if (installed == NULL) {
        set_busy(state, 0, "Could not inspect installed packages.");
        g_ptr_array_free(arguments, TRUE);
        g_clear_error(&error);
        render_results(state);
        return;
    }
    g_ptr_array_free(arguments, TRUE);
    g_subprocess_communicate_utf8_async(
        installed, NULL, NULL, search_installed_finished, state);
}

static void package_search(PackageState *state)
{
    if (state->operation_active) {
        return;
    }
    const char *query = gtk_editable_get_text(GTK_EDITABLE(state->query));
    const char *const arguments[] = {
        "apt-cache", "search", query, NULL
    };
    GError *error = NULL;
    GSubprocess *process = g_subprocess_newv(
        arguments, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                       G_SUBPROCESS_FLAGS_STDERR_MERGE, &error);
    if (process == NULL) {
        set_busy(state, 0,
                 error != NULL ? error->message : "Could not start apt-cache.");
        g_clear_error(&error);
        return;
    }
    set_busy(state, 1, "Searching packages...");
    g_subprocess_communicate_utf8_async(
        process, NULL, NULL, search_finished, state);
}

static void package_search_clicked(GtkButton *button, gpointer data)
{
    package_search(data);
    (void)button;
}

static void package_search_activate(GtkEntry *entry, gpointer data)
{
    package_search(data);
    (void)entry;
}

static void package_operation_finished(PackageState *state)
{
    if (!state->apt_process_done || !state->apt_output_done) {
        return;
    }
    int success = g_subprocess_get_successful(state->apt_process);
    g_clear_object(&state->apt_output);
    g_clear_object(&state->apt_process);
    append_log(state, success ? "\nOperation completed.\n"
                              : "\nOperation failed.\n");
    set_busy(state, 0, success ? "Operation completed."
                               : "Operation failed; see the log.");
    if (success) {
        package_search(state);
    }
}

static void package_process_waited(GObject *source, GAsyncResult *result,
                                   gpointer data)
{
    PackageState *state = data;
    GError *error = NULL;
    if (!g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error)) {
        char *message = g_strdup_printf("%s\n",
                                        error != NULL ? error->message
                                                     : "apt wait failed.\n");
        append_log(state, message);
        g_free(message);
    }
    g_clear_error(&error);
    state->apt_process_done = 1;
    package_operation_finished(state);
}

static void package_read_line(GDataInputStream *stream, GAsyncResult *result,
                              gpointer data);

static void package_read_output(PackageState *state)
{
    g_data_input_stream_read_line_async(
        state->apt_output, G_PRIORITY_DEFAULT, NULL,
        (GAsyncReadyCallback)package_read_line, state);
}

static void package_read_line(GDataInputStream *stream, GAsyncResult *result,
                              gpointer data)
{
    PackageState *state = data;
    gsize length = 0;
    GError *error = NULL;
    char *line = g_data_input_stream_read_line_finish(
        stream, result, &length, &error);
    if (line != NULL) {
        append_log(state, line);
        append_log(state, "\n");
        g_free(line);
        package_read_output(state);
    } else {
        if (error != NULL) {
            append_log(state, error->message);
            append_log(state, "\n");
        }
        g_clear_error(&error);
        state->apt_output_done = 1;
        package_operation_finished(state);
    }
}

static int run_apt_operation(PackageState *state,
                             const char * const *arguments,
                             const char *status)
{
    if (state->operation_active) {
        return 0;
    }
    GError *error = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_setenv(launcher, "DEBIAN_FRONTEND",
                                 "noninteractive", TRUE);
    g_subprocess_launcher_setenv(launcher, "APT_LISTCHANGES_FRONTEND",
                                 "none", TRUE);
    GSubprocess *process =
        g_subprocess_launcher_spawnv(launcher, arguments, &error);
    g_object_unref(launcher);
    if (process == NULL) {
        append_log(state, error != NULL ? error->message
                                        : "Could not start apt.");
        append_log(state, "\n");
        g_clear_error(&error);
        set_busy(state, 0, "Could not start apt; see the log.");
        return 0;
    }
    state->apt_process = process;
    GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(process);
    state->apt_output = g_data_input_stream_new(stdout_stream);
    state->apt_process_done = 0;
    state->apt_output_done = 0;
    append_log(state, "\n--- ");
    append_log(state, status);
    append_log(state, " ---\n");
    set_busy(state, 1, status);
    g_subprocess_wait_async(process, NULL,
                            package_process_waited, state);
    package_read_output(state);
    return 1;
}

static void package_confirmed(GObject *source, GAsyncResult *result,
                              gpointer data)
{
    PackageState *state = data;
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    const char *package = g_object_get_data(G_OBJECT(dialog), "package");
    int installed = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(dialog), "installed"));
    GError *error = NULL;
    int response = gtk_alert_dialog_choose_finish(dialog, result, &error);
    if (response == 1) {
        char *status = g_strdup_printf("%s %s...",
                                       installed ? "Removing" : "Installing",
                                       package);
        if (installed) {
            const char *const arguments[] = {
                "apt-get", "-y", "remove", package, NULL
            };
            run_apt_operation(state, arguments, status);
        } else {
            const char *const arguments[] = {
                "apt-get", "-y", "install", "--no-install-recommends",
                package, NULL
            };
            run_apt_operation(state, arguments, status);
        }
        g_free(status);
    }
    g_clear_error(&error);
}

static void package_action(GtkButton *button, gpointer data)
{
    PackageState *state = data;
    if (state->operation_active) {
        return;
    }
    const char *package = g_object_get_data(G_OBJECT(button), "package-name");
    int installed = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "package-installed"));
    char *message = g_strdup_printf(
        "%s package \"%s\"? This changes the installed system.",
        installed ? "Remove" : "Install", package);
    const char *buttons[] = {"Cancel", installed ? "Remove" : "Install", NULL};
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    g_object_set_data_full(G_OBJECT(dialog), "package",
                           g_strdup(package), g_free);
    g_object_set_data(G_OBJECT(dialog), "installed",
                      GINT_TO_POINTER(installed));
    gtk_alert_dialog_choose(dialog, state->window, NULL,
                            package_confirmed, state);
    g_object_unref(dialog);
    g_free(message);
}

static void update_clicked(GtkButton *button, gpointer data)
{
    PackageState *state = data;
    if (getuid() != 0) {
        set_busy(state, 0, "Run Packages as root to change system packages.");
        return;
    }
    const char *const arguments[] = {"apt-get", "update", NULL};
    run_apt_operation(state, arguments, "Updating package lists");
    (void)button;
}

static gboolean window_closed(GtkWindow *window, gpointer data)
{
    PackageState *state = data;
    if (state->operation_active) {
        return GDK_EVENT_STOP;
    }
    (void)window;
    return GDK_EVENT_PROPAGATE;
}

static void activate(GtkApplication *application, gpointer data)
{
    PackageState *state = data;
    state->application = application;
    state->packages = g_ptr_array_new_with_free_func(package_result_free);
    GtkWidget *window = gtk_application_window_new(application);
    state->window = GTK_WINDOW(window);
    gtk_window_set_title(state->window, "Packages");
    gtk_window_set_default_size(state->window, 700, 1100);

    GtkCssProvider *css = gtk_css_provider_new();
    const char *css_path = g_file_test(
        "/usr/share/fieldm/fieldm.css", G_FILE_TEST_IS_REGULAR)
        ? "/usr/share/fieldm/fieldm.css"
        : "/mnt/zirarch/zirarch/FieldM/data/fieldm.css";
    gtk_css_provider_load_from_path(css, css_path);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(css);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    GtkWidget *heading = gtk_label_new("Packages");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_widget_add_css_class(heading, "title-1");
    GtkWidget *search = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *entry = gtk_search_entry_new();
    state->query = entry;
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_search_entry_set_placeholder_text(
        GTK_SEARCH_ENTRY(entry), "Search package names and descriptions");
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter",
                     G_CALLBACK(package_search_focus_entered), state);
    g_signal_connect(focus, "leave",
                     G_CALLBACK(package_search_focus_left), state);
    gtk_widget_add_controller(entry, focus);
    GtkWidget *search_button = gtk_button_new_with_label("Search");
    state->search_button = search_button;
    g_signal_connect(entry, "activate",
                     G_CALLBACK(package_search_activate), state);
    g_signal_connect(search_button, "clicked",
                     G_CALLBACK(package_search_clicked), state);
    gtk_box_append(GTK_BOX(search), entry);
    gtk_box_append(GTK_BOX(search), search_button);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *status = gtk_label_new("Ready.");
    state->status = status;
    gtk_widget_set_hexpand(status, TRUE);
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    GtkWidget *update = gtk_button_new_with_label("Update lists");
    state->update_button = update;
    g_signal_connect(update, "clicked", G_CALLBACK(update_clicked), state);
    gtk_box_append(GTK_BOX(toolbar), status);
    gtk_box_append(GTK_BOX(toolbar), update);

    GtkWidget *results = gtk_list_box_new();
    state->results = results;
    GtkWidget *results_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(results_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(results_scroll),
                                  results);
    GtkWidget *log_label = gtk_label_new("Operation log");
    gtk_label_set_xalign(GTK_LABEL(log_label), 0.0f);
    GtkWidget *log = gtk_text_view_new();
    state->log = GTK_TEXT_VIEW(log);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log), GTK_WRAP_WORD_CHAR);
    GtkWidget *log_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(log_scroll),
                                                260);
    gtk_widget_set_vexpand(log_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), log);

    gtk_box_append(GTK_BOX(content), heading);
    gtk_box_append(GTK_BOX(content), search);
    gtk_box_append(GTK_BOX(content), toolbar);
    gtk_box_append(GTK_BOX(content), results_scroll);
    gtk_box_append(GTK_BOX(content), log_label);
    gtk_box_append(GTK_BOX(content), log_scroll);
    gtk_window_set_child(state->window, content);
    g_signal_connect(state->window, "close-request",
                     G_CALLBACK(window_closed), state);
    gtk_window_present(state->window);
    gtk_widget_grab_focus(entry);
}

int main(int argc, char **argv)
{
    if (getuid() != 0) {
        fprintf(stderr,
                "fieldm-packages must run as root on this lab device.\n");
        return 1;
    }
    PackageState state = {0};
    GtkApplication *application = gtk_application_new(
        "org.zirtualz.field-packages", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), &state);
    int result = g_application_run(G_APPLICATION(application), argc, argv);
    packages_keyboard("off");
    g_clear_pointer(&state.packages, g_ptr_array_unref);
    g_clear_object(&application);
    return result;
}
