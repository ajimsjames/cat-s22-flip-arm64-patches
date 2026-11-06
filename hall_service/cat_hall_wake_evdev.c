/*
 * Cat S22 Flip Native Hall Sensor Daemon (cat_hall_wake)
 * 
 * Directly reads Linux input event nodes (/dev/input/event*) to find
 * the "hall_switch" input device with EV_SW / SW_LID capability.
 * When the flip opens or closes, it dispatches standard Android power keyevents:
 *   - Flip Open  (SW_LID = 0) -> KEYCODE_WAKEUP (224)
 *   - Flip Close (SW_LID = 1) -> KEYCODE_SLEEP  (223)
 *
 * Runs efficiently via poll() with 0% CPU overhead.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/wait.h>

static void send_key(const char *code) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/system/bin/input", "input", "keyevent", code, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
}

static int find_hall_event_node(void) {
    DIR *dir = opendir("/dev/input");
    if (!dir)
        return -1;

    struct dirent *ent;
    char path[64];
    char name[128];
    int hall_fd = -1;

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;

        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
            if (strstr(name, "hall") || strstr(name, "Hall")) {
                hall_fd = fd;
                closedir(dir);
                return hall_fd;
            }
        }
        close(fd);
    }

    closedir(dir);
    return -1;
}

int main(void) {
    /* Export GPIO 1190 (GPIO 44 + base 1146) for fallback polling if hall_switch input node is not registered */
    int efd = open("/sys/class/gpio/export", O_WRONLY);
    if (efd >= 0) {
        write(efd, "1190\n", 5);
        close(efd);
    }
    int edge_fd = open("/sys/class/gpio/gpio1190/edge", O_WRONLY);
    if (edge_fd >= 0) {
        write(edge_fd, "both\n", 5);
        close(edge_fd);
    }

    int last_gpio_state = -1;

    for (;;) {
        int fd = find_hall_event_node();
        if (fd < 0) {
            /* Fallback: monitor /sys/class/gpio/gpio1190/value via POLLPRI / read */
            int gfd = open("/sys/class/gpio/gpio1190/value", O_RDONLY);
            if (gfd >= 0) {
                char buf[4] = {0};
                if (read(gfd, buf, 1) == 1) {
                    int state = (buf[0] == '1') ? 1 : 0; /* 1 = open, 0 = closed */
                    if (last_gpio_state != -1 && state != last_gpio_state) {
                        if (state == 1) {
                            send_key("224"); /* KEYCODE_WAKEUP */
                        } else {
                            send_key("223"); /* KEYCODE_SLEEP */
                        }
                    }
                    last_gpio_state = state;
                }
                struct pollfd gpfd = { .fd = gfd, .events = POLLPRI | POLLERR };
                lseek(gfd, 0, SEEK_SET);
                read(gfd, buf, sizeof(buf));
                poll(&gpfd, 1, 500);
                close(gfd);
                continue;
            }
            sleep(1);
            continue;
        }

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        struct input_event ev;
        while (poll(&pfd, 1, -1) > 0) {
            ssize_t n = read(fd, &ev, sizeof(ev));
            if (n < (ssize_t)sizeof(ev)) {
                break;
            }

            if (ev.type == EV_SW && ev.code == SW_LID) {
                if (ev.value == 0) {
                    /* Lid opened */
                    send_key("224"); /* KEYCODE_WAKEUP */
                } else {
                    /* Lid closed */
                    send_key("223"); /* KEYCODE_SLEEP */
                }
            }
        }

        close(fd);
        sleep(1);
    }

    return 0;
}
