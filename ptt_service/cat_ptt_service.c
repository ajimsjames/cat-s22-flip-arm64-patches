/*
 * Cat S22 Flip PTT Hardware Service + CatParts Settings IPC Bridge
 * - Listens to PTT button (KEY_NUMERIC_B / 523) on /dev/input/event*
 * - Reads live user preferences from /dev/shm_outerscreen_cmd (written by CatParts Settings app)
 *   Format:
 *     ptt_short=<torch|camera|assistant|dnd|outer_toggle|none>
 *     ptt_long=<outer_toggle|torch|camera|assistant|dnd|none>
 *     ptt_incall=<answer_spk|answer_ear|mute|none>
 *     outer_enabled=<1|0>
 *     outer_brightness=<25..255>
 *     outer_footer=<logcat|status|off>
 *     keylight_brightness=<0..255>
 *     apply=<counter>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/input.h>

#define PTT_KEY_CODE_1     523  /* KEY_NUMERIC_B (0x20b) */
#define PTT_KEY_CODE_2     525  /* Raw matrix row=4 col=3 keycode (0x20d) */
#define SHORT_PRESS_MS     500
#define LONG_LONG_PRESS_MS 2000
#define CONFIG_FILE        "/dev/shm_outerscreen_cmd"
#define KEYLIGHT_SYSFS     "/sys/devices/platform/soc/soc:keyboard_light/leds/keyboard_light/brightness"
#define KEYLIGHT_ALT       "/sys/class/leds/keyboard_light/brightness"

/* Configurable settings with defaults */
static char cfg_ptt_short[32]  = "torch";
static char cfg_ptt_long[32]   = "outer_toggle";
static char cfg_ptt_incall[32] = "answer_spk";
static int  cfg_outer_enabled  = 1;
static int  cfg_keylight_br    = 255;
static int  last_apply_id      = -1;

/* Toggle: 0 = outerscreen is running, 1 = outerscreen killed (next press restarts) */
static int outerscreen_killed = 0;
static char cfg_outer_mode[32] = "flip_closed";
static int cfg_outer_br       = 255;
static char cfg_outer_footer[16] = "logcat";
static int cfg_kl_enabled     = 1;
static int cfg_kl_timeout     = 0;

static void set_keylight_hw(int val) {
    int fd = open(KEYLIGHT_SYSFS, O_WRONLY);
    if (fd < 0) fd = open(KEYLIGHT_ALT, O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%d\n", val);
        write(fd, buf, len);
        close(fd);
    }
}

static void save_config(void) {
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) return;
    fprintf(fp, "ptt_short=%s\n", cfg_ptt_short);
    fprintf(fp, "ptt_long=%s\n", cfg_ptt_long);
    fprintf(fp, "ptt_incall=%s\n", cfg_ptt_incall);
    fprintf(fp, "outer_mode=%s\n", cfg_outer_mode);
    fprintf(fp, "outer_enabled=%d\n", cfg_outer_enabled);
    fprintf(fp, "outer_brightness=%d\n", cfg_outer_br);
    fprintf(fp, "outer_footer=%s\n", cfg_outer_footer);
    fprintf(fp, "keylight_enabled=%d\n", cfg_kl_enabled);
    fprintf(fp, "keylight_brightness=%d\n", cfg_keylight_br);
    fprintf(fp, "keylight_timeout=%d\n", cfg_kl_timeout);
    fprintf(fp, "apply=%d\n", last_apply_id);
    fclose(fp);
}

/* Turn off outer display cleanly via config + stop init services */
static void outerscreen_kill_all(void) {
    cfg_outer_enabled = 0;
    system("stop cat_watchdog 2>/dev/null; stop outerscreen 2>/dev/null");
    int fd = open("/sys/class/leds/lcd-backlight2/brightness", O_WRONLY);
    if (fd >= 0) { write(fd, "0\n", 2); close(fd); }
    outerscreen_killed = 1;
}

/* Restart outerscreen_service and cat_watchdog via init */
static void outerscreen_restart(void) {
    cfg_outer_enabled = 1;
    int fd = open("/sys/class/leds/lcd-backlight2/brightness", O_WRONLY);
    if (fd >= 0) { write(fd, "255\n", 4); close(fd); }
    system("start outerscreen 2>/dev/null; start cat_watchdog 2>/dev/null");
    outerscreen_killed = 0;
}

