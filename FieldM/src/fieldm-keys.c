#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    CMD_HOME = 3,
    CMD_RECENTS = 4,
    CMD_POWER = 6,
    MAX_INPUT_DEVICES = 64,
    RESTART_HOLD_MS = 5000,
    POWER_TAP_MS = 1000
};

enum {
    KEY_RECENT_SAMSUNG = 254,
    KEY_APPSELECT_SAMSUNG = 580
};

typedef struct {
    int fd;
    int touchscreen;
    char path[320];
} InputDevice;

static FILE *log_file;
static InputDevice input_devices[MAX_INPUT_DEVICES];
static int input_device_count;
static int touchscreen_fd = -1;
static unsigned char held_keys[MAX_INPUT_DEVICES][KEY_MAX + 1];
static int screen_blanked;
static int touchscreen_grabbed;
static int power_held;
static int volume_up_held;
static int volume_down_held;
static int combo_seen;
static int combo_triggered;
static int power_suppressed;
static int volume_up_suppressed;
static unsigned long long power_pressed_at;
static unsigned long long combo_started_at;
static unsigned long long back_pressed_at;
static int back_held;
static int back_toggle_triggered;
static Display *persistent_x_display;

static void log_message(const char *format, ...);

static void reap_children(void)
{
    int status;
    pid_t child;
    for (;;) {
        do {
            child = waitpid(-1, &status, WNOHANG);
        } while (child < 0 && errno == EINTR);
        if (child <= 0) {
            break;
        }
        if (WIFEXITED(status)) {
            log_message("child pid=%ld exit_status=%d",
                        (long)child, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            log_message("child pid=%ld exit_signal=%d",
                        (long)child, WTERMSIG(status));
        } else {
            log_message("child pid=%ld exited", (long)child);
        }
    }
}

static unsigned long long monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (unsigned long long)now.tv_sec * 1000ULL +
           (unsigned long long)now.tv_nsec / 1000000ULL;
}

static void sleep_ms(unsigned int milliseconds)
{
    struct timespec remaining = {
        .tv_sec = (time_t)(milliseconds / 1000),
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L
    };
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static void log_message(const char *format, ...)
{
    if (log_file == NULL) {
        return;
    }
    fflush(log_file);
    struct stat log_stat;
    if (fstat(fileno(log_file), &log_stat) == 0 &&
        log_stat.st_size > 1048576) {
        fclose(log_file);
        rename("/var/log/fieldm-keys.log",
               "/var/log/fieldm-keys.log.1");
        log_file = fopen("/var/log/fieldm-keys.log", "a");
        if (log_file == NULL) {
            return;
        }
    }
    time_t now = time(NULL);
    struct tm local;
    char timestamp[32] = "unknown-time";
    if (localtime_r(&now, &local) != NULL) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    }
    fprintf(log_file, "[%s] ", timestamp);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(log_file, format, arguments);
    va_end(arguments);
    fputc('\n', log_file);
    fflush(log_file);
}

static int read_integer_file(const char *path, int fallback)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return fallback;
    }
    int value = fallback;
    if (fscanf(file, "%d", &value) != 1) {
        value = fallback;
    }
    fclose(file);
    return value;
}

static int write_integer_file(const char *path, int value)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        log_message("cannot open %s: %s", path, strerror(errno));
        return 0;
    }
    int failed = fprintf(file, "%d\n", value) < 0;
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        log_message("cannot write %s", path);
    }
    return !failed;
}

static void set_touchscreen_grab(int grab)
{
    if (touchscreen_fd < 0 || grab == touchscreen_grabbed) {
        return;
    }
    int value = grab ? 1 : 0;
    if (ioctl(touchscreen_fd, EVIOCGRAB, value) != 0) {
        log_message("EVIOCGRAB(%d) failed on touchscreen: %s",
                    value, strerror(errno));
        return;
    }
    touchscreen_grabbed = grab;
    log_message("touchscreen %s", grab ? "grabbed while blank" : "released");
}

