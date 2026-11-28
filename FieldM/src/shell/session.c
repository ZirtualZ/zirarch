#define _POSIX_C_SOURCE 200809L

#include "session.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void session_start_x_server(const char *session_argument)
{
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1) {
        perror("Could not find desktop executable path");
        exit(1);
    }
    executable[length] = '\0';

    execlp("startx", "startx", executable, session_argument, (char *)NULL);
    perror("Could not start X. Install xorg-xinit and run this from a TTY.");
    exit(1);
}

int session_running_on_virtual_console(void)
{
    const char *terminal = ttyname(STDIN_FILENO);
    return terminal != NULL && strncmp(terminal, "/dev/tty", 8) == 0 &&
           isdigit((unsigned char)terminal[8]);
}