static void load_config(void) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) return;

    char line[128];
    int apply_id = last_apply_id;
    int prev_outer = cfg_outer_enabled;
    int prev_kl = cfg_keylight_br;

    while (fgets(line, sizeof(line), fp)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (strncmp(line, "ptt_short=", 10) == 0) {
            snprintf(cfg_ptt_short, sizeof(cfg_ptt_short), "%s", line + 10);
        } else if (strncmp(line, "ptt_long=", 9) == 0) {
            snprintf(cfg_ptt_long, sizeof(cfg_ptt_long), "%s", line + 9);
        } else if (strncmp(line, "ptt_incall=", 11) == 0) {
            snprintf(cfg_ptt_incall, sizeof(cfg_ptt_incall), "%s", line + 11);
        } else if (strncmp(line, "outer_mode=", 11) == 0) {
            snprintf(cfg_outer_mode, sizeof(cfg_outer_mode), "%s", line + 11);
        } else if (strncmp(line, "outer_enabled=", 14) == 0) {
            cfg_outer_enabled = atoi(line + 14);
        } else if (strncmp(line, "outer_brightness=", 17) == 0) {
            cfg_outer_br = atoi(line + 17);
        } else if (strncmp(line, "outer_footer=", 13) == 0) {
            snprintf(cfg_outer_footer, sizeof(cfg_outer_footer), "%s", line + 13);
        } else if (strncmp(line, "keylight_enabled=", 17) == 0) {
            cfg_kl_enabled = atoi(line + 17);
        } else if (strncmp(line, "keylight_brightness=", 20) == 0) {
            cfg_keylight_br = atoi(line + 20);
        } else if (strncmp(line, "keylight_timeout=", 17) == 0) {
            cfg_kl_timeout = atoi(line + 17);
        } else if (strncmp(line, "apply=", 6) == 0) {
            apply_id = atoi(line + 6);
        }
    }
    fclose(fp);

    if (apply_id != last_apply_id) {
        last_apply_id = apply_id;
        if (cfg_outer_enabled != prev_outer) {
            if (cfg_outer_enabled) {
                outerscreen_restart();
            } else {
                outerscreen_kill_all();
            }
        }
        if (cfg_keylight_br != prev_kl) {
            set_keylight_hw(cfg_keylight_br);
        }
    }
}

static int get_call_state(char *number_out, size_t max_len) {
    /* Returns: 0 = IDLE, 1 = RINGING, 2 = OFFHOOK */
    FILE *fp = popen("dumpsys telephony.registry 2>/dev/null", "r");
    if (!fp) return 0;

    char line[256];
    int state = 0;
    int lines_read = 0;
    if (number_out && max_len > 0) number_out[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        lines_read++;
        if (lines_read <= 40) {
            if (strstr(line, "mCallState=1") || strstr(line, "mRingingCallState=1")) {
                state = 1;
            } else if (strstr(line, "mCallState=2") || strstr(line, "mForegroundCallState=1")) {
                state = 2;
            }

            char *incoming = strstr(line, "mCallIncomingNumber=");
            if (incoming && number_out && max_len > 0) {
                sscanf(incoming, "mCallIncomingNumber=%s", number_out);
            }
        }
    }
    pclose(fp);
    return state;
}

static void trigger_speaker_answer(void) {
    system("telecom accept-call 2>/dev/null || input keyevent KEYCODE_CALL");
    usleep(350000);
    system("service call audio 33 i32 1 2>/dev/null"); /* setSpeakerphoneOn(true) */
    system("cmd media_session dispatch --key 79 2>/dev/null");
}

static void trigger_earpiece_answer(void) {
    system("telecom accept-call 2>/dev/null || input keyevent KEYCODE_CALL");
}

static void trigger_hangup(void) {
    system("telecom end-call 2>/dev/null || input keyevent KEYCODE_ENDCALL");
}

