#include "gtk_shell_internal.h"
#include "wifi.h"

#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

static void back_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *window = data;
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
        back_clicked(NULL, data);
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

static void display_apply(GtkButton *button, gpointer data)
{
    const char *brightness = g_object_get_data(G_OBJECT(button), "brightness");
    const char *dpi = g_object_get_data(G_OBJECT(button), "dpi");
    if (brightness != NULL) {
        FILE *file = fopen("/sys/class/backlight/panel/brightness", "w");
        if (file != NULL) {
            fprintf(file, "%s", brightness);
            fclose(file);
        }
    }
    if (dpi != NULL) {
        char path[1200];
        const char *home = getenv("HOME");
        if (home != NULL) {
            snprintf(path, sizeof(path), "%s/.Xresources", home);
            FILE *file = fopen(path, "w");
            if (file != NULL) {
                fprintf(file, "Xft.dpi: %s\n", dpi);
                fclose(file);
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

static GtkWidget *keyboard_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(page), heading("Keyboard"));
    gtk_box_append(GTK_BOX(page), row("Layout: QWERTY"));
    gtk_box_append(GTK_BOX(page), row("Terminal row: enabled"));
    gtk_box_append(GTK_BOX(page), row("Ctrl is sticky"));
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
        const char *names[] = {"Wi-Fi", "Display", "Keyboard", "Storage", "About"};
        const char *ids[] = {"wifi", "display", "keyboard", "storage", "about"};
        for (int i = 0; i < 5; ++i) {
            GtkWidget *button = row(names[i]);
            g_object_set_data(G_OBJECT(button), "section", (gpointer)ids[i]);
            g_signal_connect(button, "clicked", G_CALLBACK(open_section), stack);
            gtk_box_append(GTK_BOX(menu), button);
        }
        gtk_box_append(GTK_BOX(root), GTK_WIDGET(stack));
        gtk_window_set_child(GTK_WINDOW(window), root);
        GtkEventController *keys = gtk_event_controller_key_new();
        g_signal_connect(keys, "key-pressed", G_CALLBACK(settings_key), window);
        gtk_widget_add_controller(window, keys);
        g_signal_connect(window, "close-request",
                         G_CALLBACK(settings_close_requested), state);
    }
    gtk_window_present(GTK_WINDOW(state->settings));
}