static void toggle_power(void)
{
    const char *brightness_path = "/sys/class/backlight/panel/brightness";
    const char *state_path = "/run/fieldm-screen-blanked";
    const char *fb_path = "/sys/class/graphics/fb0/blank";
    if (!screen_blanked) {
        int brightness = read_integer_file(brightness_path, 134);
        write_integer_file(state_path, brightness);
        write_integer_file(brightness_path, 0);
        write_integer_file(fb_path, 1);
        screen_blanked = 1;
        set_touchscreen_grab(1);
        pid_t lock = fork();
        if (lock == 0) {
            execl("/usr/bin/fieldm-lock", "fieldm-lock", (char *)NULL);
            _exit(127);
        }
        FILE *pid = fopen("/run/fieldm-lock.pid", "w");
        if (pid != NULL) {
            fprintf(pid, "%ld\n", (long)lock);
            fclose(pid);
        }
        log_message("screen blanked");
    } else {
        FILE *pid = fopen("/run/fieldm-lock.pid", "r");
        pid_t lock = 0;
        if (pid != NULL) {
            fscanf(pid, "%d", &lock);
            fclose(pid);
        }
        if (lock > 1) kill(lock, SIGTERM);
        unlink("/run/fieldm-lock.pid");
        unlink("/run/fieldm-unlock-request");
        set_touchscreen_grab(0);
        int brightness = read_integer_file(state_path, 134);
        write_integer_file(fb_path, 0);
        write_integer_file(brightness_path, brightness > 0 ? brightness : 134);
        unlink(state_path);
        screen_blanked = 0;
        char discard[64];
        while (read(touchscreen_fd, discard, sizeof(discard)) > 0) {
        }
        log_message("drained queued touch events");
        log_message("screen unblanked");
    }
}

static int read_process_cmdline(pid_t pid, char *buffer, size_t size)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    ssize_t length = read(fd, buffer, size - 1);
    close(fd);
    if (length <= 0) {
        return 0;
    }
    for (ssize_t i = 0; i < length; ++i) {
        if (buffer[i] == '\0') {
            buffer[i] = ' ';
        }
    }
    buffer[length] = '\0';
    return 1;
}

static void set_xauthority_from_server(void)
{
    if (getenv("XAUTHORITY") != NULL) {
        return;
    }
    DIR *proc = opendir("/proc");
    if (proc == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 1) {
            continue;
        }
        char path[64], command[2048];
        snprintf(path, sizeof(path), "/proc/%ld/comm", value);
        FILE *file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        char comm[64];
        int found = fgets(comm, sizeof(comm), file) != NULL;
        fclose(file);
        if (!found || strcmp(comm, "Xorg\n") != 0 ||
            !read_process_cmdline((pid_t)value, command, sizeof(command))) {
            continue;
        }
        char *auth = strstr(command, "-auth ");
        if (auth == NULL) {
            continue;
        }
        auth += 6;
        while (*auth == ' ') {
            ++auth;
        }
        char *end_path = auth;
        while (*end_path != '\0' && *end_path != ' ') {
            ++end_path;
        }
        if (end_path > auth) {
            char saved = *end_path;
            *end_path = '\0';
            setenv("XAUTHORITY", auth, 1);
            *end_path = saved;
            break;
        }
    }
    closedir(proc);
}

static Display *open_x_display(void)
{
    if (getenv("DISPLAY") == NULL) {
        setenv("DISPLAY", ":0", 1);
    }
    set_xauthority_from_server();
    return XOpenDisplay(NULL);
}

static int x_io_error(Display *display)
{
    (void)display;
    persistent_x_display = NULL;
    return 0;
}

static Display *persistent_x(void)
{
    if (persistent_x_display != NULL) {
        return persistent_x_display;
    }
    persistent_x_display = open_x_display();
    if (persistent_x_display != NULL) {
        XSetIOErrorHandler(x_io_error);
    }
    return persistent_x_display;
}

static void send_command_in_child(long command)
{
    Display *display = persistent_x();
    if (display == NULL) {
        log_message("X action %ld ignored: display unavailable", command);
        return;
    }
    Atom owner = XInternAtom(display, "_FIELD_DESKTOP_COMMAND_OWNER", False);
    Window window = XGetSelectionOwner(display, owner);
    if (window != None) {
        XClientMessageEvent message;
        memset(&message, 0, sizeof(message));
        message.type = ClientMessage;
        message.window = window;
        message.message_type =
            XInternAtom(display, "_FIELD_DESKTOP_COMMAND", False);
        message.format = 32;
        message.data.l[0] = command;
        XSendEvent(display, window, False, NoEventMask,
                   (XEvent *)&message);
        XFlush(display);
        log_message("action=command_%ld sent to shell", command);
    } else {
        log_message("action=command_%ld shell owner unavailable", command);
    }
}

