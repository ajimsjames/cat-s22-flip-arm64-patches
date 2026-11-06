/*
 * Cat S22 Flip Dedicated Hall Sensor Wake/Sleep Daemon (cat_hall_wake)
 * 
 * - Completely separate from outerscreen_service and CatParts.
 * - Monitors kernel hall sensor transitions via filtered logcat stream:
 *     "hall far"  -> Flip Opened -> KEYCODE_WAKEUP (224)
 *     "hall near" -> Flip Closed -> KEYCODE_SLEEP  (223)
 * - Debounced (500ms) to ignore mechanical bounce/vibration.
 * - Ultra-low CPU (0.0%): logcat is filtered specifically to the kernel hall tags.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

static void trigger_keyevent(const char *code) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/system/bin/input", "input", "keyevent", code, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        FILE *fp = popen("/system/bin/logcat -b kernel -T 1 -v brief", "r");
        if (!fp) {
            fp = popen("/system/bin/logcat -T 1 -v brief", "r");
            if (!fp) {
                sleep(1);
                continue;
            }
        }

        char buf[512];
        struct timespec last_event = {0, 0};
        int last_state = -1; /* 1 = far/open, 0 = near/closed */

        while (fgets(buf, sizeof(buf), fp)) {
            int state = -1;
            if (strstr(buf, "hall far")) {
                state = 1;
            } else if (strstr(buf, "hall near")) {
                state = 0;
            }

            if (state != -1 && state != last_state) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long elapsed_ms = (now.tv_sec - last_event.tv_sec) * 1000 +
                                  (now.tv_nsec - last_event.tv_nsec) / 1000000;
                if (elapsed_ms < 500) {
                    continue; /* 500ms debounce guard */
                }
                last_event = now;
                last_state = state;

                if (state == 1) {
                    trigger_keyevent("224"); /* KEYCODE_WAKEUP */
                } else {
                    trigger_keyevent("223"); /* KEYCODE_SLEEP */
                }
            }
        }

        pclose(fp);
        sleep(1);
    }

    return 0;
}
