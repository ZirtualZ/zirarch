#include "gtk_shell_internal.h"
#include "wifi.h"
#include "../theme.h"

#include <X11/Xlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <time.h>

static void back_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *window = data;
    GtkStack *stack = g_object_get_data(G_OBJECT(window), "settings-stack");
    if (stack != NULL &&
        strcmp(gtk_stack_get_visible_child_name(stack), "menu") != 0) {
        gtk_stack_set_visible_child_name(stack, "menu");
        return;
    }
    shell_keyboard_command("entry", "off");
    gtk_widget_set_visible(window, FALSE);
    (void)button;
}

static gboolean settings_close_requested(GtkWindow *window, gpointer data)
{
    shell_keyboard_command("entry", "off");
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    (void)data;
    return TRUE;
}

static gboolean settings_key(GtkEventControllerKey *controller,
                             guint keyval, guint keycode,
                             GdkModifierType modifiers, gpointer data)
{
    (void)controller; (void)keycode; (void)modifiers;
    if (keyval == GDK_KEY_Escape) {
        GtkWidget *window = data;
        GtkStack *stack = g_object_get_data(G_OBJECT(window), "settings-stack");
        if (stack != NULL &&
            strcmp(gtk_stack_get_visible_child_name(stack), "menu") != 0) {
            gtk_stack_set_visible_child_name(stack, "menu");
        } else {
            back_clicked(NULL, data);
        }
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static GtkWidget *heading(const char *title)
{
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "title-2");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    return label;
}

static GtkWidget *row(const char *name)
{
    GtkWidget *widget = gtk_button_new_with_label(name);
    gtk_widget_add_css_class(widget, "settings-row");
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    return widget;
}

static GtkWidget *string_dropdown(GtkStringList *model)
{
    GtkExpression *expression = gtk_property_expression_new(
        GTK_TYPE_STRING_OBJECT, NULL, "string");
    return gtk_drop_down_new(G_LIST_MODEL(model), expression);
}

static GtkWidget *dropdown_with_value(GtkWidget *dropdown,
                                      const char *value, int width)
{
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), dropdown);
    GtkWidget *label = gtk_label_new(value);
    gtk_widget_add_css_class(label, "dropdown-value");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 14);
    gtk_widget_set_margin_end(label, 36);
    gtk_widget_set_can_target(label, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), label);
    g_object_set_data(G_OBJECT(dropdown), "dropdown-value-label", label);
    gtk_widget_set_size_request(overlay, width, 56);
    return overlay;
}

static void packages_clicked(GtkButton *button, gpointer data)
{
    shell_launch_action(data, SHELL_ACTION_PACKAGES);
    (void)button;
}

static void display_apply(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    const char *brightness = g_object_get_data(G_OBJECT(button), "brightness");
    const char *dpi = g_object_get_data(G_OBJECT(button), "dpi");
    if (brightness != NULL) {
        FILE *file = fopen("/sys/class/backlight/panel/brightness", "w");
        int failed = file == NULL;
        if (file != NULL) {
            if (fprintf(file, "%s", brightness) < 0) failed = 1;
            if (fclose(file) != 0) failed = 1;
        }
        if (failed) {
            shell_show_error(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                             "Could not apply display brightness.");
        }
    }
    if (dpi != NULL) {
        char path[1200];
        const char *home = g_get_home_dir();
        snprintf(path, sizeof(path), "%s/.Xresources", home);
        int failed = !fieldm_apply_xresources(state->dark_theme, dpi);
        if (failed) {
            shell_show_error(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                             "Could not save text-size settings.");
        } else {
            Display *display = XOpenDisplay(NULL);
            if (display != NULL) {
                char resource[1200];
                int length = snprintf(resource, sizeof(resource),
                                      "Xft.dpi: %s\n"
                                      "Xft.antialias: 1\n"
                                      "Xft.hinting: 1\n", dpi);
                Atom property = XInternAtom(display, "RESOURCE_MANAGER", False);
                Atom string = XInternAtom(display, "STRING", False);
                XChangeProperty(display, DefaultRootWindow(display), property,
                                string, 8, PropModeReplace,
                                (unsigned char *)resource, length);
                XFlush(display);
                XCloseDisplay(display);
            }
        }
    }
}

static void theme_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    int dark = g_object_get_data(G_OBJECT(button), "dark-theme") != NULL;
    shell_set_theme(state, dark);
}

