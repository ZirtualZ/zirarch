#define _GNU_SOURCE

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef KEY_VOLUMEDOWN
#define KEY_VOLUMEDOWN 114
#endif

int main(void)
{
    DIR *dir = opendir("/sys/class/input");
    if (dir == NULL)
        return 1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;
        char name_path[512], name[128] = "";
        snprintf(name_path, sizeof(name_path),
                 "/sys/class/input/%s/device/name",
                 entry->d_name);
        int namefd = open(name_path, O_RDONLY);
        if (namefd >= 0) {
            ssize_t count = read(namefd, name, sizeof(name) - 1);
            if (count > 0)
                name[count] = '\0';
            close(namefd);
        }
        if (strstr(name, "gpio") == NULL &&
            strstr(name, "gpio_keys") == NULL &&
            strstr(name, "gpio-keys") == NULL)
            continue;
        char device[512];
        snprintf(device, sizeof(device),
                 "/dev/input/%s", entry->d_name);
        int fd = open(device, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        unsigned char key_bits[(KEY_MAX + 1) / 8 + 1] = {0};
        if (ioctl(fd, EVIOCGKEY(sizeof(key_bits)), key_bits) >= 0 &&
            (key_bits[KEY_VOLUMEDOWN / 8] &
             (1U << (KEY_VOLUMEDOWN % 8))) != 0) {
            close(fd);
            closedir(dir);
            return 0;
        }
        close(fd);
    }
    closedir(dir);
    return 1;
}
