#include "icon_badge.h"

GtkWidget *shell_make_icon_badge(const char *name, const char *icon)
{
    GtkWidget *badge = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(badge, 64, 64);
    gtk_widget_set_halign(badge, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(badge, "app-icon-badge");

    char initial[7] = "?";
    if (name != NULL && name[0] != '\0') {
        gunichar character = g_utf8_get_char(name);
        int length = g_unichar_to_utf8(g_unichar_toupper(character), initial);
        initial[length] = '\0';
    }
    GtkWidget *letter = gtk_label_new(initial);
    gtk_widget_add_css_class(letter, "app-icon-letter");
    gtk_widget_set_hexpand(letter, TRUE);
    gtk_widget_set_valign(letter, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(letter, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(badge), letter);
    (void)icon;
    return badge;
}