static GtkWidget *display_page(ShellState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(page), heading("Display"));
    gtk_box_append(GTK_BOX(page), gtk_label_new("Theme"));
    GtkWidget *themes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *light = gtk_button_new_with_label("Light");
    GtkWidget *dark = gtk_button_new_with_label("Dark");
    g_object_set_data(G_OBJECT(light), "dark-theme", NULL);
    g_object_set_data(G_OBJECT(dark), "dark-theme", GINT_TO_POINTER(1));
    g_signal_connect(light, "clicked", G_CALLBACK(theme_clicked), state);
    g_signal_connect(dark, "clicked", G_CALLBACK(theme_clicked), state);
    gtk_box_append(GTK_BOX(themes), light);
    gtk_box_append(GTK_BOX(themes), dark);
    gtk_box_append(GTK_BOX(page), themes);
    gtk_box_append(GTK_BOX(page), row("Brightness"));
    for (int i = 0; i < 3; ++i) {
        GtkWidget *button = gtk_button_new_with_label(
            i == 0 ? "Brightness 25%" : i == 1 ? "Brightness 50%" : "Brightness 100%");
        g_object_set_data_full(G_OBJECT(button), "brightness",
                               g_strdup(i == 0 ? "34" : i == 1 ? "67" : "134"),
                               g_free);
        g_signal_connect(button, "clicked", G_CALLBACK(display_apply), state);
        gtk_box_append(GTK_BOX(page), button);
    }
    gtk_box_append(GTK_BOX(page), gtk_label_new("Text size"));
    const char *dpis[] = {"144", "168", "192"};
    for (int i = 0; i < 3; ++i) {
        GtkWidget *button = gtk_button_new_with_label(dpis[i]);
        g_object_set_data_full(G_OBJECT(button), "dpi",
                               g_strdup(dpis[i]), g_free);
        g_signal_connect(button, "clicked", G_CALLBACK(display_apply), state);
        gtk_box_append(GTK_BOX(page), button);
    }
    return page;
}

typedef struct {
    GtkWidget *dropdowns[7];
} KeyboardSettingsWidgets;

static const char *keyboard_dropdown_value(GtkWidget *dropdown)
{
    const char *const *values = g_object_get_data(
        G_OBJECT(dropdown), "keyboard-setting-values");
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    return values != NULL && values[selected] != NULL ? values[selected] : "";
}

static void save_keyboard_settings(GtkWidget *page)
{
    KeyboardSettingsWidgets *widgets = g_object_get_data(
        G_OBJECT(page), "keyboard-settings");
    const char *home = g_get_home_dir();
    char directory[1200], path[1200];
    if (widgets == NULL || home == NULL ||
        snprintf(directory, sizeof(directory), "%s/.config/fieldm", home) >=
            (int)sizeof(directory) ||
        snprintf(path, sizeof(path), "%s/keyboard.conf", directory) >=
            (int)sizeof(path) ||
        g_mkdir_with_parents(directory, 0700) != 0) {
        shell_show_error(GTK_WINDOW(gtk_widget_get_root(page)),
                         "Could not create keyboard settings.");
        return;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        shell_show_error(GTK_WINDOW(gtk_widget_get_root(page)),
                         "Could not write keyboard settings.");
        return;
    }
    static const char *const keys[] = {
        "key_height", "number_row", "terminal_row", "long_press_ms",
        "repeat_speed", "auto_show", "highlight"
    };
    int failed = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(keys); ++i) {
        if (fprintf(file, "%s=%s\n", keys[i],
                    keyboard_dropdown_value(widgets->dropdowns[i])) < 0) {
            failed = 1;
            break;
        }
    }
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        shell_show_error(GTK_WINDOW(gtk_widget_get_root(page)),
                         "Could not write keyboard settings.");
        return;
    }
    FILE *pidfile = fopen("/run/fieldm-keyboard/pid", "r");
    pid_t pid = 0;
    if (pidfile != NULL) {
        if (fscanf(pidfile, "%d", &pid) == 1 && pid > 1 &&
            kill(pid, SIGHUP) != 0 && errno != ESRCH) {
            perror("Could not reload keyboard settings");
        }
        fclose(pidfile);
    }
}

static void keyboard_setting_changed(GObject *object, GParamSpec *property,
                                     gpointer data)
{
    GtkStringObject *selected = gtk_drop_down_get_selected_item(
        GTK_DROP_DOWN(object));
    GtkWidget *label = g_object_get_data(object, "dropdown-value-label");
    if (selected != NULL && label != NULL) {
        gtk_label_set_text(GTK_LABEL(label),
                           gtk_string_object_get_string(selected));
    }
    save_keyboard_settings(data);
    (void)property;
}