static void send_escape_in_child(void)
{
    Display *display = persistent_x();
    if (display == NULL) {
        log_message("action=back ignored: X display unavailable");
        return;
    }
    KeyCode escape = XKeysymToKeycode(display, XK_Escape);
    XTestFakeKeyEvent(display, escape, True, CurrentTime);
    XTestFakeKeyEvent(display, escape, False, CurrentTime);
    XFlush(display);
    log_message("action=back Escape press/release sent with XTest");
}

static void send_alt_left(void)
{
    Display *display = persistent_x();
    if (display == NULL) {
        log_message("action=back-browser ignored: X display unavailable");
        return;
    }
    KeyCode alt = XKeysymToKeycode(display, XK_Alt_L);
    KeyCode left = XKeysymToKeycode(display, XK_Left);
    XTestFakeKeyEvent(display, alt, True, CurrentTime);
    XTestFakeKeyEvent(display, left, True, CurrentTime);
    XTestFakeKeyEvent(display, left, False, CurrentTime);
    XTestFakeKeyEvent(display, alt, False, CurrentTime);
    XFlush(display);
    log_message("action=back-browser Alt+Left sent with XTest");
}

static void send_show_desktop(void);

static int is_browser_class(const char *value)
{
    if (value == NULL) return 0;
    char lower[256];
    size_t length = strlen(value);
    if (length >= sizeof(lower)) length = sizeof(lower)-1;
    for (size_t i = 0; i < length; ++i)
        lower[i] = (char)tolower((unsigned char)value[i]);
    lower[length] = '\0';
    static const char *names[] = {
        "netsurf", "surf", "vimb", "epiphany", "firefox",
        "chromium", "chrome", "navigator"
    };
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
        if (strstr(lower, names[i]) != NULL) return 1;
    }
    return 0;
}

static int active_window_class(char *buffer, size_t size)
{
    Display *display = persistent_x();
    if (display == NULL) {
        buffer[0] = '\0';
        return 0;
    }
    Window focus = None;
    int revert = 0;
    XGetInputFocus(display, &focus, &revert);
    if (focus == None || focus == PointerRoot) {
        buffer[0] = '\0';
        return 0;
    }
    XClassHint hint = {0};
    if (!XGetClassHint(display, focus, &hint)) {
        buffer[0] = '\0';
        return 0;
    }
    const char *value = hint.res_name;
    if (value == NULL) value = hint.res_class;
    if (value == NULL) {
        buffer[0] = '\0';
    } else {
        snprintf(buffer, size, "%s", value);
    }
    if (hint.res_name != NULL) XFree(hint.res_name);
    if (hint.res_class != NULL) XFree(hint.res_class);
    return buffer[0] != '\0';
}

static void send_show_desktop(void);
static void send_home(void);

static void context_back(void)
{
    char class_name[256];
    active_window_class(class_name, sizeof(class_name));
    FILE *table = fopen("/etc/fieldm/back-keys.conf", "r");
    if (table != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), table) != NULL) {
            char *cursor = line;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (*cursor == '\0' || *cursor == '#') continue;
            line[strcspn(line, "\r\n")] = '\0';
            char *equals = strchr(line, '=');
            if (equals == NULL) continue;
            *equals++ = '\0';
            char *key = line;
            char *action = equals;
            while (*key == ' ' || *key == '\t') ++key;
            char *end = key + strlen(key);
            while (end > key && (end[-1]==' ' || end[-1]=='\t')) *--end='\0';
            if (strstr(class_name, key) != NULL) {
                if (strcmp(action, "browser") == 0) {
                    fclose(table);
                    send_alt_left();
                    log_message("back action=browser wm_class=%s", class_name);
                    return;
                }
                if (strcmp(action, "home") == 0) {
                    fclose(table);
                    send_home();
                    log_message("back action=home wm_class=%s", class_name);
                    return;
                }
                fclose(table);
                send_escape_in_child();
                log_message("back action=escape wm_class=%s", class_name);
                return;
            }
        }
        fclose(table);
    }
    if (is_browser_class(class_name)) {
        send_alt_left();
        log_message("back action=browser-back wm_class=%s", class_name);
    } else if (strstr(class_name, "xterm") != NULL ||
               strstr(class_name, "XTerm") != NULL ||
               strstr(class_name, "htop") != NULL ||
               strstr(class_name, "btop") != NULL ||
               strstr(class_name, "fieldm-kbd") != NULL ||
               strstr(class_name, "simple-desktop") != NULL ||
               strstr(class_name, "field-desktop") != NULL ||
               strstr(class_name, "settings") != NULL ||
               strstr(class_name, "recents") != NULL) {
        if (strstr(class_name, "simple-desktop") != NULL ||
            strstr(class_name, "field-desktop") != NULL ||
            strstr(class_name, "Settings") != NULL ||
            strstr(class_name, "Recents") != NULL) {
            send_command_in_child(CMD_HOME);
        } else {
            send_home();
        }
        log_message("back action=home wm_class=%s", class_name);
    } else {
        send_escape_in_child();
        log_message("back action=escape wm_class=%s", class_name);
    }
}

