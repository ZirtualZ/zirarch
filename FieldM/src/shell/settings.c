#include "gtk_shell_internal.h"
#include "wifi.h"

#include <X11/Xlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/stat.h>

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
        FILE *file = fopen(path, "w");
        int failed = file == NULL;
        if (file != NULL) {
            if (fprintf(file, "Xft.dpi: %s\n"
                          "Xft.antialias: 1\n"
                          "Xft.hinting: 1\n"
                          "XTerm*background: #F4F1EA\n"
                          "XTerm*foreground: #111111\n"
                          "XTerm*cursorColor: #111111\n"
                          "XTerm*faceName: DejaVu Sans Mono\n"
                          "XTerm*faceSize: 11\n"
                          "XTerm*scrollBar: false\n", dpi) < 0) failed = 1;
            if (fclose(file) != 0) failed = 1;
        }
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
    (void)data;
}

static GtkWidget *display_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(page), heading("Display"));
    gtk_box_append(GTK_BOX(page), row("Brightness"));
    for (int i = 0; i < 3; ++i) {
        GtkWidget *button = gtk_button_new_with_label(
            i == 0 ? "Brightness 25%" : i == 1 ? "Brightness 50%" : "Brightness 100%");
        g_object_set_data_full(G_OBJECT(button), "brightness",
                               g_strdup(i == 0 ? "34" : i == 1 ? "67" : "134"),
                               g_free);
        g_signal_connect(button, "clicked", G_CALLBACK(display_apply), NULL);
        gtk_box_append(GTK_BOX(page), button);
    }
    gtk_box_append(GTK_BOX(page), gtk_label_new("Text size"));
    const char *dpis[] = {"144", "168", "192"};
    for (int i = 0; i < 3; ++i) {
        GtkWidget *button = gtk_button_new_with_label(dpis[i]);
        g_object_set_data_full(G_OBJECT(button), "dpi",
                               g_strdup(dpis[i]), g_free);
        g_signal_connect(button, "clicked", G_CALLBACK(display_apply), NULL);
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
            char path[512], type[64] = "", status[64] = "", capacity[32] = "";
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
            char text[160];
            snprintf(text, sizeof(text), "%s%% — %s",
                     g_strstrip(capacity), g_strstrip(status));
            gtk_box_append(GTK_BOX(page), gtk_label_new(text));
            found = TRUE;
        }
        g_dir_close(directory);
    }
    if (!found) gtk_box_append(GTK_BOX(page), gtk_label_new("Battery status unavailable"));
    return page;
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
        gtk_stack_add_named(stack, display_page(), "display");
        gtk_stack_add_named(stack, keyboard_page(), "keyboard");
        gtk_stack_add_named(stack, storage_page(), "storage");
        gtk_stack_add_named(stack, about_page(), "about");
        gtk_stack_add_named(stack, apps_page(state), "apps");
        gtk_stack_add_named(stack, battery_page(), "battery");
        const char *names[] = {
            "Wi-Fi - networks and connections",
            "Display - brightness and text size",
            "Keyboard - layout and typing behavior",
            "Apps - installed apps and display size",
            "Storage - device storage",
            "Battery - charge and status",
            "About - device and system information"
        };
        const char *ids[] = {
            "wifi", "display", "keyboard", "apps", "storage", "battery", "about"
        };
        for (int i = 0; i < 7; ++i) {
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