static GtkWidget *keyboard_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(page), heading("Keyboard"));
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);
    gtk_widget_set_margin_top(page, 12);
    gtk_widget_set_margin_bottom(page, 12);
    const char *home = g_get_home_dir();
    char config_path[1200];
    snprintf(config_path, sizeof(config_path),
             "%s/.config/fieldm/keyboard.conf", home);
    char values[7][64] = {
        "64", "off", "only-terminal", "400", "normal", "on", "on"
    };
    FILE *file = fopen(config_path, "r");
    if (file != NULL) {
        char line[160], key[64], value[64];
        while (fgets(line, sizeof(line), file) != NULL) {
            if (sscanf(line, "%63[^=]=%63s", key, value) != 2) continue;
            const char *keys[] = {"key_height", "number_row", "terminal_row",
                                  "long_press_ms", "repeat_speed", "auto_show",
                                  "highlight"};
            for (size_t i = 0; i < G_N_ELEMENTS(keys); ++i) {
                if (strcmp(key, keys[i]) == 0) {
                    g_strlcpy(values[i], value, sizeof(values[i]));
                    break;
                }
            }
        }
        fclose(file);
    }

    static const char *const labels[] = {
        "Key height", "Number row", "Terminal row", "Long-press delay",
        "Repeat speed", "Auto-show keyboard", "Key-press highlight"
    };
    static const char *const options[][4] = {
        {"Compact (56 px)", "Normal (64 px)", "Large (72 px)", NULL},
        {"Off", "On", NULL},
        {"Always", "Only in terminal apps", "Off", NULL},
        {"300 ms", "400 ms", "600 ms", NULL},
        {"Slow", "Normal", "Fast", NULL},
        {"On", "Off", NULL},
        {"On", "Off", NULL}
    };
    static const char *const option_values[][4] = {
        {"56", "64", "72", NULL},
        {"off", "on", NULL},
        {"always", "only-terminal", "off", NULL},
        {"300", "400", "600", NULL},
        {"slow", "normal", "fast", NULL},
        {"on", "off", NULL},
        {"on", "off", NULL}
    };
    KeyboardSettingsWidgets *settings = g_new0(KeyboardSettingsWidgets, 1);
    g_object_set_data_full(G_OBJECT(page), "keyboard-settings",
                           settings, g_free);
    for (size_t i = 0; i < G_N_ELEMENTS(labels); ++i) {
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *label = gtk_label_new(labels[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_widget_set_hexpand(label, TRUE);
        GtkStringList *model = gtk_string_list_new(NULL);
        guint selected = 0;
        for (size_t j = 0; options[i][j] != NULL; ++j) {
            gtk_string_list_append(model, options[i][j]);
            if (strcmp(values[i], option_values[i][j]) == 0) {
                selected = (guint)j;
            }
        }
        GtkWidget *dropdown = string_dropdown(model);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), selected);
        g_object_set_data(G_OBJECT(dropdown), "keyboard-setting-values",
                          (gpointer)option_values[i]);
        settings->dropdowns[i] = dropdown;
        gtk_box_append(GTK_BOX(line), label);
        gtk_box_append(GTK_BOX(line), dropdown_with_value(
            dropdown, options[i][selected], 300));
        gtk_box_append(GTK_BOX(page), line);
        g_signal_connect(dropdown, "notify::selected",
                         G_CALLBACK(keyboard_setting_changed), page);
    }
    return page;
}

static GtkWidget *storage_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(page), heading("Storage"));
    struct statvfs info;
    char text[128];
    if (statvfs("/", &info) == 0 && info.f_blocks != 0) {
        unsigned long long total = info.f_blocks * info.f_frsize;
        unsigned long long used = (info.f_blocks - info.f_bfree) * info.f_frsize;
        snprintf(text, sizeof(text), "%llu MiB used of %llu MiB",
                 used >> 20, total >> 20);
    } else {
        snprintf(text, sizeof(text), "Root disk usage unavailable");
    }
    gtk_box_append(GTK_BOX(page), gtk_label_new(text));
    return page;
}

static void save_app_display_sizes(ShellState *state)
{
    const char *home = g_get_home_dir();
    char directory[1200], path[1200];
    if (snprintf(directory, sizeof(directory), "%s/.config/fieldm", home) >=
            (int)sizeof(directory) ||
        snprintf(path, sizeof(path), "%s/app-display.conf", directory) >=
            (int)sizeof(path) ||
        g_mkdir_with_parents(directory, 0700) != 0) {
        shell_show_error(GTK_WINDOW(state->settings),
                         "Could not create app display settings.");
        return;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        shell_show_error(GTK_WINDOW(state->settings),
                         "Could not write app display settings.");
        return;
    }
    int failed = 0;
    for (size_t i = 0; i < state->application_count; ++i) {
        const Application *app = &state->applications[i];
        if (app->desktop_id[0] != '\0' && app->display_size[0] != '\0' &&
            fprintf(file, "%s=%s\n", app->desktop_id,
                    app->display_size) < 0) {
            failed = 1;
            break;
        }
    }
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        shell_show_error(GTK_WINDOW(state->settings),
                         "Could not write app display settings.");
    }
}