static void send_show_desktop(void)
{
    Display *display = persistent_x();
    if (display == NULL) {
        log_message("action=home ignored: X display unavailable");
        return;
    }
    Atom showing = XInternAtom(display, "_NET_SHOWING_DESKTOP", False);
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = DefaultRootWindow(display);
    event.xclient.message_type = showing;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1;
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               (XEvent *)&event);
    XFlush(display);
    send_command_in_child(CMD_HOME);
    log_message("action=home sent _NET_SHOWING_DESKTOP");
}

static void send_home(void)
{
    send_show_desktop();
}

static void toggle_keyboard_manual(void)
{
    pid_t child = fork();
    if (child == 0) {
        execl("/usr/bin/fieldm-keyboard", "fieldm-keyboard",
              "manual", "toggle", (char *)NULL);
        _exit(127);
    }
    if (child < 0) {
        log_message("action=keyboard-toggle fork failed: %s", strerror(errno));
    } else {
        log_message("action=keyboard-toggle triggered");
    }
}


static void set_volume(int direction)
{
    pid_t child = fork();
    if (child == 0) {
        char *const arguments[] = {
            "/usr/bin/amixer", "set", "Master",
            direction > 0 ? "5%+" : "5%-", NULL
        };
        execv(arguments[0], arguments);
        _exit(127);
    }
    if (child > 0) {
        pid_t notice = fork();
        if (notice == 0) {
            execl("/usr/bin/fieldm-notify", "fieldm-notify",
                  direction > 0 ? "Volume up" : "Volume down",
                  (char *)NULL);
            _exit(127);
        }
    }
}

static int other_hardware_key_held(void)
{
    for (int device = 0; device < input_device_count; ++device) {
        for (int code = 1; code < BTN_MISC; ++code) {
            if (code != KEY_POWER && code != KEY_VOLUMEUP &&
                held_keys[device][code]) {
                return 1;
            }
        }
    }
    return 0;
}

static void reset_combo_if_released(void)
{
    if (!power_held && !volume_up_held && !volume_down_held) {
        combo_seen = 0;
        combo_triggered = 0;
        power_suppressed = 0;
        volume_up_suppressed = 0;
    }
}

static int process_is_target(pid_t pid)
{
    char path[64], comm[64], command[2048];
    snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    int found = fgets(comm, sizeof(comm), file) != NULL;
    fclose(file);
    if (!found) {
        return 0;
    }
    comm[strcspn(comm, "\r\n")] = '\0';
    if (strcmp(comm, "simple-desktop") == 0 ||
        strcmp(comm, "matchbox-window-manager") == 0 ||
        strcmp(comm, "fieldm-kbd") == 0) {
        return 1;
    }
    if (strcmp(comm, "Xorg") != 0 && strcmp(comm, "xinit") != 0 &&
        strcmp(comm, "startx") != 0 && strcmp(comm, "sh") != 0) {
        return 0;
    }
    if (!read_process_cmdline(pid, command, sizeof(command)) ||
        strstr(command, ":0") == NULL) {
        return 0;
    }
    return strcmp(comm, "sh") != 0 || strstr(command, "/startx") != NULL;
}

