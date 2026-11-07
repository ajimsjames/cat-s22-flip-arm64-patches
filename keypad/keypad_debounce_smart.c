/*
 * Cat S22 Flip Keypad Daemon with Smooth Continuous Virtual Mouse Mode
 * and Smart Dialer / Home Screen Physical Key Navigation (ARM64)
 *
 * 1. Debounces raw matrix keypad events (120ms anti-chatter).
 * 2. Smart Dialer & Home Screen Navigation (when Mouse Mode is OFF):
 *    - In Dialer (com.android.dialer) when dialpad is closed & search unfocused:
 *        * Number keys 0-9: Auto-opens the dialpad (taps Keypad FAB via uinput
 *          touchscreen) and immediately types the pressed digit and subsequent digits!
 *        * D-Pad LEFT / RIGHT: Cycles between Speed Dial, Call History, and Contacts
 *          bottom navigation tabs!
 *        * D-Pad UP / DOWN: Normal vertical list scrolling in Call History / Contacts.
 *        * Short-press '#' or Short-press Speaker: Focuses top "Search contacts" bar!
 *        * Long-press Speaker (>= 500ms): Opens top-right 3-dot "More options" menu!
 *    - In Dialer when dialpad is open:
 *        * Green Call key (KEY_SEND / 231 or KEY_PHONE / 169): Immediately taps
 *          the green Dial floating action button to place the call!
 *    - On Home Screen (com.android.launcher3) when App Drawer is closed:
 *        * Number keys 0-9: Immediately launches Dialer pre-filled with that digit!
 * 3. Long-press '*' (KEY_NUMERIC_STAR / 522 >= 2.0 seconds):
 *    - Toggles Virtual Mouse Mode ON / OFF with haptic pulse.
 *    - In Mouse Mode:
 *        * D-PAD UP, DOWN, LEFT, RIGHT:
 *          - Continuous smooth fluid movement (60 Hz poll ticker).
 *          - Dynamic acceleration (starts precise, accelerates smoothly while held).
 *        * D-PAD CENTER (ENTER / 28): Left Click / Tap at cursor position.
 * 4. User Configurable Mouse Speed:
 *    - Real-time speed setting loaded from /dev/shm_outerscreen_cmd (configured via CatParts app).
 * 5. Normal Mode (all other apps):
 *    - D-PAD works as normal directional navigation.
 *    - Speaker button: Short press -> TAB, Long press -> MENU.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

static long long mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void _sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

#define INPUT_DEV       "/dev/input/event1"
#define HALL_DEV        "/dev/input/event3"
#define UINPUT_DEV      "/dev/uinput"
#define CONFIG_FILE     "/dev/shm_outerscreen_cmd"
#define WAKE_FILE       "/dev/shm_outerscreen_wake"

#define DEBOUNCE_NS     120000000LL        /* 120 ms anti-chatter */
#define STAR_LONG_NS    2000000000LL       /* Full 2.0 seconds long-press for '*' */
#define SPK_LONG_NS     500000000LL        /* 500 ms for Speaker button */
#define MAX_KEYS        600

#define KEY_SPEAKER_RAW 248  /* KEY_MICMUTE */
#define KEY_CUSTOM_TAB  15   /* KEY_TAB */
#define KEY_CUSTOM_MENU 139  /* KEY_MENU */

#define KEY_STAR_RAW    522  /* KEY_NUMERIC_STAR */
#define KEY_POUND_RAW   523  /* KEY_NUMERIC_POUND */
#define KEY_ENTER_RAW   28   /* KEY_ENTER */
#define KEY_UP_RAW      103  /* KEY_UP */
#define KEY_DOWN_RAW    108  /* KEY_DOWN */
#define KEY_LEFT_RAW    105  /* KEY_LEFT */
#define KEY_RIGHT_RAW   106  /* KEY_RIGHT */
#define KEY_BACK_RAW    158  /* KEY_BACK */
#define KEY_CALL_RAW1   169  /* KEY_PHONE (0x00a9 in matrix_keys) */
#define KEY_CALL_RAW2   231  /* KEY_SEND */

#define FG_OTHER        0
#define FG_DIALER       1
#define FG_LAUNCHER     2