static void app_display_changed(GObject *object, GParamSpec *property,
                                gpointer data)
{
    ShellState *state = data;
    guint index = GPOINTER_TO_UINT(
        g_object_get_data(object, "application-index"));
    GtkDropDown *dropdown = GTK_DROP_DOWN(object);
    GtkStringObject *selected = gtk_drop_down_get_selected_item(dropdown);
    if (index < state->application_count && selected != NULL) {
        const char *label = gtk_string_object_get_string(selected);
        GtkWidget *value_label = g_object_get_data(
            object, "dropdown-value-label");
        if (value_label != NULL) {
            gtk_label_set_text(GTK_LABEL(value_label), label);
        }
        const char *value = strcmp(label, "Small") == 0 ? "small" :
                            strcmp(label, "Large") == 0 ? "large" : "normal";
        g_strlcpy(state->applications[index].display_size, value,
                  sizeof(state->applications[index].display_size));
        save_app_display_sizes(state);
    }
    (void)property;
}

static GtkWidget *apps_page(ShellState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);
    gtk_widget_set_margin_top(page, 12);
    gtk_widget_set_margin_bottom(page, 12);
    gtk_box_append(GTK_BOX(page), heading("Apps"));
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *list = gtk_list_box_new();
    for (size_t i = 0; i < state->application_count; ++i) {
        Application *app = &state->applications[i];
        GtkWidget *item = gtk_list_box_row_new();
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *name = gtk_label_new(app->name);
        gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
        gtk_widget_set_hexpand(name, TRUE);
        const char *options[] = {"Small", "Normal", "Large", NULL};
        GtkStringList *model = gtk_string_list_new(options);
        GtkWidget *dropdown = string_dropdown(model);
        guint selected = strcmp(app->display_size, "small") == 0 ? 0 :
                         strcmp(app->display_size, "large") == 0 ? 2 : 1;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), selected);
        g_object_set_data(G_OBJECT(dropdown), "application-index",
                          GUINT_TO_POINTER((guint)i));
        g_signal_connect(dropdown, "notify::selected",
                         G_CALLBACK(app_display_changed), state);
        GtkWidget *dropdown_value = dropdown_with_value(
            dropdown, options[selected], 152);
        gtk_box_append(GTK_BOX(line), name);
        gtk_box_append(GTK_BOX(line), dropdown_value);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(item), line);
        gtk_list_box_append(GTK_LIST_BOX(list), item);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(page), scroll);
    GtkWidget *packages = row("Open Packages");
    g_signal_connect(packages, "clicked",
                     G_CALLBACK(packages_clicked), state);
    gtk_box_append(GTK_BOX(page), packages);
    return page;
}

static GtkWidget *battery_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);
    gtk_widget_set_margin_top(page, 12);
    gtk_box_append(GTK_BOX(page), heading("Battery"));
    GDir *directory = g_dir_open("/sys/class/power_supply", 0, NULL);
    gboolean found = FALSE;
    if (directory != NULL) {
        const char *entry;
        while ((entry = g_dir_read_name(directory)) != NULL) {
            char path[512], type[64] = "", status[64] = "",
                 capacity[32] = "", voltage[32] = "", temperature[32] = "";
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", entry);
            FILE *file = fopen(path, "r");
            if (file == NULL) continue;
            fgets(type, sizeof(type), file);
            fclose(file);
            if (strncmp(type, "Battery", 7) != 0) continue;
            snprintf(path, sizeof(path),
                     "/sys/class/power_supply/%s/capacity", entry);
            file = fopen(path, "r");
            if (file != NULL) { fgets(capacity, sizeof(capacity), file); fclose(file); }
            snprintf(path, sizeof(path),
                     "/sys/class/power_supply/%s/status", entry);
            file = fopen(path, "r");
            if (file != NULL) { fgets(status, sizeof(status), file); fclose(file); }
            snprintf(path, sizeof(path),
                     "/sys/class/power_supply/%s/voltage_now", entry);
            file = fopen(path, "r");
            if (file != NULL) { fgets(voltage, sizeof(voltage), file); fclose(file); }
            snprintf(path, sizeof(path),
                     "/sys/class/power_supply/%s/temp", entry);
            file = fopen(path, "r");
            if (file != NULL) { fgets(temperature, sizeof(temperature), file); fclose(file); }
            char text[320];
            snprintf(text, sizeof(text),
                     "%s%% — %s\nVoltage: %s uV\nTemperature: %s deci-C",
                     g_strstrip(capacity), g_strstrip(status),
                     g_strstrip(voltage), g_strstrip(temperature));
            gtk_box_append(GTK_BOX(page), gtk_label_new(text));
            found = TRUE;
        }
        g_dir_close(directory);
    }
    if (!found) gtk_box_append(GTK_BOX(page), gtk_label_new("Battery status unavailable"));
    return page;
}

typedef struct {
    GtkWidget *page;
    GtkWidget *list;
    GtkWidget *run_all;
    GtkWidget *busy;
    GSubprocess *process;
} HardwarePageState;

static void hardware_show_detail(GtkWidget *widget, gpointer data)
{
    (void)data;
    const char *text = g_object_get_data(G_OBJECT(widget), "detail");
    shell_show_error(GTK_WINDOW(gtk_widget_get_root(widget)),
                     text != NULL ? text : "No details available.");
}