static int collect_session_processes(pid_t *pids, size_t capacity)
{
    DIR *proc = opendir("/proc");
    if (proc == NULL) {
        log_message("cannot inspect /proc: %s", strerror(errno));
        return 0;
    }
    size_t count = 0;
    struct dirent *entry;
    pid_t targets[256];
    size_t target_count = 0;
    while (count < capacity && (entry = readdir(proc)) != NULL) {
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 1 ||
            value == (long)getpid()) {
            continue;
        }
        if (process_is_target((pid_t)value)) {
            pids[count++] = (pid_t)value;
            if (target_count < sizeof(targets) / sizeof(targets[0])) {
                targets[target_count++] = (pid_t)value;
            }
        }
    }
    rewinddir(proc);
    while (count < capacity && (entry = readdir(proc)) != NULL) {
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 1 ||
            value == (long)getpid()) {
            continue;
        }
        pid_t candidate = (pid_t)value;
        pid_t parent = candidate;
        int descendant = 0;
        for (int depth = 0; depth < 32; ++depth) {
            char path[64], stat[1024];
            snprintf(path, sizeof(path), "/proc/%ld/stat", (long)parent);
            FILE *file = fopen(path, "r");
            if (file == NULL ||
                fgets(stat, sizeof(stat), file) == NULL) {
                if (file != NULL) fclose(file);
                break;
            }
            fclose(file);
            char *close_paren = strrchr(stat, ')');
            if (close_paren == NULL) {
                break;
            }
            unsigned long long dummy;
            char state;
            pid_t ppid = 0;
            if (sscanf(close_paren + 2, "%c %d %llu", &state,
                       &ppid, &dummy) != 3) {
                break;
            }
            int matches = 0;
            for (size_t i = 0; i < target_count; ++i) {
                if (parent == targets[i]) {
                    matches = 1;
                    break;
                }
            }
            if (matches) {
                descendant = 1;
                break;
            }
            if (ppid <= 1 || ppid == parent) {
                break;
            }
            parent = ppid;
        }
        if (descendant) {
            int duplicate = 0;
            for (size_t i = 0; i < count; ++i) {
                if (pids[i] == candidate) {
                    duplicate = 1;
                    break;
                }
            }
            if (!duplicate) {
                pids[count++] = candidate;
            }
        }
    }
    closedir(proc);
    return (int)count;
}

static void clear_keyboard_leases(void)
{
    static const char *const paths[] = {
        "/run/fieldm-keyboard/manual",
        "/run/fieldm-keyboard/entry",
        "/run/fieldm-keyboard/terminal",
        "/run/fieldm-keyboard/pid",
        "/run/fieldm-keyboard/hide-sequence"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (unlink(paths[i]) != 0 && errno != ENOENT) {
            log_message("cannot remove %s: %s", paths[i], strerror(errno));
        }
    }
}

static int old_xorg_alive(void)
{
    DIR *proc = opendir("/proc");
    if (proc == NULL) {
        return 0;
    }
    struct dirent *entry;
    int alive = 0;
    while ((entry = readdir(proc)) != NULL) {
        char path[64], comm[64];
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || value <= 1) {
            continue;
        }
        snprintf(path, sizeof(path), "/proc/%ld/comm", value);
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            if (fgets(comm, sizeof(comm), file) != NULL &&
                strcmp(comm, "Xorg\n") == 0) {
                alive = 1;
            }
            fclose(file);
        }
        if (alive) {
            break;
        }
    }
    closedir(proc);
    return alive;
}

static void screen_feedback(void)
{
    int brightness = read_integer_file(
        "/sys/class/backlight/panel/brightness", 134);
    if (brightness > 0) {
        write_integer_file("/sys/class/backlight/panel/brightness", 0);
        sleep_ms(120);
        write_integer_file("/sys/class/backlight/panel/brightness", brightness);
    } else {
        write_integer_file("/sys/class/graphics/fb0/blank", 0);
        sleep_ms(120);
        if (screen_blanked) {
            write_integer_file("/sys/class/graphics/fb0/blank", 1);
        }
    }
}

