#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

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
    CMD_BACK = 2,
    CMD_HOME = 3,
    CMD_RECENTS = 4,
    CMD_POWER = 6,
    CMD_CLOSE_FOCUSED = 7,
    CMD_APP_BACK = 8,
    MAX_INPUT_DEVICES = 64,
    RESTART_HOLD_MS = 5000,
    POWER_TAP_MS = 1000
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
static int combo_seen;
static int combo_triggered;
static int power_suppressed;
static int volume_up_suppressed;
static unsigned long long power_pressed_at;
static unsigned long long combo_started_at;

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
        log_message("screen blanked");
    } else {
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

static void send_command_in_child(long command)
{
    pid_t child = fork();
    if (child != 0) {
        return;
    }
    Display *display = open_x_display();
    if (display != NULL) {
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
        }
        XCloseDisplay(display);
    }
    _exit(0);
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
    if (!power_held && !volume_up_held) {
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
        strcmp(comm, "simple-wm") == 0 ||
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

static void update_key_state(int device, unsigned int code, int value,
                             unsigned long long now)
{
    if (value == 2) {
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
        } else if (value == 0 && power_held) {
            power_held = 0;
            unsigned long long duration = now - power_pressed_at;
            if (!combo_seen && !power_suppressed &&
                duration < POWER_TAP_MS && !other_hardware_key_held()) {
                toggle_power();
                send_command_in_child(CMD_POWER);
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
            }
            reset_combo_if_released();
        }
    } else if (value == 1 && (code == KEY_BACK || code == KEY_ESC)) {
        send_command_in_child(CMD_APP_BACK);
    } else if (value == 1 && code == KEY_HOME) {
        send_command_in_child(CMD_CLOSE_FOCUSED);
    } else if (value == 1 && code == KEY_MENU) {
        send_command_in_child(CMD_RECENTS);
    } else if (value == 1 && code == KEY_VOLUMEDOWN) {
        set_volume(-1);
    }
}

static void check_restart_combo(unsigned long long now)
{
    if (power_held && volume_up_held && combo_seen && !combo_triggered &&
        now - combo_started_at >= RESTART_HOLD_MS) {
        combo_triggered = 1;
        restart_gui_session();
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
    log_message("started independently of X; blanked=%d", screen_blanked);

    struct pollfd polls[MAX_INPUT_DEVICES];
    unsigned long long refresh_deadline = monotonic_ms() + 1000;
    for (;;) {
        reap_children();
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