static void hardware_set_result(HardwarePageState *state, const char *line)
{
    char **fields = g_strsplit(line, "\t", 4);
    if (g_strv_length(fields) >= 4) {
        GtkWidget *item = NULL;
        for (GtkWidget *child = gtk_widget_get_first_child(state->list);
             child != NULL;
             child = gtk_widget_get_next_sibling(child)) {
            const char *id = g_object_get_data(G_OBJECT(child), "probe-id");
            if (id != NULL && strcmp(id, fields[0]) == 0) {
                item = child;
                break;
            }
        }
        if (item != NULL) {
            GtkWidget *status = g_object_get_data(G_OBJECT(item),
                                                   "probe-status");
            GtkWidget *detail = g_object_get_data(G_OBJECT(item),
                                                   "probe-detail");
            gtk_label_set_text(GTK_LABEL(status), fields[1]);
            g_object_set_data_full(G_OBJECT(detail), "detail",
                                   g_strdup(fields[3]), g_free);
        }
    }
    g_strfreev(fields);
}

static void hardware_probe_done(GObject *source, GAsyncResult *result,
                                gpointer data)
{
    HardwarePageState *state = data;
    char *stdout_text = NULL, *stderr_text = NULL;
    GError *error = NULL;
    if (!g_subprocess_communicate_utf8_finish(
            G_SUBPROCESS(source), result, &stdout_text, &stderr_text, &error)) {
        gtk_label_set_text(GTK_LABEL(state->busy), "Hardware probe failed.");
    } else {
        char **lines = g_strsplit(stdout_text, "\n", -1);
        for (size_t i = 0; lines[i] != NULL; ++i) {
            if (lines[i][0] != '\0') hardware_set_result(state, lines[i]);
        }
        g_strfreev(lines);
        if (error == NULL && stderr_text != NULL && stderr_text[0] != '\0')
            gtk_label_set_text(GTK_LABEL(state->busy), stderr_text);
        else
            gtk_label_set_text(GTK_LABEL(state->busy),
                               "Complete. Details are read-only and time-limited.");
    }
    g_clear_error(&error);
    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_object(&state->process);
    gtk_widget_set_sensitive(state->run_all, TRUE);
}

static int hardware_start(HardwarePageState *state, const char *probe)
{
    if (state->process != NULL) return 0;
    GError *error = NULL;
    const char *arguments[] = {
        "/usr/lib/fieldm/hwprobe",
        "--log",
        probe == NULL ? "all" : probe,
        NULL
    };
    state->process = g_subprocess_newv((const gchar *const *)arguments,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &error);
    if (state->process == NULL) {
        char *message = g_strdup_printf("Could not start hardware probe: %s",
                                        error != NULL ? error->message :
                                        "unknown error");
        gtk_label_set_text(GTK_LABEL(state->busy), message);
        g_free(message);
        g_clear_error(&error);
        return 0;
    }
    gtk_widget_set_sensitive(state->run_all, FALSE);
    gtk_label_set_text(GTK_LABEL(state->busy), "Running read-only probes…");
    g_subprocess_communicate_utf8_async(state->process, NULL, NULL,
                                        hardware_probe_done, state);
    return 1;
}

static void hardware_run_all(GtkButton *button, gpointer data)
{
    (void)button;
    hardware_start(data, NULL);
}

static void hardware_run_one(GtkButton *button, gpointer data)
{
    const char *id = g_object_get_data(G_OBJECT(button), "probe-id");
    hardware_start(data, id);
}

static void hardware_export(GtkButton *button, gpointer data)
{
    HardwarePageState *state = data;
    char date[32];
    time_t now = time(NULL);
    struct tm utc = {0};
    gmtime_r(&now, &utc);
    strftime(date, sizeof(date), "%Y%m%d-%H%M%S", &utc);
    char *destination_name = g_strdup_printf("/root/hwtest-%s.txt", date);
    GFile *source = g_file_new_for_path("/var/log/fieldm-hwtest.txt");
    GFile *destination = g_file_new_for_path(destination_name);
    GError *error = NULL;
    gboolean ok = g_file_copy(source, destination, G_FILE_COPY_OVERWRITE,
                              NULL, NULL, NULL, &error);
    if (ok) {
        gtk_label_set_text(GTK_LABEL(state->busy), destination_name);
    } else {
        gtk_label_set_text(GTK_LABEL(state->busy),
                           error != NULL ? error->message :
                           "Could not export report.");
    }
    g_clear_error(&error);
    g_object_unref(source);
    g_object_unref(destination);
    g_free(destination_name);
    (void)button;
}

static void hardware_page_destroyed(GtkWidget *widget, gpointer data)
{
    HardwarePageState *state = data;
    if (state->process != NULL) g_subprocess_force_exit(state->process);
    (void)widget;
}

