/*
 * Cat S22 Flip Keypad Light & Hall Sensor Controller Daemon
 * - Monitors /dev/input/event3 (hall_sensor) + /dev/input/event* (keypad activity)
 * - Reads user settings from /dev/shm_outerscreen_cmd (written by CatParts Settings app):
 *     keylight_enabled=<1|0>
 *     keylight_brightness=<0..255>
 *     keylight_timeout=<0|5|10|30|60>  (0 = stay on while flip open)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <sys/stat.h>
#include <linux/input.h>

#define HALL_DEV        "/dev/input/event3"
#define KEYLIGHT_SYSFS  "/sys/devices/platform/soc/soc:keyboard_light/leds/keyboard_light/brightness"
#define KEYLIGHT_ALT    "/sys/class/leds/keyboard_light/brightness"
#define CONFIG_FILE     "/dev/shm_outerscreen_cmd"

static int cfg_kl_enabled    = 1;
static int cfg_kl_brightness = 255;
static int cfg_kl_timeout    = 0; /* 0 = stay on while flip open */
static int lid_open          = 1;
static int current_led_val   = -1;

static void set_keypad_light(int val) {
    if (val == current_led_val) return;
    int fd = open(KEYLIGHT_SYSFS, O_WRONLY);
    if (fd < 0) fd = open(KEYLIGHT_ALT, O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%d\n", val);
        write(fd, buf, len);
        close(fd);
        current_led_val = val;
    }
}

static void load_config(void) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) return;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (strncmp(line, "keylight_enabled=", 17) == 0) {
            cfg_kl_enabled = atoi(line + 17);
        } else if (strncmp(line, "keylight_brightness=", 20) == 0) {
            cfg_kl_brightness = atoi(line + 20);
            if (cfg_kl_brightness < 0) cfg_kl_brightness = 0;
            if (cfg_kl_brightness > 255) cfg_kl_brightness = 255;
        } else if (strncmp(line, "keylight_timeout=", 17) == 0) {
            cfg_kl_timeout = atoi(line + 17);
        }
    }
    fclose(fp);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    load_config();

    struct pollfd fds[16];
    int num_fds = 0;
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            fds[num_fds].fd = fd;
            fds[num_fds].events = POLLIN;
            num_fds++;
        }
    }

    time_t last_activity = time(NULL);
    time_t last_cfg_mtime = 0;

    set_keypad_light(cfg_kl_enabled ? cfg_kl_brightness : 0);

    for (;;) {
        int ret = poll(fds, num_fds, 1000);
        time_t now = time(NULL);

        struct stat st;
        if (stat(CONFIG_FILE, &st) == 0 && st.st_mtime != last_cfg_mtime) {
            last_cfg_mtime = st.st_mtime;
            load_config();
            last_activity = now;
            current_led_val = -1; /* force refresh */
        }

        if (ret > 0) {
            for (int i = 0; i < num_fds; i++) {
                if (fds[i].revents & POLLIN) {
                    struct input_event ev;
                    while (read(fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                        /* EV_SW = 0x05, SW_LID = 0x00 */
                        if (ev.type == 0x05 && ev.code == 0x00) {
                            lid_open = (ev.value != 0) ? 1 : 0;
                            last_activity = now;
                            current_led_val = -1;
                        } else if (ev.type == EV_KEY && ev.value == 1) {
                            /* Physical key press wakes keypad backlight if lid is open */
                            last_activity = now;
                        }
                    }
                }
            }
        }

        if (!lid_open || !cfg_kl_enabled || cfg_kl_brightness == 0) {
            set_keypad_light(0);
        } else if (cfg_kl_timeout > 0 && (now - last_activity) >= cfg_kl_timeout) {
            set_keypad_light(0);
        } else {
            set_keypad_light(cfg_kl_brightness);
        }
    }

    for (int i = 0; i < num_fds; i++) close(fds[i].fd);
    return 0;
}
