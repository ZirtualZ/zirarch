#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <stdio.h>
#include <time.h>

static GtkWidget *clock_label;
static GtkWidget *date_label;
static GtkWidget *battery_label;

static gboolean update_lock(gpointer data)
{
    time_t now = time(NULL);
    struct tm local;
    char clock[16], date[64], battery[64];
    FILE *file;
    int capacity = -1;
    char status[32] = "unknown";
    localtime_r(&now, &local);
    strftime(clock, sizeof(clock), "%H:%M", &local);
    strftime(date, sizeof(date), "%A, %B %e", &local);
    gtk_label_set_text(GTK_LABEL(clock_label), clock);
    gtk_label_set_text(GTK_LABEL(date_label), date);
    file = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (file != NULL) {
        fscanf(file, "%d", &capacity);
        fclose(file);
    }
    file = fopen("/sys/class/power_supply/battery/status", "r");
    if (file != NULL) {
        if (fgets(status, sizeof(status), file)) {
            status[strcspn(status, "\r\n")] = '\0';
        }
        fclose(file);
    }
    snprintf(battery, sizeof(battery), "Battery %d%% %s",
             capacity >= 0 ? capacity : 0, status);
    gtk_label_set_text(GTK_LABEL(battery_label), battery);
    (void)data;
    return G_SOURCE_CONTINUE;
}

static void unlock_clicked(GtkButton *button, gpointer data)
{
    FILE *file;
    (void)button;
    file = fopen("/run/fieldm-unlock-request", "w");
    if (file != NULL) fclose(file);
    gtk_widget_destroy(GTK_WIDGET(data));
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Zirarch Lock");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(window));
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_halign(root, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(root, GTK_ALIGN_CENTER);
    clock_label = gtk_label_new("--:--");
    date_label = gtk_label_new("");
    battery_label = gtk_label_new("");
    GtkWidget *unlock = gtk_button_new_with_label(
        "Swipe up / press Home to unlock");
    gtk_widget_set_size_request(unlock, 620, 96);
    gtk_box_pack_start(GTK_BOX(root), clock_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), date_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), battery_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), unlock, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    g_signal_connect(unlock, "clicked", G_CALLBACK(unlock_clicked), window);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    update_lock(NULL);
    g_timeout_add_seconds(30, update_lock, NULL);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