static void hardware_row_activated(GtkListBox *list, GtkListBoxRow *row,
                                   gpointer data)
{
    (void)list;
    if (row == NULL) return;
    hardware_show_detail(GTK_WIDGET(row), data);
}

static GtkWidget *hardware_page(void)
{
    static const struct {
        const char *id;
        const char *name;
    } probes[] = {
        {"display", "Display"},
        {"touchscreen", "Touchscreen"},
        {"hardware-keys", "Hardware keys"},
        {"speaker", "Speaker / earpiece"},
        {"microphone", "Microphone"},
        {"vibration", "Vibration"},
        {"sensors", "Sensors"},
        {"battery", "Battery"},
        {"backlight", "Backlight"},
        {"wifi", "Wi-Fi"},
        {"bluetooth", "Bluetooth"},
        {"cameras", "Cameras"},
        {"gps", "GPS"},
        {"modem", "Modem"},
        {"storage", "Storage"},
        {"rtc", "RTC"},
        {"usb-network", "USB networking"}
    };
    HardwarePageState *state = g_new0(HardwarePageState, 1);
    state->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(state->page, 12);
    gtk_widget_set_margin_end(state->page, 12);
    gtk_widget_set_margin_top(state->page, 8);
    gtk_box_append(GTK_BOX(state->page), heading("Hardware Test"));
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->run_all = gtk_button_new_with_label("Run all");
    GtkWidget *export = gtk_button_new_with_label("Export report");
    gtk_box_append(GTK_BOX(toolbar), state->run_all);
    gtk_box_append(GTK_BOX(toolbar), export);
    gtk_box_append(GTK_BOX(state->page), toolbar);
    state->busy = gtk_label_new("Read-only probes; interactive checks are marked NEEDS CHECK.");
    gtk_label_set_wrap(GTK_LABEL(state->busy), TRUE);
    gtk_label_set_xalign(GTK_LABEL(state->busy), 0.0f);
    gtk_box_append(GTK_BOX(state->page), state->busy);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    state->list = gtk_list_box_new();
    g_signal_connect(state->list, "row-activated",
                     G_CALLBACK(hardware_row_activated), state);
    for (size_t i = 0; i < G_N_ELEMENTS(probes); ++i) {
        GtkWidget *item = gtk_list_box_row_new();
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(text, TRUE);
        GtkWidget *name = gtk_label_new(probes[i].name);
        gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
        GtkWidget *status = gtk_label_new("NEEDS CHECK");
        gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
        gtk_label_set_markup(GTK_LABEL(status),
                             "<b>not run</b>");
        gtk_box_append(GTK_BOX(text), name);
        gtk_box_append(GTK_BOX(text), status);
        GtkWidget *test = gtk_button_new_with_label("Test");
        g_object_set_data_full(G_OBJECT(item), "probe-id",
                               g_strdup(probes[i].id), g_free);
        g_object_set_data(G_OBJECT(item), "probe-status", status);
        g_object_set_data(G_OBJECT(item), "probe-detail", item);
        g_object_set_data_full(G_OBJECT(test), "probe-id",
                               g_strdup(probes[i].id), g_free);
        g_object_set_data_full(G_OBJECT(item), "detail",
                               g_strdup("Run this probe to collect details."),
                               g_free);
        g_signal_connect(test, "clicked", G_CALLBACK(hardware_run_one), state);
        gtk_box_append(GTK_BOX(line), text);
        gtk_box_append(GTK_BOX(line), test);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(item), line);
        gtk_list_box_append(GTK_LIST_BOX(state->list), item);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), state->list);
    gtk_box_append(GTK_BOX(state->page), scroll);
    g_signal_connect(state->run_all, "clicked",
                     G_CALLBACK(hardware_run_all), state);
    g_signal_connect(export, "clicked", G_CALLBACK(hardware_export), state);
    g_signal_connect(state->page, "destroy",
                     G_CALLBACK(hardware_page_destroyed), state);
    g_object_set_data_full(G_OBJECT(state->page), "hardware-state",
                           state, g_free);
    return state->page;
}

static gboolean update_clock_status(gpointer data)
{
    FILE *file = fopen("/run/zirarch-clock-status", "r");
    char state[32] = "", timestamp[64] = "";
    if (file != NULL) {
        if (fscanf(file, "%31s %63s", state, timestamp) < 1) {
            state[0] = '\0';
        }
        fclose(file);
    }
    if (strcmp(state, "synced") == 0) {
        time_t now = time(NULL);
        struct tm local = {0};
        localtime_r(&now, &local);
        char text[64];
        strftime(text, sizeof(text), "Time: synced %H:%M", &local);
        gtk_label_set_text(GTK_LABEL(data), text);
    } else {
        gtk_label_set_text(GTK_LABEL(data), "Time: not synced");
    }
    (void)timestamp;
    return G_SOURCE_CONTINUE;
}

