#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *url;
    GtkWidget *progress;
    GtkWidget *reload_stop;
    WebKitWebView *web;
    char *zoom_text;
    int desktop;
} BrowserState;

static int dark_theme(void)
{
    FILE *file = fopen("/root/.config/simple-desktop/features", "r");
    char line[128];
    int dark = 0;
    if (file == NULL) return 0;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "theme=dark", 10) == 0) dark = 1;
    }
    fclose(file);
    return dark;
}

static void load_uri(BrowserState *state, const char *text)
{
    char uri[2048];
    const char *cursor = text;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (cursor[0] == '\0') return;
    if (strstr(cursor, "://") != NULL ||
        strncmp(cursor, "about:", 6) == 0 ||
        strncmp(cursor, "file:", 5) == 0) {
        snprintf(uri, sizeof(uri), "%s", cursor);
    } else if (strchr(cursor, '.') != NULL && strchr(cursor, ' ') == NULL) {
        snprintf(uri, sizeof(uri), "https://%s", cursor);
    } else {
        char encoded[1024];
        char *p = encoded;
        for (; *cursor && (size_t)(p - encoded) < sizeof(encoded) - 4; cursor++) {
            if (isalnum((unsigned char)*cursor) || strchr("-._~", *cursor)) {
                *p++ = *cursor;
            } else {
                p += snprintf(p, 4, "%%%02X", (unsigned char)*cursor);
            }
        }
        *p = '\0';
        snprintf(uri, sizeof(uri),
                 "https://lite.duckduckgo.com/lite/?q=%s", encoded);
    }
    webkit_web_view_load_uri(state->web, uri);
}

static gboolean url_activate(GtkWidget *entry, BrowserState *state)
{
    load_uri(state, gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    return TRUE;
}

static void url_focus(GtkWidget *entry, gpointer data)
{
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    (void)data;
}

static void back_clicked(GtkButton *button, BrowserState *state)
{
    (void)button;
    if (webkit_web_view_can_go_back(state->web))
        webkit_web_view_go_back(state->web);
    else
        gtk_window_close(GTK_WINDOW(state->window));
}

static void forward_clicked(GtkButton *button, BrowserState *state)
{
    (void)button;
    if (webkit_web_view_can_go_forward(state->web))
        webkit_web_view_go_forward(state->web);
}

static void reload_stop_clicked(GtkButton *button, BrowserState *state)
{
    (void)button;
    if (webkit_web_view_is_loading(state->web))
        webkit_web_view_stop_loading(state->web);
    else
        webkit_web_view_reload(state->web);
}

static void title_changed(WebKitWebView *web, GParamSpec *spec,
                          BrowserState *state)
{
    (void)spec;
    const char *title = webkit_web_view_get_title(web);
    gtk_window_set_title(GTK_WINDOW(state->window),
                         title != NULL && title[0] != '\0'
                             ? title : "Browser");
}

static void uri_changed(WebKitWebView *web, GParamSpec *spec,
                        BrowserState *state)
{
    (void)spec;
    const char *uri = webkit_web_view_get_uri(web);
    if (uri != NULL) gtk_entry_set_text(GTK_ENTRY(state->url), uri);
}

static void progress_changed(WebKitWebView *web, GParamSpec *spec,
                             BrowserState *state)
{
    (void)spec;
    double progress = webkit_web_view_get_estimated_load_progress(web);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress), progress);
}

static void loading_changed(WebKitWebView *web, GParamSpec *spec,
                            BrowserState *state)
{
    (void)spec;
    gtk_button_set_label(GTK_BUTTON(state->reload_stop),
                         webkit_web_view_is_loading(web) ? "Stop" : "Reload");
}

static void notify_download(WebKitDownload *download, BrowserState *state)
{
    char *message = g_strdup_printf("Download started: %s",
                                    "file");
    g_spawn_command_line_async(
        g_strdup_printf("/usr/bin/fieldm-notify %s",
                        message != NULL ? message : "download"),
        NULL);
    g_free(message);
    (void)state;
}

static void download_started(WebKitWebContext *context, WebKitDownload *download,
                             BrowserState *state)
{
    (void)context;
    mkdir("/root/Downloads", 0700);
    char path[2048];
    snprintf(path, sizeof(path),
             "file:///root/Downloads/download-%lu",
             (unsigned long)time(NULL));
    webkit_download_set_destination(download, path);
    notify_download(download, state);
}

static void desktop_action(BrowserState *state);

static void menu_action(GtkMenuItem *item, BrowserState *state)
{
    const char *action = g_object_get_data(G_OBJECT(item), "action");
    if (!strcmp(action, "home"))
        webkit_web_view_load_uri(state->web,
                                 "https://lite.duckduckgo.com/lite/");
    else if (!strcmp(action, "zoom-in"))
        webkit_web_view_set_zoom_level(state->web,
                                       webkit_web_view_get_zoom_level(state->web) + 0.25);
    else if (!strcmp(action, "zoom-out"))
        webkit_web_view_set_zoom_level(state->web,
                                       webkit_web_view_get_zoom_level(state->web) - 0.25);
    else if (!strcmp(action, "desktop")) {
        state->desktop = !state->desktop;
        desktop_action(state);
    }
    else if (!strcmp(action, "clear")) {
        webkit_web_context_clear_cache(
            webkit_web_view_get_context(state->web));
    }
}