static void execute_idle_action(const char *action) {
    if (strcmp(action, "torch") == 0) {
        system("am broadcast -a com.cat.parts.TOGGLE_TORCH -n com.cat.parts/.BootReceiver >/dev/null 2>&1");
    } else if (strcmp(action, "outer_toggle") == 0) {
        /* Directly toggle the CatParts 'Outer Display Enabled' preference so button & UI stay 100% synced */
        system("am broadcast -a com.cat.parts.TOGGLE_OUTER -n com.cat.parts/.BootReceiver >/dev/null 2>&1");
    } else if (strcmp(action, "camera") == 0) {
        system("am start -a android.media.action.STILL_IMAGE_CAMERA >/dev/null 2>&1 || input keyevent KEYCODE_CAMERA 2>/dev/null");
    } else if (strcmp(action, "assistant") == 0) {
        system("input keyevent KEYCODE_VOICE_ASSIST 2>/dev/null");
    } else if (strcmp(action, "dnd") == 0) {
        system("input keyevent KEYCODE_VOLUME_MUTE 2>/dev/null");
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    /* Ensure config file exists and is world-writable for CatParts app */
    int cfd = open(CONFIG_FILE, O_CREAT | O_RDWR, 0666);
    if (cfd >= 0) close(cfd);
    chmod(CONFIG_FILE, 0666);
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

    if (num_fds == 0) return 1;

    struct timespec press_time = {0, 0};
    struct timespec last_action_time = {0, 0};
    int is_pressed = 0;
    time_t last_cfg_mtime = 0;

    for (;;) {
        int ret = poll(fds, num_fds, 1000);

        /* Check if CatParts Settings app updated /dev/shm_outerscreen_cmd */
        struct stat st;
        if (stat(CONFIG_FILE, &st) == 0 && st.st_mtime != last_cfg_mtime) {
            last_cfg_mtime = st.st_mtime;
            load_config();
        }

        if (ret <= 0) continue;

        for (int i = 0; i < num_fds; i++) {
            if (fds[i].revents & POLLIN) {
                struct input_event ev;
                while (read(fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type == EV_KEY && (ev.code == PTT_KEY_CODE_1 || ev.code == PTT_KEY_CODE_2)) {
                        if (ev.value == 1 && !is_pressed) { /* Key Down */
                            clock_gettime(CLOCK_MONOTONIC, &press_time);
                            is_pressed = 1;
                        } else if (ev.value == 0 && is_pressed) { /* Key Up */
                            struct timespec release_time;
                            clock_gettime(CLOCK_MONOTONIC, &release_time);
                            long elapsed_ms = (release_time.tv_sec - press_time.tv_sec) * 1000 +
                                              (release_time.tv_nsec - press_time.tv_nsec) / 1000000;
                            long since_last_ms = (release_time.tv_sec - last_action_time.tv_sec) * 1000 +
                                                 (release_time.tv_nsec - last_action_time.tv_nsec) / 1000000;
                            is_pressed = 0;
                            if (since_last_ms < 250) continue; /* Debounce duplicate event1/event7 reports */
                            last_action_time = release_time;

                            load_config();
                            char incoming_num[64] = {0};
                            int call_state = get_call_state(incoming_num, sizeof(incoming_num));

                            if (call_state == 1) {
                                /* Incoming Call */
                                if (elapsed_ms >= SHORT_PRESS_MS) {
                                    if (strcmp(cfg_ptt_incall, "answer_spk") == 0) {
                                        trigger_speaker_answer();
                                    } else if (strcmp(cfg_ptt_incall, "answer_ear") == 0) {
                                        trigger_earpiece_answer();
                                    }
                                }
                            } else if (call_state == 2) {
                                /* Active Call */
                                if (elapsed_ms >= SHORT_PRESS_MS) {
                                    if (strcmp(cfg_ptt_incall, "mute") == 0) {
                                        system("input keyevent KEYCODE_MUTE 2>/dev/null");
                                    } else if (strcmp(cfg_ptt_incall, "none") != 0) {
                                        trigger_hangup();
                                    }
                                }
                            } else {
                                /* Idle */
                                if (elapsed_ms >= LONG_LONG_PRESS_MS) {
                                    execute_idle_action(cfg_ptt_long);
                                } else {
                                    execute_idle_action(cfg_ptt_short);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < num_fds; i++) close(fds[i].fd);
    return 0;
}
