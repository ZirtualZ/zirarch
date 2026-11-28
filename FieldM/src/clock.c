#include <gtk/gtk.h>
#include <time.h>

static GtkWidget *clock_label;
static GtkWidget *date_label;

static gboolean clock_tick(gpointer data)
{
    time_t now = time(NULL);
    struct tm local;
    char clock[16], date[64];
    localtime_r(&now, &local);
    strftime(clock, sizeof(clock), "%H:%M:%S", &local);
    strftime(date, sizeof(date), "%A, %B %e", &local);
    gtk_label_set_text(GTK_LABEL(clock_label), clock);
    gtk_label_set_text(GTK_LABEL(date_label), date);
    (void)data;
    return G_SOURCE_CONTINUE;
}

static void run_alarm_command(GtkButton *button, gpointer data)
{
    (void)button;
    char *arguments[] = {
        "/usr/bin/zirarch-alarm", "add", "07:00",
        "Alarm", NULL
    };
    g_spawn_async(NULL, arguments, NULL, G_SPAWN_DEFAULT,
                  NULL, NULL, NULL, NULL);
    (void)data;
}

static void clock_activate(GtkApplication *app, gpointer data)
{
    (void)data;
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Clock");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 1280);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);
    gtk_widget_set_margin_top(root, 16);
    clock_label = gtk_label_new("00:00:00");
    date_label = gtk_label_new("");
    gtk_widget_add_css_class(clock_label, "title-1");
    gtk_label_set_xalign(GTK_LABEL(clock_label), 0.5f);
    gtk_label_set_xalign(GTK_LABEL(date_label), 0.5f);
    gtk_box_append(GTK_BOX(root), clock_label);
    gtk_box_append(GTK_BOX(root), date_label);
    clock_tick(NULL);
    g_timeout_add_seconds(1, clock_tick, NULL);
    GtkWidget *add = gtk_button_new_with_label("Add alarm 07:00");
    g_signal_connect(add, "clicked", G_CALLBACK(run_alarm_command), NULL);
    gtk_box_append(GTK_BOX(root), add);
    gtk_box_append(GTK_BOX(root), gtk_label_new("Timer"));
    gtk_box_append(GTK_BOX(root), gtk_label_new("Stopwatch"));
    gtk_window_set_child(GTK_WINDOW(window), root);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
    GtkApplication *application =
        gtk_application_new("org.zirarch.Clock", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate",
                     G_CALLBACK(clock_activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