static GtkWidget *add_menu_item(GtkWidget *menu, const char *label,
                                const char *action, BrowserState *state)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    g_object_set_data_full(G_OBJECT(item), "action",
                           g_strdup(action), g_free);
    g_signal_connect(item, "activate", G_CALLBACK(menu_action), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

static void menu_clicked(GtkButton *button, BrowserState *state)
{
    GtkWidget *menu = gtk_menu_new();
    add_menu_item(menu, "Home page", "home", state);
    add_menu_item(menu, "Zoom +", "zoom-in", state);
    add_menu_item(menu, "Zoom -", "zoom-out", state);
    add_menu_item(menu, state->desktop ? "Mobile site" : "Desktop site",
                  "desktop", state);
    add_menu_item(menu, "Clear data", "clear", state);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(button),
                             GDK_GRAVITY_SOUTH_WEST,
                             GDK_GRAVITY_NORTH_WEST, NULL);
}

static void desktop_action(BrowserState *state)
{
    WebKitSettings *settings = webkit_web_view_get_settings(state->web);
    const char *ua = state->desktop ? NULL :
        "Mozilla/5.0 (Linux; Android 10; SM-G550T) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/131.0.0.0 Mobile Safari/537.36";
    if (ua != NULL)
        webkit_settings_set_user_agent(settings, ua);
    else
        webkit_settings_set_user_agent(settings, NULL);
    webkit_web_view_set_settings(state->web, settings);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    BrowserState *state = g_new0(BrowserState, 1);

    state->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state->window), "Browser");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 720, 1280);
    gtk_window_set_wmclass(GTK_WINDOW(state->window),
                           "fieldm-browser", "fieldm-browser");
    gtk_window_set_role(GTK_WINDOW(state->window), "fieldm-browser");

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(root, "fieldm-browser");
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(bar, "fieldm-browser-bar");
    gtk_widget_set_margin_start(bar, 4);
    gtk_widget_set_margin_end(bar, 4);
    gtk_widget_set_margin_top(bar, 4);
    gtk_widget_set_margin_bottom(bar, 4);
    GtkWidget *back = gtk_button_new_with_label("Back");
    GtkWidget *forward = gtk_button_new_with_label("Forward");
    state->url = gtk_entry_new();
    gtk_widget_set_hexpand(state->url, TRUE);
    state->reload_stop = gtk_button_new_with_label("Reload");
    GtkWidget *menu = gtk_button_new_with_label("Menu");
    gtk_box_pack_start(GTK_BOX(bar), back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), forward, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), state->url, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), state->reload_stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), menu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), bar, FALSE, FALSE, 0);

    state->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->progress), FALSE);
    gtk_box_pack_start(GTK_BOX(root), state->progress, FALSE, FALSE, 0);

    state->web = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_box_pack_start(GTK_BOX(root), GTK_WIDGET(state->web), TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(state->window), root);

    WebKitSettings *settings = webkit_web_view_get_settings(state->web);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_zoom_text_only(settings, FALSE);
#ifdef WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
    webkit_settings_set_hardware_acceleration_policy(
        settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
#endif
    webkit_settings_set_user_agent(
        settings,
        "Mozilla/5.0 (Linux; Android 10; SM-G550T) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/131.0.0.0 Mobile Safari/537.36");
    webkit_web_view_set_settings(state->web, settings);
    webkit_web_view_set_zoom_level(state->web, 1.25);

    GtkCssProvider *css = gtk_css_provider_new();
    char *css_text = dark_theme()
        ? g_strdup("#fieldm-browser, #fieldm-browser-bar, window { background: #161513; color: #EAE6DD; } "
                   "entry { background: #161513; color: #EAE6DD; border: 1px solid #EAE6DD; } "
                   "button { background: #161513; color: #EAE6DD; border: 1px solid #EAE6DD; }")
        : g_strdup("#fieldm-browser, #fieldm-browser-bar, window { background: #F4F1EA; color: #111111; } "
                   "entry { background: #F4F1EA; color: #111111; border: 1px solid #111111; } "
                   "button { background: #F4F1EA; color: #111111; border: 1px solid #111111; }");
    gtk_css_provider_load_from_data(css, css_text, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(css_text);
    g_object_unref(css);

    g_signal_connect(back, "clicked", G_CALLBACK(back_clicked), state);
    g_signal_connect(forward, "clicked", G_CALLBACK(forward_clicked), state);
    g_signal_connect(state->reload_stop, "clicked",
                     G_CALLBACK(reload_stop_clicked), state);
    g_signal_connect(menu, "clicked", G_CALLBACK(menu_clicked), state);
    g_signal_connect(state->url, "activate", G_CALLBACK(url_activate), state);
    g_signal_connect(state->url, "focus-in-event",
                     G_CALLBACK(url_focus), state);
    g_signal_connect(state->web, "notify::title",
                     G_CALLBACK(title_changed), state);
    g_signal_connect(state->web, "notify::uri",
                     G_CALLBACK(uri_changed), state);
    g_signal_connect(state->web, "notify::estimated-load-progress",
                     G_CALLBACK(progress_changed), state);
    g_signal_connect(state->web, "notify::is-loading",
                     G_CALLBACK(loading_changed), state);
    g_signal_connect(webkit_web_view_get_context(state->web),
                     "download-started", G_CALLBACK(download_started), state);

    g_signal_connect(state->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(state->window);

    if (argc > 1)
        load_uri(state, argv[1]);
    else
        webkit_web_view_load_uri(state->web,
                                 "https://lite.duckduckgo.com/lite/");
    gtk_main();
    g_free(state);
    return 0;
}