static void restart_gui_session(void)
{
    log_message("Power+VolumeUp held for 5 seconds; restarting FieldM");
    screen_feedback();
    clear_keyboard_leases();

    pid_t pids[256];
    int count = collect_session_processes(pids,
                                          sizeof(pids) / sizeof(pids[0]));
    for (int i = 0; i < count; ++i) {
        if (kill(pids[i], SIGTERM) != 0 && errno != ESRCH) {
            log_message("SIGTERM %ld failed: %s",
                        (long)pids[i], strerror(errno));
        }
    }
    unsigned long long deadline = monotonic_ms() + 2000;
    while (monotonic_ms() < deadline) {
        int remaining = 0;
        for (int i = 0; i < count; ++i) {
            if (kill(pids[i], 0) == 0 || errno == EPERM) {
                remaining = 1;
            }
        }
        if (!remaining) {
            break;
        }
        sleep_ms(50);
    }
    for (int i = 0; i < count; ++i) {
        if (kill(pids[i], 0) == 0 || errno == EPERM) {
            if (kill(pids[i], SIGKILL) == 0) {
                log_message("SIGKILL %ld after timeout", (long)pids[i]);
            }
        }
    }
    unsigned long long x_deadline = monotonic_ms() + 2000;
    while (old_xorg_alive() && monotonic_ms() < x_deadline) {
        sleep_ms(50);
    }
    if (old_xorg_alive()) {
        log_message("old Xorg is still present after bounded termination");
    } else {
        log_message("verified old Xorg gone");
    }
    FILE *mode = fopen("/run/zirarch-boot-mode", "w");
    if (mode != NULL) {
        fputs("gui\n", mode);
        fclose(mode);
    } else {
        log_message("cannot set boot mode to gui: %s", strerror(errno));
    }

    pid_t child = fork();
    if (child == 0) {
        execl("/sbin/rc-service", "rc-service",
              "zirarch-boot-mode", "restart", (char *)NULL);
        execl("/usr/sbin/rc-service", "rc-service",
              "zirarch-boot-mode", "restart", (char *)NULL);
        _exit(127);
    }
    if (child < 0) {
        log_message("cannot fork OpenRC restart: %s", strerror(errno));
    } else {
        log_message("requested rc-service zirarch-boot-mode restart");
        unsigned long long reap_deadline = monotonic_ms() + 2000;
        int status;
        pid_t reaped;
        for (;;) {
            reaped = waitpid(child, &status, WNOHANG);
            if (reaped == child) {
                if (WIFEXITED(status)) {
                    log_message("OpenRC restart child exit_status=%d",
                                WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    log_message("OpenRC restart child exit_signal=%d",
                                WTERMSIG(status));
                }
                break;
            }
            if (reaped < 0 && errno != EINTR) {
                break;
            }
            if (monotonic_ms() >= reap_deadline) {
                log_message("OpenRC restart child did not exit within 2 seconds");
                break;
            }
            sleep_ms(50);
        }
    }
}

static void take_screenshot(void)
{
    time_t now = time(NULL);
    struct tm local;
    char directory[128], path[512];
    localtime_r(&now, &local);
    mkdir("/root/Pictures", 0700);
    strftime(directory, sizeof(directory), "/root/Pictures/screenshot-%Y%m%d-%H%M%S.png",
             &local);
    snprintf(path, sizeof(path), "%s", directory);
    pid_t child = fork();
    if (child == 0) {
        setenv("DISPLAY", ":0", 1);
        execl("/usr/bin/scrot", "scrot", path, (char *)NULL);
        execl("/bin/scrot", "scrot", path, (char *)NULL);
        _exit(127);
    }
    pid_t flash = fork();
    if (flash == 0) {
        setenv("DISPLAY", ":0", 1);
        execl("/usr/bin/xsetroot", "xsetroot", "-solid", "#FFFFFF",
              (char *)NULL);
        _exit(127);
    }
    sleep_ms(80);
    flash = fork();
    if (flash == 0) {
        setenv("DISPLAY", ":0", 1);
        execl("/usr/bin/xsetroot", "xsetroot", "-solid", "#161513",
              (char *)NULL);
        _exit(127);
    }
    log_message("action=screenshot path=%s", path);
}

static void update_key_state(int device, unsigned int code, int value,
                             unsigned long long now)
{
    const char *action = "ignore";
    if (value == 2) {
        log_message("key event device=%d code=%u value=repeat action=ignored", device, code);
        return;
    }
    int is_new_press = value == 1 &&
        code <= KEY_MAX && !held_keys[device][code];
    if (code <= KEY_MAX && value == 1) {
        held_keys[device][code] = 1;
    } else if (code <= KEY_MAX && value == 0) {
        held_keys[device][code] = 0;
    }
    if (value != 0 && value != 1) {
        return;
    }
    if (screen_blanked && code == KEY_HOME && value == 1) {
        toggle_power();
        send_command_in_child(CMD_HOME);
        log_message("key event device=%d code=%u value=%d action=blanked-home", device, code, value);
        return;
    }
    if (screen_blanked && code != KEY_POWER &&
        code != KEY_VOLUMEUP && code != KEY_HOME) {
        return;
    }
    if (value == 1 && code != KEY_POWER && code != KEY_VOLUMEUP) {
        if (power_held) power_suppressed = 1;
        if (volume_up_held) volume_up_suppressed = 1;
    }

    if (code == KEY_POWER) {
        if (value == 1 && is_new_press) {
            power_held = 1;
            power_pressed_at = now;
            power_suppressed = other_hardware_key_held();
            if (volume_up_held) {
                combo_seen = 1;
                combo_started_at = now;
                power_suppressed = 1;
                volume_up_suppressed = 1;
            }
            if (volume_down_held) {
                combo_seen = 1;
                combo_started_at = now;
                power_suppressed = 1;
                take_screenshot();
            }
        } else if (value == 0 && power_held) {
            power_held = 0;
            unsigned long long duration = now - power_pressed_at;
            if (!combo_seen && !power_suppressed &&
                duration < POWER_TAP_MS && !other_hardware_key_held()) {
                toggle_power();
                send_command_in_child(CMD_POWER);
                action = "power-tap";
            } else {
                action = "power-release-suppressed";
            }
            reset_combo_if_released();
        }
    } else if (code == KEY_VOLUMEUP) {
        if (value == 1 && is_new_press) {
            volume_up_held = 1;
            volume_up_suppressed = other_hardware_key_held();
            if (power_held) {
                combo_seen = 1;
                combo_started_at = now;
                power_suppressed = 1;
                volume_up_suppressed = 1;
            }
        } else if (value == 0 && volume_up_held) {
            volume_up_held = 0;
            if (!combo_seen && !volume_up_suppressed &&
                !other_hardware_key_held()) {
                set_volume(1);
                action = "volume-up-release";
            } else {
                action = "volume-up-release-suppressed";
            }
            reset_combo_if_released();
        }
    } else if (code == KEY_BACK || code == KEY_ESC) {
        if (value == 1 && is_new_press) {
            back_held = 1;
            back_pressed_at = now;
            back_toggle_triggered = 0;
            action = "back-press";
        } else if (value == 0 && back_held) {
            back_held = 0;
            if (back_toggle_triggered) {
                action = "back-long-toggle";
            } else {
                context_back();
                action = "back-context";
            }
            back_pressed_at = 0;
            back_toggle_triggered = 0;
        }
    } else if (value == 1 &&
               (code == KEY_HOME || code == KEY_HOMEPAGE)) {
        send_show_desktop();
        action = "home-show-desktop";
    } else if (value == 1 &&
               (code == KEY_MENU || code == KEY_RECENT_SAMSUNG ||
                code == KEY_APPSELECT_SAMSUNG)) {
        send_command_in_child(CMD_RECENTS);
        action = "recents-open";
    } else if (code == KEY_VOLUMEDOWN) {
        if (value == 1 && is_new_press) {
            volume_down_held = 1;
            if (power_held) {
                combo_seen = 1;
                combo_started_at = now;
                power_suppressed = 1;
                take_screenshot();
            }
        } else if (value == 0 && volume_down_held) {
            volume_down_held = 0;
            if (!combo_seen && !power_held) {
                set_volume(-1);
                action = "volume-down";
            } else {
                action = "volume-down-suppressed";
            }
            reset_combo_if_released();
        }
    }
    log_message("key event device=%d code=%u value=%d action=%s",
            device, code, value, action);
}

static void check_restart_combo(unsigned long long now)
{
    if (power_held && volume_up_held && combo_seen && !combo_triggered &&
        now - combo_started_at >= RESTART_HOLD_MS) {
        combo_triggered = 1;
        restart_gui_session();
    }
    if (back_held && !back_toggle_triggered &&
        back_pressed_at != 0 &&
        now - back_pressed_at >= 600) {
        back_toggle_triggered = 1;
        toggle_keyboard_manual();
        log_message("action=back-long keyboard-toggle");
    }
}

static int device_name(const char *event_name, char *name, size_t size)
{
    char path[256];
    int length = snprintf(path, sizeof(path),
                          "/sys/class/input/%s/device/name", event_name);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return 0;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    int found = fgets(name, (int)size, file) != NULL;
    fclose(file);
    if (found) {
        name[strcspn(name, "\r\n")] = '\0';
    }
    return found;
}

static void remove_input_device(int index)
{
    if (index < 0 || index >= input_device_count) {
        return;
    }
    if (touchscreen_fd == input_devices[index].fd) {
        touchscreen_fd = -1;
        touchscreen_grabbed = 0;
    }
    close(input_devices[index].fd);
    log_message("closed unavailable input device %s",
                input_devices[index].path);
    memset(held_keys[index], 0, sizeof(held_keys[index]));
    for (int i = index; i + 1 < input_device_count; ++i) {
        input_devices[i] = input_devices[i + 1];
        memcpy(held_keys[i], held_keys[i + 1], sizeof(held_keys[i]));
    }
    --input_device_count;
    memset(&input_devices[input_device_count], 0,
           sizeof(input_devices[input_device_count]));
    memset(held_keys[input_device_count], 0,
           sizeof(held_keys[input_device_count]));
}

static void open_input_devices(void)
{
    DIR *dir = opendir("/dev/input");
    if (dir == NULL) {
        log_message("cannot open /dev/input: %s", strerror(errno));
        return;
    }
    struct dirent *entry;
    while (input_device_count < MAX_INPUT_DEVICES &&
           (entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        char path[320], name[128] = "";
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        int existing = 0;
        for (int i = 0; i < input_device_count; ++i) {
            if (strcmp(input_devices[i].path, path) == 0) {
                existing = 1;
                break;
            }
        }
        if (existing) {
            close(fd);
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
        int touchscreen = device_name(entry->d_name, name, sizeof(name)) &&
                          strcmp(name, "sec_touchscreen") == 0;
        input_devices[input_device_count].fd = fd;
        input_devices[input_device_count].touchscreen = touchscreen;
        snprintf(input_devices[input_device_count].path,
                 sizeof(input_devices[input_device_count].path), "%s", path);
        memset(held_keys[input_device_count], 0,
               sizeof(held_keys[input_device_count]));
        if (touchscreen) {
            touchscreen_fd = fd;
        }
        log_message("monitoring %s (%s)", path,
                    touchscreen ? "touchscreen" : "key device");
        ++input_device_count;
    }
    closedir(dir);
    if (touchscreen_fd < 0) {
        log_message("sec_touchscreen input device was not found");
    } else if (screen_blanked) {
        set_touchscreen_grab(1);
    }
}

int main(void)
{
    struct stat log_stat;
    if (stat("/var/log/fieldm-keys.log", &log_stat) == 0 &&
        log_stat.st_size > 1048576) {
        rename("/var/log/fieldm-keys.log",
               "/var/log/fieldm-keys.log.1");
    }
    signal(SIGCHLD, SIG_DFL);
    log_file = fopen("/var/log/fieldm-keys.log", "a");
    if (log_file == NULL) {
        perror("fieldm-keys: cannot open /var/log/fieldm-keys.log");
        return 1;
    }
    FILE *state = fopen("/run/fieldm-screen-blanked", "r");
    screen_blanked = state != NULL;
    if (state != NULL) {
        fclose(state);
    }
    open_input_devices();
    persistent_x();
    log_message("started independently of X; blanked=%d", screen_blanked);

    struct pollfd polls[MAX_INPUT_DEVICES];
    unsigned long long refresh_deadline = monotonic_ms() + 1000;
    for (;;) {
        reap_children();
        if (screen_blanked &&
            access("/run/fieldm-unlock-request", F_OK) == 0) {
            unlink("/run/fieldm-unlock-request");
            toggle_power();
            send_command_in_child(CMD_HOME);
            log_message("lock-screen action=unlocked");
        }
        for (int i = 0; i < input_device_count; ++i) {
            polls[i].fd = input_devices[i].fd;
            polls[i].events = POLLIN;
            polls[i].revents = 0;
        }
        int ready = poll(polls, (nfds_t)input_device_count, 100);
        if (ready < 0 && errno != EINTR) {
            log_message("input poll failed: %s", strerror(errno));
            sleep_ms(100);
        }
        if (ready > 0) {
            for (int i = 0; i < input_device_count; ++i) {
                if (polls[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    remove_input_device(i);
                    --i;
                    continue;
                }
                if (!(polls[i].revents & POLLIN)) {
                    continue;
                }
                struct input_event events[32];
                ssize_t size;
                while ((size = read(input_devices[i].fd, events,
                                    sizeof(events))) > 0) {
                    size_t event_count = (size_t)size / sizeof(events[0]);
                    for (size_t j = 0; j < event_count; ++j) {
                        if (events[j].type != EV_KEY) {
                            continue;
                        }
                        update_key_state(i, events[j].code,
                                         events[j].value, monotonic_ms());
                        check_restart_combo(monotonic_ms());
                    }
                }
            }
        }
        unsigned long long now = monotonic_ms();
        check_restart_combo(now);
        if (now >= refresh_deadline) {
            open_input_devices();
            refresh_deadline = now + 1000;
        }
    }
}