static void remove_clock_timer(GtkWidget *widget, gpointer data)
{
    guint source_id = GPOINTER_TO_UINT(data);
    if (source_id != 0) g_source_remove(source_id);
    (void)widget;
}

static void post_every_boot_clicked(GtkCheckButton *button, gpointer data)
{
    (void)data;
    const char *path = "/var/lib/zirarch/post-every-boot";
    GError *error = NULL;
    if (gtk_check_button_get_active(button)) {
        if (g_mkdir_with_parents("/var/lib/zirarch", 0755) != 0 ||
            !g_file_set_contents(path, "1\n", -1, &error)) {
            shell_show_error(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                             "Could not enable POST on every boot.");
        }
    } else if (remove(path) != 0 && errno != ENOENT) {
        shell_show_error(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                         "Could not disable POST on every boot.");
    }
    g_clear_error(&error);
}

static void post_run_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    GError *error = NULL;
    char *arguments[] = {"/usr/bin/zirarch-post", "--force", NULL};
    if (!g_spawn_async(NULL, arguments, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                        NULL, &error)) {
        shell_show_error(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                         "Could not run POST.");
    }
    g_clear_error(&error);
}

static void post_report_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    char *report = NULL;
    GError *error = NULL;
    if (!g_file_get_contents("/var/log/zirarch-post.log",
                             &report, NULL, &error)) {
        shell_show_error(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                         "No previous POST report is available.");
    } else {
        shell_show_error(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                         report);
    }
    g_clear_error(&error);
    g_free(report);
}

static GtkWidget *about_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(page), heading("About"));
    gtk_box_append(GTK_BOX(page), gtk_label_new("Samsung Galaxy On5 SM-G550T"));
    char kernel[256] = "Kernel unavailable";
    FILE *file = fopen("/proc/sys/kernel/osrelease", "r");
    if (file != NULL) {
        fgets(kernel, sizeof(kernel), file);
        fclose(file);
        kernel[strcspn(kernel, "\r\n")] = '\0';
    }
    char model[256];
    snprintf(model, sizeof(model), "Kernel %s", kernel);
    gtk_box_append(GTK_BOX(page), gtk_label_new(model));
    GtkWidget *clock_status = gtk_label_new("Time: not synced");
    gtk_label_set_xalign(GTK_LABEL(clock_status), 0.0f);
    gtk_box_append(GTK_BOX(page), clock_status);
    update_clock_status(clock_status);
    guint clock_timer = g_timeout_add_seconds(
        1, update_clock_status, clock_status);
    g_signal_connect(clock_status, "destroy",
                     G_CALLBACK(remove_clock_timer),
                     GUINT_TO_POINTER(clock_timer));
    FILE *stats = fopen("/proc/loadavg", "r");
    char load[128] = "CPU load unavailable";
    if (stats != NULL) {
        double one, five, fifteen;
        if (fscanf(stats, "%lf %lf %lf", &one, &five, &fifteen) == 3) {
            snprintf(load, sizeof(load), "CPU load: %.2f / %.2f / %.2f",
                     one, five, fifteen);
        }
        fclose(stats);
    }
    gtk_box_append(GTK_BOX(page), gtk_label_new(load));
    stats = fopen("/proc/meminfo", "r");
    unsigned long total = 0, available = 0, free_memory = 0;
    char key[32], unit[16];
    unsigned long value;
    if (stats != NULL) {
        while (fscanf(stats, "%31s %lu %15s", key, &value, unit) == 3) {
            if (strcmp(key, "MemTotal:") == 0) total = value;
            else if (strcmp(key, "MemAvailable:") == 0) available = value;
            else if (strcmp(key, "MemFree:") == 0) free_memory = value;
        }
        fclose(stats);
    }
    char memory[128];
    if (total != 0) {
        unsigned long used = total - (available != 0 ? available : free_memory);
        snprintf(memory, sizeof(memory), "RAM: %lu MiB of %lu MiB used",
                 used / 1024, total / 1024);
    } else {
        snprintf(memory, sizeof(memory), "RAM usage unavailable");
    }
    gtk_box_append(GTK_BOX(page), gtk_label_new(memory));
    struct statvfs disk;
    char disk_text[128];
    if (statvfs("/", &disk) == 0 && disk.f_blocks != 0) {
        unsigned long long total_bytes = disk.f_blocks * disk.f_frsize;
        unsigned long long used_bytes =
            (disk.f_blocks - disk.f_bfree) * disk.f_frsize;
        snprintf(disk_text, sizeof(disk_text),
                 "Disk: %llu MiB of %llu MiB used",
                 used_bytes >> 20, total_bytes >> 20);
    } else {
        snprintf(disk_text, sizeof(disk_text), "Disk usage unavailable");
    }
    gtk_box_append(GTK_BOX(page), gtk_label_new(disk_text));
    GtkWidget *post_every = gtk_check_button_new_with_label(
        "Show POST on every boot");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(post_every),
        g_file_test("/var/lib/zirarch/post-every-boot",
                    G_FILE_TEST_IS_REGULAR));
    g_signal_connect(post_every, "toggled",
                     G_CALLBACK(post_every_boot_clicked), NULL);
    GtkWidget *post_now = gtk_button_new_with_label("Run POST now");
    GtkWidget *post_report = gtk_button_new_with_label("Last boot report");
    g_signal_connect(post_now, "clicked", G_CALLBACK(post_run_clicked), NULL);
    g_signal_connect(post_report, "clicked",
                     G_CALLBACK(post_report_clicked), NULL);
    gtk_box_append(GTK_BOX(page), post_every);
    gtk_box_append(GTK_BOX(page), post_now);
    gtk_box_append(GTK_BOX(page), post_report);
    return page;
}

