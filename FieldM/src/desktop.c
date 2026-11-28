#define _POSIX_C_SOURCE 200809L

#include "shell/gtk_shell.h"
#include "shell/session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int own_session = argc > 1 && strcmp(argv[1], "--x-session") == 0;
    const char *display = getenv("DISPLAY");
    if (!own_session &&
        ((display == NULL || display[0] == '\0') ||
         session_running_on_virtual_console())) {
        session_start_x_server("--x-session");
    }
    return gtk_shell_run(argc, argv);
}