static const int matrix_keys[] = {
    0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009,
    0x000a, 0x000b, 0x000e, 0x001c, 0x0067, 0x0069, 0x006a, 0x006c,
    0x009e, 0x00a9, 0x00ac, 0x00f8, 0x020a, 0x020b, 0x020c, 0x020d,
    0x0244,
    15,     /* KEY_TAB */
    139     /* KEY_MENU */
};
#define NUM_KEYS (sizeof(matrix_keys)/sizeof(matrix_keys[0]))

/* Shared UI state updated by background monitor thread & optimistic local actions */
static volatile int g_fg_app         = FG_OTHER;
static volatile int g_dialpad_open   = 0;
static volatile int g_search_focused = 0;
static volatile int g_dialer_tab     = 1; /* 0=Speed dial, 1=Call log, 2=Contacts */
static volatile int g_apps_drawer    = 0;
static volatile long long g_optimistic_until_ns = 0;

static int keycode_to_digit_char(int code) {
    if (code >= KEY_1 && code <= KEY_9) return '1' + (code - KEY_1);
    if (code == KEY_0) return '0';
    return 0;
}

#define REQUIRED_DPI 210

static void enforce_hardware_display_dpi(void) {
    /* Verify and enforce required 210 DPI so physical keypad touch tap coordinates remain 100% aligned */
    FILE *fp = popen("wm density 2>/dev/null", "r");
    if (fp) {
        char buf[256];
        int current_dpi = 0;
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "Override density:")) {
                current_dpi = atoi(buf + 17);
            } else if (current_dpi == 0 && strstr(buf, "Physical density:")) {
                current_dpi = atoi(buf + 17);
            }
        }
        pclose(fp);

        if (current_dpi != REQUIRED_DPI) {
            system("wm density 210 >/dev/null 2>&1");
            system("settings put secure display_density_forced 210 >/dev/null 2>&1");
        }
    }
}

static void *ui_state_monitor_thread(void *arg) {
    (void)arg;
    char line[1024];
    int check_count = 0;

    /* Enforce required DPI on startup */
    enforce_hardware_display_dpi();

    for (;;) {
        _sleep_ms(300);

        /* Periodically re-check DPI (every ~15 seconds) to self-heal if someone changed it */
        if (++check_count >= 50) {
            check_count = 0;
            enforce_hardware_display_dpi();
        }

        /* Respect optimistic local state transitions for 750ms after a smart action */
        if (mono_ns() < g_optimistic_until_ns) {
            continue;
        }

        FILE *fp = popen("dumpsys activity top 2>/dev/null", "r");
        if (!fp) {
            _sleep_ms(500);
            continue;
        }

        int cur_act = FG_OTHER;
        int dialpad_vis = 0;
        int search_foc = 0;
        int tab_idx = g_dialer_tab;
        int apps_vis = 0;

        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strstr(line, "ACTIVITY ")) {
                if (strstr(line, "com.android.dialer")) {
                    cur_act = FG_DIALER;
                    dialpad_vis = 0;
                    search_foc = 0;
                } else if (strstr(line, "com.android.launcher3")) {
                    cur_act = FG_LAUNCHER;
                    apps_vis = 0;
                } else {
                    cur_act = FG_OTHER;
                }
            } else if (cur_act == FG_DIALER) {
                if (strstr(line, "DialpadSlidingRelativeLayout{")) {
                    if (strstr(line, " V.E")) dialpad_vis = 1;
                    else if (strstr(line, " G.E")) dialpad_vis = 0;
                } else if (strstr(line, "app:id/search_view")) {
                    if (strstr(line, " .F")) search_foc = 1;
                } else if (strstr(line, "BottomNavItem{")) {
                    if (strstr(line, " ..S")) {
                        if (strstr(line, "speed_dial_tab")) tab_idx = 0;
                        else if (strstr(line, "call_log_tab")) tab_idx = 1;
                        else if (strstr(line, "contacts_tab")) tab_idx = 2;
                    }
                }
            } else if (cur_act == FG_LAUNCHER) {
                if (strstr(line, "app:id/apps_view")) {
                    if (strstr(line, " V.E")) apps_vis = 1;
                    else if (strstr(line, " G.E")) apps_vis = 0;
                }
            }
        }
        pclose(fp);

        if (mono_ns() >= g_optimistic_until_ns) {
            g_fg_app         = cur_act;
            g_dialpad_open   = dialpad_vis;
            g_search_focused = search_foc;
            g_dialer_tab     = tab_idx;
            g_apps_drawer    = apps_vis;
        }
    }
    return NULL;
}

