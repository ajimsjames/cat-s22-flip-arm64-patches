/*
 * Cat S22 Flip Outer Display Hardware & Service Watchdog Daemon
 * Checks outerscreen_service heartbeat every 2 seconds.
 * If frozen, hung, or heartbeat stale > 10 seconds:
 *   - Kills stalled service
 *   - Re-initializes /dev/graphics/fb2 unblank and lcd-backlight2
 *   - Restarts outerscreen_service cleanly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <signal.h>
#include <dirent.h>

#define HEARTBEAT_FILE "/dev/shm_outerscreen_heartbeat"
#define CONFIG_FILE    "/dev/shm_outerscreen_cmd"
#define FB_DEV         "/dev/graphics/fb2"
#define BACKLIGHT_NODE "/sys/devices/platform/soc/1a00000.qcom,mdss_mdp/1a00000.qcom,mdss_mdp:qcom,mdss_fb_secondary/leds/lcd-backlight2/brightness"
#define BACKLIGHT_ALT  "/sys/class/leds/lcd-backlight2/brightness"
#define TIMEOUT_SEC    15

static int is_outer_enabled_in_config(void) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) return 1;
    char line[128];
    int enabled = 1;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "outer_enabled=", 14) == 0) {
            enabled = atoi(line + 14);
        }
    }
    fclose(fp);
    return enabled;
}

/* Direct /proc scan — avoids system() + waitpid() failing when SIGCHLD == SIG_IGN */
static int is_outerscreen_running(void) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            char buf[32] = {0};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0 && strncmp(buf, "outerscreen_ser", 15) == 0) {
                found = 1;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

static void reset_hardware(void) {
    int bfd = open(BACKLIGHT_NODE, O_WRONLY);
    if (bfd < 0) bfd = open(BACKLIGHT_ALT, O_WRONLY);
    if (bfd >= 0) {
        write(bfd, "255\n", 4);
        close(bfd);
    }

    int fbfd = open(FB_DEV, O_RDWR);
    if (fbfd >= 0) {
        ioctl(fbfd, FBIOBLANK, FB_BLANK_UNBLANK);
        close(fbfd);
    }
}

static time_t get_heartbeat(void) {
    struct stat st;
    if (stat(HEARTBEAT_FILE, &st) == 0) {
        return st.st_mtime;
    }
    return 0;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    // Initial grace period for system to stabilize after boot
    sleep(12);

    for (;;) {
        sleep(2);

        if (!is_outer_enabled_in_config()) {
            continue;
        }

        time_t now = time(NULL);
        time_t hb = get_heartbeat();

        // Check if outerscreen_service process exists via /proc
        int proc_running = is_outerscreen_running();

        if (!proc_running || (hb > 0 && (now - hb) > TIMEOUT_SEC)) {
            // Heartbeat expired or process died -> Auto Recovery
            system("killall -9 outerscreen_service 2>/dev/null");
            usleep(200000);

            // Re-arm hardware
            reset_hardware();

            // Refresh heartbeat timestamp to give restart a fresh window
            close(open(HEARTBEAT_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0666));

            // Start service via init
            system("start outerscreen");

            // Sleep grace period
            sleep(5);
        }
    }

    return 0;
}
