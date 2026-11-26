#include "icon_badge.h"

#include <glib/gstdio.h>

GtkWidget *shell_make_icon_badge(const char *name, const char *icon)
{
    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_size_request(overlay, 56, 56);
    gtk_widget_add_css_class(overlay, "app-icon-badge");

    char initial[7] = "?";
    if (name != NULL && name[0] != '\0') {
        gunichar character = g_utf8_get_char(name);
        int length = g_unichar_to_utf8(g_unichar_toupper(character), initial);
        initial[length] = '\0';
    }
    GtkWidget *letter = gtk_label_new(initial);
    gtk_widget_add_css_class(letter, "app-icon-letter");
    gtk_overlay_set_child(GTK_OVERLAY(overlay), letter);

    GtkWidget *image = NULL;
    if (icon != NULL && icon[0] == '/' && g_file_test(icon,
                                                      G_FILE_TEST_IS_REGULAR)) {
        image = gtk_image_new_from_file(icon);
    } else if (icon != NULL && icon[0] != '\0') {
        GtkIconTheme *theme = gtk_icon_theme_get_for_display(
            gdk_display_get_default());
        if (gtk_icon_theme_has_icon(theme, icon)) {
            image = gtk_image_new_from_icon_name(icon);
        }
    }
    if (image != NULL) {
        gtk_image_set_pixel_size(GTK_IMAGE(image), 52);
        gtk_widget_add_css_class(image, "app-icon-image");
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), image);
    }
    return overlay;
}