static void emit_key(int ui, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = val;
    write(ui, &ev, sizeof(ev));

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(ui, &ev, sizeof(ev));
}

static void emit_rel(int ui, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_REL;
    ev.code = code;
    ev.value = val;
    write(ui, &ev, sizeof(ev));

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(ui, &ev, sizeof(ev));
}

static void emit_rel_xy(int ui, int dx, int dy) {
    struct input_event ev;
    if (dx != 0) {
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_REL;
        ev.code = REL_X;
        ev.value = dx;
        write(ui, &ev, sizeof(ev));
    }
    if (dy != 0) {
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_REL;
        ev.code = REL_Y;
        ev.value = dy;
        write(ui, &ev, sizeof(ev));
    }
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(ui, &ev, sizeof(ev));
}

/* Emit an instant <10ms touchscreen tap via the virtual uinput touchscreen */
static void emit_tap(int ui_touch, int x, int y) {
    struct input_event ev[8];
    memset(ev, 0, sizeof(ev));

    ev[0].type = EV_ABS; ev[0].code = ABS_MT_SLOT;        ev[0].value = 0;
    ev[1].type = EV_ABS; ev[1].code = ABS_MT_TRACKING_ID; ev[1].value = 1;
    ev[2].type = EV_ABS; ev[2].code = ABS_MT_POSITION_X;  ev[2].value = x;
    ev[3].type = EV_ABS; ev[3].code = ABS_MT_POSITION_Y;  ev[3].value = y;
    ev[4].type = EV_KEY; ev[4].code = BTN_TOUCH;          ev[4].value = 1;
    ev[5].type = EV_SYN; ev[5].code = SYN_REPORT;         ev[5].value = 0;
    write(ui_touch, ev, 6 * sizeof(struct input_event));

    _sleep_ms(12);

    memset(ev, 0, sizeof(ev));
    ev[0].type = EV_ABS; ev[0].code = ABS_MT_TRACKING_ID; ev[0].value = -1;
    ev[1].type = EV_KEY; ev[1].code = BTN_TOUCH;          ev[1].value = 0;
    ev[2].type = EV_SYN; ev[2].code = SYN_REPORT;         ev[2].value = 0;
    write(ui_touch, ev, 3 * sizeof(struct input_event));
}

static void haptic_pulse(int ms) {
    int fd_dur = open("/sys/class/leds/vibrator/duration", O_WRONLY);
    int fd_act = open("/sys/class/leds/vibrator/activate", O_WRONLY);
    if (fd_dur >= 0 && fd_act >= 0) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%d\n", ms);
        write(fd_dur, buf, len);
        write(fd_act, "1\n", 2);
    }
    if (fd_dur >= 0) close(fd_dur);
    if (fd_act >= 0) close(fd_act);
}

/* Parse mouse speed preference from config file */
static int read_configured_mouse_speed(int default_speed) {
    int fd = open(CONFIG_FILE, O_RDONLY);
    if (fd < 0) return default_speed;
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return default_speed;
    buf[n] = '\0';
    for (int i = 0; i <= n - 12; i++) {
        if (strncmp(&buf[i], "mouse_speed=", 12) == 0) {
            int val = atoi(&buf[i + 12]);
            if (val >= 4 && val <= 80) return val;
        }
    }
    return default_speed;
}

/* Returns: 1 = lid open, 0 = lid closed */
static int get_lid_open(int hall_fd) {
    if (hall_fd >= 0) {
        unsigned long sw_bit = 0;
        if (ioctl(hall_fd, EVIOCGSW(sizeof(sw_bit)), &sw_bit) >= 0) {
            /* SW_LID bit 0: 0 = lid open, 1 = lid closed */
            return (sw_bit & 1) ? 0 : 1;
        }
    }
    return 1;
}