static void open_section(GtkButton *button, gpointer data)
{
    GtkStack *stack = data;
    const char *name = g_object_get_data(G_OBJECT(button), "section");
    gtk_stack_set_visible_child_name(stack, name);
}

void settings_open(ShellState *state)
{
    shell_keyboard_command("entry", "off");
    if (state->launcher != NULL) launcher_close(state);
    if (state->quick_settings_open) {
        state->quick_settings_open = 0;
        if (state->home_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(state->home_stack), "home");
        }
    }
    if (state->recents != NULL) gtk_widget_set_visible(state->recents, FALSE);
    if (state->settings == NULL) {
        GtkWidget *window = gtk_application_window_new(state->application);
        state->settings = window;
        gtk_window_set_title(GTK_WINDOW(window), "Settings");
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        gtk_window_set_default_size(GTK_WINDOW(window),
                                    state->screen.width,
                                    state->screen.height);
        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *back = gtk_button_new_with_label("Back");
        gtk_widget_set_size_request(back, 64, 64);
        g_signal_connect(back, "clicked", G_CALLBACK(back_clicked), window);
        GtkWidget *title = heading("Settings");
        gtk_box_append(GTK_BOX(header), back);
        gtk_box_append(GTK_BOX(header), title);
        gtk_box_append(GTK_BOX(root), header);
        GtkStack *stack = GTK_STACK(gtk_stack_new());
        gtk_stack_set_transition_type(stack, GTK_STACK_TRANSITION_TYPE_NONE);
        gtk_widget_set_vexpand(GTK_WIDGET(stack), TRUE);
        GtkWidget *menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_start(menu, 20);
        gtk_widget_set_margin_end(menu, 20);
        gtk_widget_set_margin_top(menu, 12);
        gtk_widget_set_margin_bottom(menu, 12);
        gtk_stack_add_named(stack, menu, "menu");
        gtk_stack_add_named(stack, wifi_page_new(GTK_WINDOW(window)), "wifi");
        gtk_stack_add_named(stack, display_page(state), "display");
        gtk_stack_add_named(stack, keyboard_page(), "keyboard");
        gtk_stack_add_named(stack, storage_page(), "storage");
        gtk_stack_add_named(stack, about_page(), "about");
        gtk_stack_add_named(stack, apps_page(state), "apps");
        gtk_stack_add_named(stack, battery_page(), "battery");
        gtk_stack_add_named(stack, hardware_page(), "hardware");
        const char *names[] = {
            "Wi-Fi - networks and connections",
            "Display - brightness and text size",
            "Keyboard - layout and typing behavior",
            "Apps - installed apps and display size",
            "Storage - device storage",
            "Battery - charge and status",
            "Hardware Test - safe read-only diagnostics",
            "About - device and system information"
        };
        const char *ids[] = {
            "wifi", "display", "keyboard", "apps", "storage", "battery",
            "hardware", "about"
        };
        for (int i = 0; i < 8; ++i) {
            GtkWidget *button = row(names[i]);
            g_object_set_data(G_OBJECT(button), "section", (gpointer)ids[i]);
            g_signal_connect(button, "clicked", G_CALLBACK(open_section), stack);
            gtk_box_append(GTK_BOX(menu), button);
        }
        gtk_box_append(GTK_BOX(root), GTK_WIDGET(stack));
        g_object_set_data(G_OBJECT(window), "settings-stack", stack);
        gtk_window_set_child(GTK_WINDOW(window), root);
        GtkEventController *keys = gtk_event_controller_key_new();
        g_signal_connect(keys, "key-pressed", G_CALLBACK(settings_key), window);
        gtk_widget_add_controller(window, keys);
        g_signal_connect(window, "close-request",
                         G_CALLBACK(settings_close_requested), state);
    }

    gtk_window_present(GTK_WINDOW(state->settings));
}

void settings_open_section(ShellState *state, const char *section)
{
    settings_open(state);
    GtkStack *stack = g_object_get_data(G_OBJECT(state->settings),
                                        "settings-stack");
    if (stack != NULL && section != NULL) {
        gtk_stack_set_visible_child_name(stack, section);
    }
}
