#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

enum {
    CMD_BACK = 2,
    CMD_HOME = 3,
    CMD_RECENTS = 4,
    CMD_APPS = 5,
    CMD_POWER = 6
};

static Display *display;
static Window command_window;
static Atom command_atom;
static int screen_blanked;

static void send_command(long command)
{
    if (display == NULL || command_window == None) {
        return;
    }
    XClientMessageEvent message;
    memset(&message, 0, sizeof(message));
    message.type = ClientMessage;
    message.window = command_window;
    message.message_type = command_atom;
    message.format = 32;
    message.data.l[0] = command;
    XSendEvent(display, command_window, False, NoEventMask,
               (XEvent *)&message);
    XFlush(display);
}

static void send_escape(void)
{
    Window focus = None;
    int revert = 0;
    XGetInputFocus(display, &focus, &revert);
    if (focus == None || focus == PointerRoot) {
        focus = DefaultRootWindow(display);
    }
    KeyCode escape = XKeysymToKeycode(display, XK_Escape);
    XKeyEvent event;
    memset(&event, 0, sizeof(event));
    event.display = display;
    event.window = focus;
    event.root = DefaultRootWindow(display);
    event.subwindow = None;
    event.time = CurrentTime;
    event.x = 1;
    event.y = 1;
    event.x_root = 1;
    event.y_root = 1;
    event.same_screen = True;
    event.keycode = escape;
    event.type = KeyPress;
    XSendEvent(display, focus, True, KeyPressMask, (XEvent *)&event);
    event.type = KeyRelease;
    XSendEvent(display, focus, True, KeyReleaseMask, (XEvent *)&event);
    XFlush(display);
}

static void set_volume(int direction)
{
    char *const arguments[] = {
        "/usr/bin/amixer", "set", "Master",
        direction > 0 ? "5%+" : "5%-", NULL
    };
    pid_t child = fork();
    if (child == 0) {
        execv(arguments[0], arguments);
        _exit(127);
    }
}

static void toggle_power(void)
{
    const char *brightness_path = "/sys/class/backlight/panel/brightness";
    const char *state_path = "/run/fieldm-screen-blanked";
    if (!screen_blanked) {
        int brightness = 0;
        FILE *read = fopen(brightness_path, "r");
        if (read != NULL) {
            fscanf(read, "%d", &brightness);
            fclose(read);
        }
        FILE *state = fopen(state_path, "w");
        if (state != NULL) {
            fprintf(state, "%d\n", brightness);
            fclose(state);
        }
        FILE *backlight = fopen(brightness_path, "w");
        if (backlight != NULL) {
            fprintf(backlight, "0\n");
            fclose(backlight);
        }
        FILE *fb = fopen("/sys/class/graphics/fb0/blank", "w");
        if (fb != NULL) {
            fprintf(fb, "1\n");
            fclose(fb);
        }
        screen_blanked = 1;
    } else {
        int brightness = 134;
        FILE *state = fopen(state_path, "r");
        if (state != NULL) {
            fscanf(state, "%d", &brightness);
            fclose(state);
        }
        FILE *fb = fopen("/sys/class/graphics/fb0/blank", "w");
        if (fb != NULL) {
            fprintf(fb, "0\n");
            fclose(fb);
        }
        FILE *backlight = fopen(brightness_path, "w");
        if (backlight != NULL) {
            fprintf(backlight, "%d\n", brightness > 0 ? brightness : 134);
            fclose(backlight);
        }
        unlink(state_path);
        screen_blanked = 0;
    }
}

static unsigned long long monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000ULL +
           (unsigned long long)now.tv_nsec / 1000000ULL;
}

int main(void)
{
    FILE *initial_state = fopen("/run/fieldm-screen-blanked", "r");
    screen_blanked = initial_state != NULL;
    if (initial_state != NULL) {
        fclose(initial_state);
    }
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        return 1;
    }
    command_atom = XInternAtom(display, "_FIELD_DESKTOP_COMMAND", False);
    command_window = XGetSelectionOwner(
        display, XInternAtom(display, "_FIELD_DESKTOP_COMMAND_OWNER", False));
    if (command_window == None) {
        XCloseDisplay(display);
        return 1;
    }

    struct pollfd *polls = calloc(64, sizeof(*polls));
    int *fds = calloc(64, sizeof(*fds));
    int count = 0;
    DIR *dir = opendir("/dev/input");
    if (dir == NULL || polls == NULL || fds == NULL) {
        free(polls);
        free(fds);
        if (dir != NULL) {
            closedir(dir);
        }
        XCloseDisplay(display);
        return 1;
    }
    struct dirent *entry;
    while (count < 64 && (entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        char path[320];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        unsigned long evbits[(EV_MAX + 1 + 8 * sizeof(unsigned long) - 1) /
                             (8 * sizeof(unsigned long))] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 ||
            !(evbits[EV_KEY / (8 * sizeof(unsigned long))] &
              (1UL << (EV_KEY % (8 * sizeof(unsigned long)))))) {
            close(fd);
            continue;
        }
        polls[count].fd = fd;
        polls[count].events = POLLIN;
        fds[count] = fd;
        ++count;
    }
    closedir(dir);

    unsigned long long home_down = 0;
    int home_pressed = 0;
    for (;;) {
        int ready = poll(polls, (nfds_t)count, 100);
        unsigned long long now = monotonic_ms();
        if (home_pressed && home_down != 0 &&
            now - home_down >= 1000) {
            home_pressed = 0;
            send_command(CMD_APPS);
        }
        if (ready <= 0) {
            continue;
        }
        for (int i = 0; i < count; ++i) {
            if (!(polls[i].revents & POLLIN)) {
                continue;
            }
            struct input_event events[32];
            ssize_t size;
            while ((size = read(polls[i].fd, events, sizeof(events))) > 0) {
                for (size_t j = 0;
                     j < (size_t)size / sizeof(events[0]); ++j) {
                    if (screen_blanked &&
                        (events[j].type == EV_ABS ||
                         events[j].type == EV_REL ||
                         events[j].type == EV_SYN ||
                         events[j].code == BTN_TOUCH)) {
                        continue;
                    }
                    if (events[j].type != EV_KEY) {
                        continue;
                    }
                    int code = events[j].code;
                    if (events[j].value == 1) {
                        if (screen_blanked &&
                            code != KEY_POWER && code != KEY_HOME) {
                            continue;
                        }
                        if (screen_blanked && code == KEY_HOME) {
                            toggle_power();
                            send_command(CMD_HOME);
                            continue;
                        }
                        if (code == KEY_BACK || code == KEY_ESC) {
                            send_escape();
                            send_command(CMD_BACK);
                        } else if (code == KEY_HOME) {
                            home_down = now;
                            home_pressed = 1;
                        } else if (code == KEY_MENU ||
                                   code == 139 /* KEY_MENU */) {
                            send_command(CMD_RECENTS);
                        } else if (code == KEY_VOLUMEUP) {
                            set_volume(1);
                        } else if (code == KEY_VOLUMEDOWN) {
                            set_volume(-1);
                        } else if (code == KEY_POWER) {
                            toggle_power();
                            send_command(CMD_POWER);
                        }
                    } else if (events[j].value == 0 && code == KEY_HOME) {
                        if (home_pressed && home_down != 0 &&
                            now - home_down < 1000) {
                            send_command(CMD_HOME);
                        }
                        home_pressed = 0;
                        home_down = 0;
                    }
                }
            }
        }
    }
}