static int create_virtual_touchscreen(void) {
    int ut = open(UINPUT_DEV, O_WRONLY | O_NONBLOCK);
    if (ut < 0) return -1;

    ioctl(ut, UI_SET_EVBIT, EV_KEY);
    ioctl(ut, UI_SET_EVBIT, EV_ABS);
    ioctl(ut, UI_SET_EVBIT, EV_SYN);
    ioctl(ut, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(ut, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    ioctl(ut, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(ut, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(ut, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(ut, UI_SET_ABSBIT, ABS_MT_POSITION_Y);

    struct uinput_user_dev utd;
    memset(&utd, 0, sizeof(utd));
    snprintf(utd.name, UINPUT_MAX_NAME_SIZE, "Cat_Virtual_Touch");
    utd.id.bustype = 0x0003;
    utd.id.vendor  = 0x18d1;
    utd.id.product = 0x0002;
    utd.id.version = 0x0001;

    utd.absmin[ABS_MT_SLOT]        = 0;
    utd.absmax[ABS_MT_SLOT]        = 9;
    utd.absmin[ABS_MT_TRACKING_ID] = 0;
    utd.absmax[ABS_MT_TRACKING_ID] = 65535;
    utd.absmin[ABS_MT_POSITION_X]  = 0;
    utd.absmax[ABS_MT_POSITION_X]  = 479;
    utd.absmin[ABS_MT_POSITION_Y]  = 0;
    utd.absmax[ABS_MT_POSITION_Y]  = 639;

    write(ut, &utd, sizeof(utd));
    ioctl(ut, UI_DEV_CREATE, 0);
    return ut;
}

int main(void) {
    int src = open(INPUT_DEV, O_RDONLY | O_NONBLOCK);
    if (src < 0) return 1;

    ioctl(src, EVIOCGRAB, 1);

    int hall_fd = open(HALL_DEV, O_RDONLY | O_NONBLOCK);

    int ui = open(UINPUT_DEV, O_WRONLY | O_NONBLOCK);
    if (ui < 0) return 2;

    ioctl(ui, UI_SET_EVBIT, EV_KEY);
    ioctl(ui, UI_SET_EVBIT, EV_REL);
    ioctl(ui, UI_SET_EVBIT, EV_SYN);
    ioctl(ui, UI_SET_EVBIT, EV_MSC);
    ioctl(ui, UI_SET_MSCBIT, MSC_SCAN);

    ioctl(ui, UI_SET_RELBIT, REL_X);
    ioctl(ui, UI_SET_RELBIT, REL_Y);
    ioctl(ui, UI_SET_RELBIT, REL_WHEEL);

    ioctl(ui, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(ui, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(ui, UI_SET_KEYBIT, BTN_MIDDLE);

    for (unsigned int i = 0; i < NUM_KEYS; i++) {
        ioctl(ui, UI_SET_KEYBIT, matrix_keys[i]);
    }

    struct uinput_user_dev uid;
    memset(&uid, 0, sizeof(uid));
    snprintf(uid.name, UINPUT_MAX_NAME_SIZE, "Cat_Keypad_Clean");
    uid.id.bustype = 0x0003;
    uid.id.vendor  = 0x18d1;
    uid.id.product = 0x0001;
    uid.id.version = 0x0001;

    write(ui, &uid, sizeof(uid));
    ioctl(ui, UI_DEV_CREATE, 0);

    int ui_touch = create_virtual_touchscreen();

    pthread_t mon_tid;
    pthread_create(&mon_tid, NULL, ui_state_monitor_thread, NULL);
    pthread_detach(mon_tid);

    long long last_down[MAX_KEYS];
    long long last_up[MAX_KEYS];
    int       last_val[MAX_KEYS];
    int       suppressed_up[MAX_KEYS];
    memset(last_down,     0, sizeof(last_down));
    memset(last_up,       0, sizeof(last_up));
    memset(suppressed_up, 0, sizeof(suppressed_up));
    for (int i = 0; i < MAX_KEYS; i++) last_val[i] = -1;

    long long spk_down_time = 0;
    int spk_is_down = 0;

    long long star_down_time = 0;
    int star_is_down = 0;

    int mouse_mode = 0; /* 0 = Normal, 1 = Mouse Cursor Mode */
    int current_mouse_speed = 18; /* Default base speed */

    long long enter_down_time = 0;
    int enter_is_down = 0;

    /* Active directional hold state for smooth 60Hz continuous movement */
    int dpad_up_held = 0;
    int dpad_down_held = 0;
    int dpad_left_held = 0;
    int dpad_right_held = 0;
    long long dpad_held_start = 0;

    long long last_cfg_check = 0;

    /* Tab X coordinates for Speed dial (0), Call history (1), Contacts (2) */
    static const int tab_x_coords[3] = { 80, 240, 400 };
    static const int tab_y_coord     = 583;

    struct pollfd pfd;
    pfd.fd = src;
    pfd.events = POLLIN;

    for (;;) {
        int any_held = mouse_mode && (dpad_up_held || dpad_down_held || dpad_left_held || dpad_right_held);
        int timeout_ms = any_held ? 16 : 1000;

        pfd.revents = 0;
        poll(&pfd, 1, timeout_ms);

        long long now = mono_ns();

        if (now - last_cfg_check > 2000000000LL) {
            current_mouse_speed = read_configured_mouse_speed(18);
            last_cfg_check = now;
        }

        if (pfd.revents & POLLIN) {
            struct input_event ev;
            while (read(src, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type != EV_KEY) {
                    write(ui, &ev, sizeof(ev));
                    continue;
                }

                int code = ev.code;
                int val  = ev.value;

                if (code < 0 || code >= MAX_KEYS) {
                    write(ui, &ev, sizeof(ev));
                    continue;
                }

                if (val == 1) {
                    if (last_val[code] == 1 && (now - last_down[code]) < DEBOUNCE_NS) continue;
                    if (last_val[code] == 0 && (now - last_up[code])   < DEBOUNCE_NS) continue;
                    last_down[code] = now;
                    last_val[code]  = 1;
                } else if (val == 0) {
                    if (last_val[code] == 0 && (now - last_up[code]) < DEBOUNCE_NS) continue;
                    if (last_val[code] != 1) continue;
                    last_up[code]  = now;
                    last_val[code] = 0;

                    if (suppressed_up[code]) {
                        suppressed_up[code] = 0;
                        continue;
                    }
                }

                /* When flip is CLOSED (SW_LID == 1):
                 * - If PTT (523 / 525) is pressed, signal outerscreen_service via WAKE_FILE
                 * - Suppress forwarding any matrix_keypad keys to Cat_Keypad_Clean so Android
                 *   InputReader never wakes the main display while the flip is closed!
                 */
                if (hall_fd < 0) hall_fd = open(HALL_DEV, O_RDONLY | O_NONBLOCK);
                if (!get_lid_open(hall_fd)) {
                    if (val == 1 && (code == 523 || code == 525)) {
                        int wfd = open(WAKE_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0666);
                        if (wfd >= 0) {
                            write(wfd, "1\n", 2);
                            close(wfd);
                        }
                    }
                    continue;
                }

                /* 1. Long-press '*' (>= 2.0 seconds) to toggle Virtual Mouse */
                if (code == KEY_STAR_RAW) {
                    if (val == 1) {
                        star_down_time = now;
                        star_is_down = 1;
                    } else if (val == 0) {
                        if (star_is_down) {
                            star_is_down = 0;
                            long long duration = now - star_down_time;
                            if (duration >= STAR_LONG_NS) {
                                mouse_mode = !mouse_mode;
                                dpad_up_held = 0;
                                dpad_down_held = 0;
                                dpad_left_held = 0;
                                dpad_right_held = 0;
                                if (mouse_mode) {
                                    haptic_pulse(60);
                                    current_mouse_speed = read_configured_mouse_speed(18);
                                    emit_rel(ui, REL_X, 0);
                                    emit_rel(ui, REL_Y, 0);
                                } else {
                                    haptic_pulse(25);
                                    _sleep_ms(80);
                                    haptic_pulse(25);
                                }
                            } else {
                                emit_key(ui, KEY_STAR_RAW, 1);
                                emit_key(ui, KEY_STAR_RAW, 0);
                            }
                        }
                    }
                    continue;
                }

                /* 2. Speaker Button (Dialer-aware: Short -> Search Contacts, Long -> 3-dot Menu; Otherwise TAB / MENU) */
                if (code == KEY_SPEAKER_RAW) {
                    if (val == 1) {
                        spk_down_time = now;
                        spk_is_down = 1;
                    } else if (val == 0) {
                        if (spk_is_down) {
                            spk_is_down = 0;
                            long long duration = now - spk_down_time;
                            if (!mouse_mode && g_fg_app == FG_DIALER && !g_dialpad_open && ui_touch >= 0) {
                                if (duration >= SPK_LONG_NS) {
                                    /* Long-press Speaker in Dialer -> Tap 3-dot More Options (437, 79) */
                                    emit_tap(ui_touch, 437, 79);
                                } else {
                                    /* Short-press Speaker in Dialer -> Tap Search Contacts bar (220, 79) */
                                    g_search_focused = 1;
                                    g_optimistic_until_ns = mono_ns() + 750000000LL;
                                    emit_tap(ui_touch, 220, 79);
                                }
                            } else {
                                if (duration >= SPK_LONG_NS) {
                                    emit_key(ui, KEY_CUSTOM_MENU, 1);
                                    emit_key(ui, KEY_CUSTOM_MENU, 0);
                                } else {
                                    emit_key(ui, KEY_CUSTOM_TAB, 1);
                                    emit_key(ui, KEY_CUSTOM_TAB, 0);
                                }
                            }
                        }
                    }
                    continue;
                }

                /* 3. Virtual Mouse Controls (Active ONLY when mouse_mode == 1) */
                if (mouse_mode) {
                    if (code == KEY_UP_RAW) {
                        dpad_up_held = (val == 1);
                        if (val == 1) dpad_held_start = now;
                        continue;
                    } else if (code == KEY_DOWN_RAW) {
                        dpad_down_held = (val == 1);
                        if (val == 1) dpad_held_start = now;
                        continue;
                    } else if (code == KEY_LEFT_RAW) {
                        dpad_left_held = (val == 1);
                        if (val == 1) dpad_held_start = now;
                        continue;
                    } else if (code == KEY_RIGHT_RAW) {
                        dpad_right_held = (val == 1);
                        if (val == 1) dpad_held_start = now;
                        continue;
                    } else if (code == KEY_ENTER_RAW) {
                        if (val == 1) {
                            enter_down_time = now;
                            enter_is_down = 1;
                        } else if (val == 0) {
                            if (enter_is_down) {
                                enter_is_down = 0;
                                long long duration = now - enter_down_time;
                                if (duration >= 500000000LL) {
                                    haptic_pulse(40);
                                    emit_key(ui, BTN_RIGHT, 1);
                                    emit_key(ui, BTN_RIGHT, 0);
                                } else {
                                    emit_key(ui, BTN_LEFT, 1);
                                    emit_key(ui, BTN_LEFT, 0);
                                }
                            }
                        }
                        continue;
                    }
                }

                /* 4. Smart Dialer & Home Screen Key Remapping (when mouse_mode == 0) */
                if (!mouse_mode && ui_touch >= 0) {
                    int digit_ch = keycode_to_digit_char(code);

                    /* Track BACK key closing dialpad or search bar */
                    if (code == KEY_BACK_RAW && val == 1) {
                        if (g_fg_app == FG_DIALER) {
                            if (g_dialpad_open) {
                                g_dialpad_open = 0;
                                g_optimistic_until_ns = now + 750000000LL;
                            } else if (g_search_focused) {
                                g_search_focused = 0;
                                g_optimistic_until_ns = now + 750000000LL;
                            }
                        }
                    }

                    /* Case A: Inside Dialer (com.android.dialer) */
                    if (g_fg_app == FG_DIALER) {
                        if (!g_dialpad_open && !g_search_focused) {
                            /* A1. Number keys 0-9 -> Auto-open Dialpad FAB & type digit */
                            if (digit_ch != 0 && val == 1) {
                                suppressed_up[code] = 1;
                                g_dialpad_open = 1;
                                g_optimistic_until_ns = mono_ns() + 900000000LL;
                                emit_tap(ui_touch, 422, 468); /* Tap Keypad FAB */
                                _sleep_ms(55);                /* Wait for dialpad digits EditText focus */
                                emit_key(ui, code, 1);
                                emit_key(ui, code, 0);
                                continue;
                            }

                            /* A2. D-Pad Left / Right -> Switch Bottom Tabs (Speed dial <-> Call history <-> Contacts) */
                            if ((code == KEY_LEFT_RAW || code == KEY_RIGHT_RAW) && val == 1) {
                                suppressed_up[code] = 1;
                                int next_tab = g_dialer_tab;
                                if (code == KEY_LEFT_RAW) {
                                    next_tab = (next_tab + 2) % 3;
                                } else {
                                    next_tab = (next_tab + 1) % 3;
                                }
                                g_dialer_tab = next_tab;
                                g_optimistic_until_ns = mono_ns() + 750000000LL;
                                emit_tap(ui_touch, tab_x_coords[next_tab], tab_y_coord);
                                continue;
                            }

                            /* A3. '#' Key -> Focus top "Search contacts" bar */
                            if (code == KEY_POUND_RAW && val == 1) {
                                suppressed_up[code] = 1;
                                g_search_focused = 1;
                                g_optimistic_until_ns = mono_ns() + 750000000LL;
                                emit_tap(ui_touch, 220, 79);
                                continue;
                            }

                            /* A4. Green Call Key when dialpad is closed -> Open Dialpad */
                            if ((code == KEY_CALL_RAW1 || code == KEY_CALL_RAW2) && val == 1) {
                                suppressed_up[code] = 1;
                                g_dialpad_open = 1;
                                g_optimistic_until_ns = mono_ns() + 750000000LL;
                                emit_tap(ui_touch, 422, 468);
                                continue;
                            }
                        } else if (g_dialpad_open) {
                            /* A5. Green Call Key when dialpad is OPEN -> Tap green Dial button (240, 582) */
                            if ((code == KEY_CALL_RAW1 || code == KEY_CALL_RAW2) && val == 1) {
                                suppressed_up[code] = 1;
                                g_dialpad_open = 0;
                                g_optimistic_until_ns = mono_ns() + 1000000000LL;
                                emit_tap(ui_touch, 240, 582);
                                continue;
                            }
                        }
                    }
                    /* Case B: On Home Screen (com.android.launcher3) with App Drawer closed */
                    else if (g_fg_app == FG_LAUNCHER && !g_apps_drawer) {
                        if (digit_ch != 0 && val == 1) {
                            suppressed_up[code] = 1;
                            g_fg_app = FG_DIALER;
                            g_dialpad_open = 1;
                            g_optimistic_until_ns = mono_ns() + 1200000000LL;
                            char cmd[128];
                            snprintf(cmd, sizeof(cmd),
                                     "am start -a android.intent.action.DIAL -d tel:%c >/dev/null 2>&1 &",
                                     digit_ch);
                            system(cmd);
                            continue;
                        }
                    }
                }

                /* 5. Normal Mode: pass through */
                write(ui, &ev, sizeof(ev));
            }
        }

        /* 6. Continuous Smooth Mouse Movement Loop */
        if (mouse_mode && (dpad_up_held || dpad_down_held || dpad_left_held || dpad_right_held)) {
            long long hold_time = now - dpad_held_start;
            int step = current_mouse_speed / 3;
            if (step < 4) step = 4;

            if (hold_time > 800000000LL) {
                step = (current_mouse_speed * 16) / 10;
            } else if (hold_time > 150000000LL) {
                step = current_mouse_speed;
            }

            int dx = 0;
            int dy = 0;
            if (dpad_left_held)  dx -= step;
            if (dpad_right_held) dx += step;
            if (dpad_up_held)    dy -= step;
            if (dpad_down_held)  dy += step;

            emit_rel_xy(ui, dx, dy);
        }
    }

    if (ui_touch >= 0) ioctl(ui_touch, UI_DEV_DESTROY, 0);
    ioctl(ui, UI_DEV_DESTROY, 0);
    return 0;
}
