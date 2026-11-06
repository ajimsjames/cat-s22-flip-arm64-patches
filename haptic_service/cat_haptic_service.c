/*
 * Cat S22 Flip Smart Haptic Feedback Daemon (cat_haptic_service)
 * 
 * - Listens directly to physical keypad event device (/dev/input/event1 and /dev/input/event7).
 * - Fires a crisp 25ms tactile vibration on keypress down.
 * - Hardware vibrator path: /sys/class/leds/vibrator/duration + activate
 *   Fallback: cmd vibrator_manager synced oneshot 25
 * - Zero lag, lightweight event loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/wait.h>

#define VIB_DURATION_NODE "/sys/class/leds/vibrator/duration"
#define VIB_ACTIVATE_NODE "/sys/class/leds/vibrator/activate"

static int g_has_sysfs_vib = -1;

static void trigger_haptic(int ms) {
    if (g_has_sysfs_vib != 0) {
        int fd_dur = open(VIB_DURATION_NODE, O_WRONLY);
        int fd_act = open(VIB_ACTIVATE_NODE, O_WRONLY);
        if (fd_dur >= 0 && fd_act >= 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d\n", ms);
            write(fd_dur, buf, strlen(buf));
            write(fd_act, "1\n", 2);
            close(fd_dur);
            close(fd_act);
            g_has_sysfs_vib = 1;
            return;
        }
        if (fd_dur >= 0) close(fd_dur);
        if (fd_act >= 0) close(fd_act);
        g_has_sysfs_vib = 0;
    }

    /* Fallback via Android cmd */
    pid_t pid = fork();
    if (pid == 0) {
        char ms_str[16];
        snprintf(ms_str, sizeof(ms_str), "%d", ms);
        execl("/system/bin/cmd", "cmd", "vibrator_manager", "synced", "oneshot", ms_str, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    /* Open keypad input devices */
    int fds[4];
    int num_fds = 0;

    const char *devs[] = {
        "/dev/input/event1", /* soc:matrix_keypad */
        "/dev/input/event7", /* Cat_Keypad_Clean */
        "/dev/input/event0", /* gpio-keys */
        "/dev/input/event2"  /* keypad */
    };

    struct pollfd pfd[4];

    for (size_t i = 0; i < sizeof(devs)/sizeof(devs[0]); i++) {
        int fd = open(devs[i], O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            fds[num_fds] = fd;
            pfd[num_fds].fd = fd;
            pfd[num_fds].events = POLLIN;
            num_fds++;
        }
    }

    if (num_fds == 0) {
        /* Retry after delay if input subsystem isn't ready yet */
        sleep(2);
        return 1;
    }

    struct input_event ev;

    for (;;) {
        int ret = poll(pfd, num_fds, -1);
        if (ret <= 0) continue;

        for (int i = 0; i < num_fds; i++) {
            if (pfd[i].revents & POLLIN) {
                while (read(pfd[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    /* Only on key press DOWN (value == 1) */
                    if (ev.type == EV_KEY && ev.value == 1) {
                        /* Trigger 20ms crisp haptic */
                        trigger_haptic(20);
                    }
                }
            }
        }
    }

    return 0;
}
