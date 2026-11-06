/*
 * Cat S22 Flip Outer OLED (128x128 fb2) Dynamic Display Daemon
 * 
 * - Full suite of 22 Watchfaces and 14 Call UI HUDs.
 * - Scaled-down high-density UI layout.
 * - Contact Name lookup from content://com.android.contacts.
 * - Always-on 128x128 32-bit ARGB8888 framebuffer rendering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <poll.h>
#include <linux/input.h>

#define FB_DEV          "/dev/graphics/fb2"
#define BACKLIGHT_NODE  "/sys/devices/platform/soc/1a00000.qcom,mdss_mdp/1a00000.qcom,mdss_mdp:qcom,mdss_fb_secondary/leds/lcd-backlight2/brightness"
#define BACKLIGHT_ALT   "/sys/class/leds/lcd-backlight2/brightness"
#define WAKELOCK_PATH   "/sys/power/wake_lock"
#define WAKEUNLOCK_PATH "/sys/power/wake_unlock"
#define WAKELOCK_NAME   "outerscreen_wakelock\n"

#define PTT_KEY_1       523  /* KEY_NUMERIC_B */
#define PTT_KEY_2       525  /* Raw matrix row=4 col=3 */
#define WAKE_TIMEOUT_SEC 10

static void acquire_wakelock(void) {
    int fd = open(WAKELOCK_PATH, O_WRONLY);
    if (fd >= 0) {
        write(fd, WAKELOCK_NAME, sizeof(WAKELOCK_NAME) - 1);
        close(fd);
    }
}

static void release_wakelock(void) {
    int fd = open(WAKEUNLOCK_PATH, O_WRONLY);
    if (fd >= 0) {
        write(fd, WAKELOCK_NAME, sizeof(WAKELOCK_NAME) - 1);
        close(fd);
    }
}

static int open_input_device_by_name(const char *target_name) {
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            char name[128] = {0};
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
                if (strstr(name, target_name) != NULL) {
                    return fd;
                }
            }
            close(fd);
        }
    }
    return -1;
}

/* Returns: 1 = lid open, 0 = lid closed */
static int get_lid_state(int hall_fd) {
    if (hall_fd < 0) return 0; /* Default closed if no sensor */
    unsigned long sw_bit = 0;
    if (ioctl(hall_fd, EVIOCGSW(sizeof(sw_bit)), &sw_bit) >= 0) {
        /* SW_LID bit 0: 0 = lid open, 1 = lid closed */
        return (sw_bit & 1) ? 0 : 1;
    }
    return 0;
}

static void set_backlight(int val) {
    int fd = open(BACKLIGHT_NODE, O_WRONLY);
    if (fd < 0) fd = open(BACKLIGHT_ALT, O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d\n", val);
        write(fd, buf, strlen(buf));
        close(fd);
    }
}

/* Colors (0xFF Alpha channel prevents display inversion) */
#define COLOR_BLACK         0xFF000000
#define COLOR_WHITE         0xFFFFFFFF
#define COLOR_PIP_GREEN     0xFF10B981
#define COLOR_PIP_LIGHT     0xFFA7F3D0
#define COLOR_PIP_DARK      0xFF064E3B
#define COLOR_AMBER         0xFFF59E0B
#define COLOR_AMBER_BRIGHT  0xFFFBBF24
#define COLOR_AMBER_DARK    0xFF78350F
#define COLOR_RED           0xFFEF4444
#define COLOR_RED_DARK      0xFF991B1B
#define COLOR_GRAY          0xFF334155
#define COLOR_GRAY_LIGHT    0xFF94A3B8
#define COLOR_BLUE          0xFF3B82F6
#define COLOR_CYAN          0xFF06B6D4
#define COLOR_MAGENTA       0xFFEC4899
#define COLOR_PURPLE        0xFFA855F7
#define COLOR_YELLOW        0xFFEAB308
#define COLOR_GB_BG         0xFF8BAC0F
#define COLOR_GB_FG         0xFF0F380F
#define COLOR_GB_MID        0xFF306230

/* 5x7 ASCII font table */
static const uint8_t font5x7[][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x00, 0x00, 0x5F, 0x00, 0x00},
    ['"'] = {0x00, 0x07, 0x00, 0x07, 0x00},
    ['#'] = {0x14, 0x7F, 0x14, 0x7F, 0x14},
    ['$'] = {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    ['%'] = {0x23, 0x13, 0x08, 0x64, 0x62},
    ['&'] = {0x36, 0x49, 0x55, 0x22, 0x50},
    ['\''] = {0x00, 0x05, 0x03, 0x00, 0x00},
    ['('] = {0x00, 0x1C, 0x22, 0x41, 0x00},
    [')'] = {0x00, 0x41, 0x22, 0x1C, 0x00},
    ['*'] = {0x14, 0x08, 0x3E, 0x08, 0x14},
    ['+'] = {0x08, 0x08, 0x3E, 0x08, 0x08},
    [','] = {0x00, 0x50, 0x30, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['/'] = {0x20, 0x10, 0x08, 0x04, 0x02},
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00},
    [';'] = {0x00, 0x56, 0x36, 0x00, 0x00},
    ['<'] = {0x08, 0x14, 0x22, 0x41, 0x00},
    ['='] = {0x14, 0x14, 0x14, 0x14, 0x14},
    ['>'] = {0x00, 0x41, 0x22, 0x14, 0x08},
    ['?'] = {0x02, 0x01, 0x51, 0x09, 0x06},
    ['@'] = {0x32, 0x49, 0x79, 0x41, 0x3E},
    ['A'] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q'] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W'] = {0x7F, 0x20, 0x18, 0x20, 0x7F},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43},
    ['['] = {0x00, 0x7F, 0x41, 0x41, 0x00},
    ['\\'] = {0x02, 0x04, 0x08, 0x10, 0x20},
    [']'] = {0x00, 0x41, 0x41, 0x7F, 0x00},
    ['^'] = {0x04, 0x02, 0x01, 0x02, 0x04},
    ['_'] = {0x40, 0x40, 0x40, 0x40, 0x40},
};

static void draw_pixel(uint32_t *fb, int x, int y, uint32_t color) {
    if (x >= 0 && x < 128 && y >= 0 && y < 128) {
        fb[y * 128 + x] = color;
    }
}

static void fill_rect(uint32_t *fb, int x, int y, int w, int h, uint32_t color) {
    for (int cy = y; cy < y + h && cy < 128; cy++) {
        if (cy < 0) continue;
        for (int cx = x; cx < x + w && cx < 128; cx++) {
            if (cx >= 0) fb[cy * 128 + cx] = color;
        }
    }
}

static void draw_rect(uint32_t *fb, int x, int y, int w, int h, uint32_t color) {
    for (int cx = x; cx < x + w; cx++) { draw_pixel(fb, cx, y, color); draw_pixel(fb, cx, y + h - 1, color); }
    for (int cy = y; cy < y + h; cy++) { draw_pixel(fb, x, cy, color); draw_pixel(fb, x + w - 1, cy, color); }
}

static void draw_telemetry_bar(uint32_t *fb, int x, int y, int w, int h, int pct, uint32_t frame_color, uint32_t fill_color) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    draw_rect(fb, x, y, w, h, frame_color);
    if (w > 2 && h > 2) {
        int fill_w = (pct * (w - 2)) / 100;
        if (fill_w > 0) {
            fill_rect(fb, x + 1, y + 1, fill_w, h - 2, fill_color);
        }
    }
}

static void draw_char(uint32_t *fb, int x, int y, char c, uint32_t color, int scale) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 32 || c > 95) c = ' ';
    const uint8_t *glyph = font5x7[(int)c];
    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                if (scale == 1) {
                    draw_pixel(fb, x + col, y + row, color);
                } else {
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            draw_pixel(fb, x + col * scale + dx, y + row * scale + dy, color);
                        }
                    }
                }
            }
        }
    }
}

static void draw_string(uint32_t *fb, int x, int y, const char *str, uint32_t color, int scale) {
    int cur_x = x;
    while (*str) {
        draw_char(fb, cur_x, y, *str, color, scale);
        cur_x += 6 * scale;
        str++;
    }
}

static void draw_string_centered(uint32_t *fb, int y, const char *str, uint32_t color, int scale) {
    int len = strlen(str);
    int w = len * 6 * scale - scale;
    int x = (128 - w) / 2;
    if (x < 0) x = 0;
    draw_string(fb, x, y, str, color, scale);
}

static void clear_screen(uint32_t *fb) {
    for (int i = 0; i < 128 * 128; i++) fb[i] = COLOR_BLACK;
}

static int get_battery(void) {
    FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (!f) return -1;
    int cap = -1;
    fscanf(f, "%d", &cap);
    fclose(f);
    return cap;
}

static int get_cpu_usage(void) {
    static unsigned long prev_user = 0, prev_nice = 0, prev_sys = 0, prev_idle = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    unsigned long user, nice, sys, idle;
    char cpu[8];
    fscanf(f, "%s %lu %lu %lu %lu", cpu, &user, &nice, &sys, &idle);
    fclose(f);
    unsigned long diff_user = user - prev_user;
    unsigned long diff_nice = nice - prev_nice;
    unsigned long diff_sys = sys - prev_sys;
    unsigned long diff_idle = idle - prev_idle;
    prev_user = user; prev_nice = nice; prev_sys = sys; prev_idle = idle;
    unsigned long total = diff_user + diff_nice + diff_sys + diff_idle;
    if (total == 0) return 0;
    return (int)(((diff_user + diff_nice + diff_sys) * 100) / total);
}

static int get_ram_usage(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    unsigned long total = 0, free = 0, buffers = 0, cached = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line + 9, "%lu", &total);
        else if (strncmp(line, "MemFree:", 8) == 0) sscanf(line + 8, "%lu", &free);
        else if (strncmp(line, "Buffers:", 8) == 0) sscanf(line + 8, "%lu", &buffers);
        else if (strncmp(line, "Cached:", 7) == 0) sscanf(line + 7, "%lu", &cached);
    }
    fclose(f);
    if (total == 0) return 0;
    unsigned long used = total - (free + buffers + cached);
    return (int)((used * 100) / total);
}

/* Contact Lookup */
static void lookup_contact_name(const char *number, char *out_name, size_t max_len) {
    if (!number || !number[0]) {
        snprintf(out_name, max_len, "UNKNOWN");
        return;
    }
    out_name[0] = '\0';
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "content query --uri content://com.android.contacts/phone_lookup/%s --projection display_name 2>/dev/null", number);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            char *p = strstr(line, "display_name=");
            if (p) {
                p += 13;
                char *comma = strchr(p, ',');
                if (comma) *comma = '\0';
                char *nl = strpbrk(p, "\r\n");
                if (nl) *nl = '\0';
                snprintf(out_name, max_len, "%s", p);
            }
        }
        pclose(fp);
    }
    if (!out_name[0]) {
        snprintf(out_name, max_len, "%s", number);
    }
}

/* Telephony poll */
static int poll_telephony(char *caller_num, size_t max_len) {
    FILE *fp = popen("dumpsys telephony.registry 2>/dev/null", "r");
    if (!fp) return 0;

    char line[256];
    int call_state = 0; // 0 = IDLE, 1 = RINGING, 2 = OFFHOOK
    int lines_read = 0;
    if (caller_num && max_len > 0) caller_num[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        lines_read++;
        if (lines_read <= 45) {
            if (strstr(line, "mCallState=1") || strstr(line, "mRingingCallState=1")) {
                call_state = 1;
            } else if (strstr(line, "mCallState=2") || strstr(line, "mForegroundCallState=1")) {
                call_state = 2;
            }

            char *incoming = strstr(line, "mCallIncomingNumber=");
            if (incoming && caller_num && max_len > 0 && !caller_num[0]) {
                sscanf(incoming, "mCallIncomingNumber=%s", caller_num);
            }
        }
    }
    pclose(fp);
    return call_state;
}

/* Preferences from /dev/shm_outerscreen_cmd */
static int  cfg_outer_enabled    = 1;
static int  cfg_outer_brightness = 255;
static char cfg_outer_theme[32]  = "pipboy";
static char cfg_outer_call[32]   = "pipboy";
static char cfg_outer_footer[32] = "battery";

static void load_outer_config(void) {
    FILE *fp = fopen("/dev/shm_outerscreen_cmd", "r");
    if (!fp) return;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (strncmp(line, "outer_enabled=", 14) == 0) {
            cfg_outer_enabled = atoi(line + 14);
        } else if (strncmp(line, "outer_brightness=", 17) == 0) {
            int br = atoi(line + 17);
            if (br >= 10 && br <= 255) cfg_outer_brightness = br;
        } else if (strncmp(line, "outer_theme=", 12) == 0) {
            snprintf(cfg_outer_theme, sizeof(cfg_outer_theme), "%s", line + 12);
        } else if (strncmp(line, "outer_call_theme=", 17) == 0) {
            snprintf(cfg_outer_call, sizeof(cfg_outer_call), "%s", line + 17);
        } else if (strncmp(line, "outer_footer=", 13) == 0) {
            snprintf(cfg_outer_footer, sizeof(cfg_outer_footer), "%s", line + 13);
        }
    }
    fclose(fp);
}

/* === RENDERING ENGINES === */

/* Common compact header */
static void draw_status_bar(uint32_t *fb, const char *title, uint32_t color, int bat) {
    draw_string(fb, 2, 2, "4G", color, 1);
    draw_string_centered(fb, 2, title, color, 1);
    char bat_buf[8];
    snprintf(bat_buf, sizeof(bat_buf), "%d%%", bat >= 0 ? bat : 0);
    draw_string(fb, 126 - (int)strlen(bat_buf) * 6, 2, bat_buf, color, 1);
    fill_rect(fb, 2, 11, 124, 1, color);
}

/* 22 WATCHFACES (Compact Scaled-Down UI) */
static void render_watchface(uint32_t *fb, const char *theme, struct tm *tm, int bat, int cpu, int ram) {
    char time_str[16];
    char date_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M", tm);
    strftime(date_str, sizeof(date_str), "%a, %b %d", tm);
    for (int i = 0; date_str[i]; i++) if (date_str[i] >= 'a' && date_str[i] <= 'z') date_str[i] -= 32;

    if (strcmp(theme, "gshock") == 0) {
        fill_rect(fb, 0, 0, 128, 128, COLOR_BLACK);
        draw_rect(fb, 1, 1, 126, 126, COLOR_GRAY);
        draw_status_bar(fb, "G-SHOCK", COLOR_WHITE, bat);
        draw_string_centered(fb, 24, date_str, COLOR_GRAY_LIGHT, 1);
        draw_string_centered(fb, 38, time_str, COLOR_WHITE, 3);
        char sec_buf[8]; snprintf(sec_buf, sizeof(sec_buf), ":%02d", tm->tm_sec);
        draw_string(fb, 100, 48, sec_buf, COLOR_YELLOW, 1);
        draw_string_centered(fb, 74, "WR 20BAR - TOUGH", COLOR_YELLOW, 1);
        fill_rect(fb, 14, 94, (bat * 100) / 100, 6, COLOR_RED);
        draw_rect(fb, 14, 94, 100, 6, COLOR_GRAY_LIGHT);
        draw_string_centered(fb, 110, "CAT S22 FLIP", COLOR_GRAY_LIGHT, 1);

    } else if (strcmp(theme, "doom") == 0) {
        draw_status_bar(fb, "DOOM SLAYER", COLOR_RED, bat);
        draw_string_centered(fb, 22, time_str, COLOR_RED, 3);
        draw_string_centered(fb, 52, "E1M1: HANGAR", COLOR_AMBER, 1);
        char stats[32]; snprintf(stats, sizeof(stats), "HLT:%3d%%  ARM:%3d%%", bat, ram);
        draw_string_centered(fb, 70, stats, COLOR_WHITE, 1);
        char ammo[32]; snprintf(ammo, sizeof(ammo), "AMMO: %02d / BFG", cpu);
        draw_string_centered(fb, 86, ammo, COLOR_YELLOW, 1);
        fill_rect(fb, 10, 106, (bat * 108) / 100, 6, COLOR_RED);
        draw_rect(fb, 10, 106, 108, 6, COLOR_RED_DARK);

    } else if (strcmp(theme, "lcars") == 0) {
        fill_rect(fb, 0, 0, 16, 128, COLOR_AMBER);
        fill_rect(fb, 16, 0, 112, 12, COLOR_AMBER_BRIGHT);
        draw_string(fb, 20, 2, "STARFLEET LCARS", COLOR_BLACK, 1);
        draw_string(fb, 22, 26, time_str, COLOR_WHITE, 3);
        draw_string(fb, 22, 56, date_str, COLOR_AMBER, 1);
        char stardate[32]; snprintf(stardate, sizeof(stardate), "SD: 47634.%d", tm->tm_yday);
        draw_string(fb, 22, 74, stardate, COLOR_PURPLE, 1);
        char warp[32]; snprintf(warp, sizeof(warp), "WARP CORE: %d%%", bat);
        draw_string(fb, 22, 92, warp, COLOR_CYAN, 1);
        draw_string(fb, 22, 110, "SYS: NOMINAL", COLOR_AMBER, 1);

    } else if (strcmp(theme, "amber") == 0) {
        draw_status_bar(fb, "AMBER TELEMETRY", COLOR_AMBER, bat);
        draw_string_centered(fb, 26, time_str, COLOR_AMBER_BRIGHT, 3);
        draw_string_centered(fb, 56, date_str, COLOR_AMBER, 1);
        char telem[32]; snprintf(telem, sizeof(telem), "CPU:%d%% RAM:%d%%", cpu, ram);
        draw_string_centered(fb, 74, telem, COLOR_AMBER, 1);
        fill_rect(fb, 14, 94, (bat * 100) / 100, 4, COLOR_AMBER);
        draw_rect(fb, 14, 94, 100, 4, COLOR_AMBER_DARK);
        draw_string_centered(fb, 110, "POWER 88% NOMINAL", COLOR_AMBER, 1);

    } else if (strcmp(theme, "matrix") == 0) {
        draw_string(fb, 2, 2, "101001", COLOR_PIP_DARK, 1);
        draw_string(fb, 80, 2, "01101", COLOR_PIP_DARK, 1);
        draw_string_centered(fb, 24, time_str, COLOR_PIP_GREEN, 3);
        draw_string_centered(fb, 54, date_str, COLOR_PIP_LIGHT, 1);
        draw_string_centered(fb, 72, "WAKE UP, NEO...", COLOR_PIP_GREEN, 1);
        draw_string_centered(fb, 90, "MATRIX HAS YOU", COLOR_PIP_DARK, 1);
        char bat_m[32]; snprintf(bat_m, sizeof(bat_m), "NODE BAT: %d%%", bat);
        draw_string_centered(fb, 108, bat_m, COLOR_PIP_GREEN, 1);

    } else if (strcmp(theme, "cat") == 0) {
        fill_rect(fb, 0, 0, 128, 12, COLOR_YELLOW);
        draw_string(fb, 4, 2, "CAT S22 HEAVY DUTY", COLOR_BLACK, 1);
        draw_string_centered(fb, 26, time_str, COLOR_WHITE, 3);
        draw_string_centered(fb, 56, date_str, COLOR_YELLOW, 1);
        fill_rect(fb, 4, 76, 120, 12, COLOR_YELLOW);
        draw_string_centered(fb, 78, "BUILT FOR IT", COLOR_BLACK, 1);
        char diesel[32]; snprintf(diesel, sizeof(diesel), "POWER CELL: %d%%", bat);
        draw_string_centered(fb, 98, diesel, COLOR_WHITE, 1);
        draw_string_centered(fb, 112, "IP68 RUGGED FLIP", COLOR_GRAY_LIGHT, 1);

    } else if (strcmp(theme, "radar") == 0) {
        draw_status_bar(fb, "TACTICAL RADAR", COLOR_CYAN, bat);
        draw_rect(fb, 40, 20, 48, 48, COLOR_PIP_DARK);
        draw_string_centered(fb, 36, time_str, COLOR_WHITE, 2);
        draw_string_centered(fb, 74, "HDG 084 SWEEP", COLOR_CYAN, 1);
        char coords[32]; snprintf(coords, sizeof(coords), "GRID 48N-11E %d%%", bat);
        draw_string_centered(fb, 92, coords, COLOR_PIP_LIGHT, 1);
        draw_string_centered(fb, 110, "TARGET ACQUIRED", COLOR_PIP_GREEN, 1);

    } else if (strcmp(theme, "synthwave") == 0) {
        draw_status_bar(fb, "SYNTHWAVE 84", COLOR_MAGENTA, bat);
        draw_string_centered(fb, 26, time_str, COLOR_MAGENTA, 3);
        draw_string_centered(fb, 56, date_str, COLOR_CYAN, 1);
        fill_rect(fb, 0, 78, 128, 1, COLOR_PURPLE);
        draw_string_centered(fb, 88, "OUTRUN HIGHWAY", COLOR_MAGENTA, 1);
        char neon[32]; snprintf(neon, sizeof(neon), "NEON POWER: %d%%", bat);
        draw_string_centered(fb, 108, neon, COLOR_CYAN, 1);

    } else if (strcmp(theme, "lcd") == 0) {
        fill_rect(fb, 0, 0, 128, 128, COLOR_GRAY_LIGHT);
        draw_string(fb, 4, 4, "CASIO", COLOR_BLACK, 1);
        draw_string(fb, 70, 4, date_str, COLOR_BLACK, 1);
        draw_string_centered(fb, 30, time_str, COLOR_BLACK, 3);
        char f91[32]; snprintf(f91, sizeof(f91), "ALARM CHRONO %d%%", bat);
        draw_string_centered(fb, 64, f91, COLOR_BLACK, 1);
        draw_string_centered(fb, 86, "WATER RESIST", COLOR_BLACK, 1);
        draw_string_centered(fb, 108, "LITHIUM", COLOR_BLACK, 1);

    } else if (strcmp(theme, "nothing") == 0) {
        draw_status_bar(fb, "(1)", COLOR_WHITE, bat);
        draw_string_centered(fb, 28, time_str, COLOR_WHITE, 3);
        draw_string_centered(fb, 60, date_str, COLOR_GRAY_LIGHT, 1);
        draw_string_centered(fb, 80, "NOTHING (R)", COLOR_WHITE, 1);
        char gly[32]; snprintf(gly, sizeof(gly), "GLYPH: %d%%", bat);
        draw_string_centered(fb, 104, gly, COLOR_GRAY_LIGHT, 1);

    } else if (strcmp(theme, "mono") == 0) {
        /* Stark OLED Mono */
        draw_string_centered(fb, 30, time_str, COLOR_WHITE, 3);
        draw_string_centered(fb, 64, date_str, COLOR_GRAY_LIGHT, 1);
        char bat_mono[32]; snprintf(bat_mono, sizeof(bat_mono), "%d%% BATTERY", bat);
        draw_string_centered(fb, 88, bat_mono, COLOR_WHITE, 1);

    } else if (strcmp(theme, "vegas") == 0) {
        /* Pip-Boy Amber New Vegas */
        draw_status_bar(fb, "MOJAVE WASTELAND", COLOR_AMBER, bat);
        draw_string_centered(fb, 24, time_str, COLOR_AMBER_BRIGHT, 3);
        draw_string_centered(fb, 54, "NEW VEGAS // OCT 31", COLOR_AMBER, 1);
        char veg[32]; snprintf(veg, sizeof(veg), "HP:%d%%  AP:95", bat);
        draw_string_centered(fb, 72, veg, COLOR_AMBER_BRIGHT, 1);
        draw_string_centered(fb, 90, "RAD: 0  CAPS:1420", COLOR_AMBER, 1);
        draw_string_centered(fb, 108, "> PIP-BOY 3000-A", COLOR_AMBER_DARK, 1);

    } else if (strcmp(theme, "agent007") == 0) {
        /* GoldenEye 007 */
        fill_rect(fb, 2, 20, 10, (bat * 80) / 100, COLOR_BLUE);
        draw_rect(fb, 2, 20, 10, 80, COLOR_WHITE);
        fill_rect(fb, 16, 20, 10, (ram * 80) / 100, COLOR_RED);
        draw_rect(fb, 16, 20, 10, 80, COLOR_WHITE);
        draw_string(fb, 32, 10, "MI6 COVERT HUD", COLOR_BLUE, 1);
        draw_string(fb, 32, 34, time_str, COLOR_WHITE, 2);
        draw_string(fb, 32, 58, date_str, COLOR_CYAN, 1);
        draw_string(fb, 32, 78, "MISSION STATUS", COLOR_YELLOW, 1);
        draw_string(fb, 32, 92, "OBJECTIVE: 100%", COLOR_PIP_GREEN, 1);
        draw_string(fb, 32, 110, "LASER WATCH OK", COLOR_WHITE, 1);

    } else if (strcmp(theme, "gameboy") == 0) {
        /* Nintendo DMG-01 */
        fill_rect(fb, 0, 0, 128, 128, COLOR_GB_BG);
        draw_string(fb, 4, 4, "DOT MATRIX", COLOR_GB_FG, 1);
        char gb_bat[16]; snprintf(gb_bat, sizeof(gb_bat), "BAT:%d%%", bat);
        draw_string(fb, 76, 4, gb_bat, COLOR_GB_FG, 1);
        draw_string_centered(fb, 30, time_str, COLOR_GB_FG, 3);
        draw_string_centered(fb, 64, date_str, COLOR_GB_MID, 1);
        draw_string_centered(fb, 86, "HIGH SCORE 88400", COLOR_GB_FG, 1);
        draw_string_centered(fb, 106, "GAME BOY (TM)", COLOR_GB_MID, 1);

    } else if (strcmp(theme, "arasaka") == 0) {
        /* Night City Cyberware */
        draw_status_bar(fb, "ARASAKA CYBER", COLOR_RED, bat);
        draw_string_centered(fb, 26, time_str, COLOR_WHITE, 3);
        draw_string_centered(fb, 56, "SANDEVISTAN: OK", COLOR_RED, 1);
        char neural[32]; snprintf(neural, sizeof(neural), "NEURAL SYNC: %d%%", bat);
        draw_string_centered(fb, 76, neural, COLOR_AMBER, 1);
        fill_rect(fb, 10, 94, 108, 12, COLOR_RED_DARK);
        draw_string_centered(fb, 96, "SUB-DERMAL LVL 4", COLOR_WHITE, 1);
        draw_string_centered(fb, 112, "SEC: CLASSIFIED", COLOR_RED, 1);

    } else if (strcmp(theme, "terminal") == 0) {
        /* Linux Terminal */
        draw_string(fb, 2, 2, "root@cat-s22:~#", COLOR_BLUE, 1);
        draw_string(fb, 2, 16, time_str, COLOR_WHITE, 2);
        draw_string(fb, 2, 36, date_str, COLOR_PIP_GREEN, 1);
        char up[32]; snprintf(up, sizeof(up), "bat: %d%% temp: 31C", bat);
        draw_string(fb, 2, 54, up, COLOR_PIP_LIGHT, 1);
        char load[32]; snprintf(load, sizeof(load), "cpu: %d%% ram: %d%%", cpu, ram);
        draw_string(fb, 2, 72, load, COLOR_AMBER, 1);
        draw_string(fb, 2, 90, "kernel: 4.9.227-cat", COLOR_GRAY_LIGHT, 1);
        draw_string(fb, 2, 108, "bash$ _", COLOR_PIP_GREEN, 1);

    } else if (strcmp(theme, "cockpit") == 0) {
        /* Glass Cockpit */
        draw_status_bar(fb, "PFD HORIZON", COLOR_CYAN, bat);
        fill_rect(fb, 0, 50, 128, 1, COLOR_CYAN);
        fill_rect(fb, 54, 50, 20, 2, COLOR_YELLOW);
        draw_string_centered(fb, 22, time_str, COLOR_WHITE, 2);
        draw_string(fb, 4, 60, "ALT 320", COLOR_PIP_GREEN, 1);
        draw_string(fb, 76, 60, "HDG 084", COLOR_PIP_GREEN, 1);
        draw_string_centered(fb, 80, "MACH 0.82 // FL340", COLOR_YELLOW, 1);
        draw_string_centered(fb, 100, "AP: ENGAGED GPS-1", COLOR_CYAN, 1);

    } else if (strcmp(theme, "diver") == 0) {
        /* Sub Aqua Diver */
        draw_status_bar(fb, "DIVER 300M", COLOR_CYAN, bat);
        draw_string_centered(fb, 24, time_str, COLOR_WHITE, 3);
        draw_string_centered(fb, 56, "DEPTH: 28M  NDL:42M", COLOR_CYAN, 1);
        char tank[32]; snprintf(tank, sizeof(tank), "O2 TANK: %d BAR", (bat * 2));
        draw_string_centered(fb, 76, tank, COLOR_PIP_LIGHT, 1);
        draw_string_centered(fb, 94, "DIVE TIME: 34:10", COLOR_YELLOW, 1);
        draw_string_centered(fb, 110, "SAFETY STOP OK", COLOR_PIP_GREEN, 1);

    } else if (strcmp(theme, "cybersynth") == 0) {
        /* Outrun Cyber Synth */
        draw_status_bar(fb, "OUTRUN SYNTH", COLOR_MAGENTA, bat);
        draw_string_centered(fb, 26, time_str, COLOR_MAGENTA, 3);
        draw_string_centered(fb, 56, date_str, COLOR_PURPLE, 1);
        draw_string_centered(fb, 76, "NEON GRID // 88%", COLOR_CYAN, 1);
        draw_string_centered(fb, 94, "CRUISING 120 MPH", COLOR_MAGENTA, 1);
        draw_string_centered(fb, 110, "RETRO WAVE OK", COLOR_PURPLE, 1);

    } else if (strcmp(theme, "steampunk") == 0) {
        /* Steampunk Brass */
        draw_status_bar(fb, "CHRONOMETER", COLOR_AMBER, bat);
        draw_string_centered(fb, 24, time_str, COLOR_WHITE, 2);
        draw_string_centered(fb, 46, "X : XLV", COLOR_AMBER_BRIGHT, 2);
        draw_string_centered(fb, 70, date_str, COLOR_AMBER, 1);
        char psi[32]; snprintf(psi, sizeof(psi), "BOILER: %d PSI", 100 + bat);
        draw_string_centered(fb, 88, psi, COLOR_AMBER_BRIGHT, 1);
        draw_string_centered(fb, 108, "STEAM REGULATOR", COLOR_AMBER_DARK, 1);

    } else if (strcmp(theme, "solar") == 0) {
        /* Solar Horizon */
        draw_string_centered(fb, 20, time_str, COLOR_WHITE, 3);
        draw_string_centered(fb, 52, date_str, COLOR_AMBER, 1);
        draw_string_centered(fb, 72, "SUNRISE: 06:14", COLOR_YELLOW, 1);
        draw_string_centered(fb, 90, "SUNSET : 18:24", COLOR_AMBER_DARK, 1);
        char sol_b[32]; snprintf(sol_b, sizeof(sol_b), "SOLAR BAT: %d%%", bat);
        draw_string_centered(fb, 108, sol_b, COLOR_WHITE, 1);

    } else {
        /* Pip-Boy 3000 (Default) */
        draw_status_bar(fb, "Cat S22 FLIP", COLOR_PIP_GREEN, bat);
        draw_string(fb, 10, 22, time_str, COLOR_WHITE, 3);
        char sec_buf[8]; snprintf(sec_buf, sizeof(sec_buf), ":%02d", tm->tm_sec);
        draw_string(fb, 102, 34, sec_buf, COLOR_PIP_GREEN, 1);
        draw_string_centered(fb, 48, date_str, COLOR_PIP_LIGHT, 1);
        fill_rect(fb, 2, 58, 124, 1, COLOR_PIP_DARK);

        /* Telemetry */
        char telem_b[16];
        snprintf(telem_b, sizeof(telem_b), "BAT:%3d%%", bat >= 0 ? bat : 0);
        draw_string(fb, 4, 64, telem_b, COLOR_PIP_GREEN, 1);
        draw_telemetry_bar(fb, 58, 64, 66, 7, bat >= 0 ? bat : 0, COLOR_PIP_DARK, COLOR_PIP_GREEN);

        char telem_c[16];
        snprintf(telem_c, sizeof(telem_c), "CPU:%3d%%", cpu);
        draw_string(fb, 4, 76, telem_c, COLOR_PIP_LIGHT, 1);
        draw_telemetry_bar(fb, 58, 76, 66, 7, cpu, COLOR_PIP_DARK, COLOR_PIP_LIGHT);

        char telem_r[16];
        snprintf(telem_r, sizeof(telem_r), "RAM:%3d%%", ram);
        draw_string(fb, 4, 88, telem_r, COLOR_PIP_LIGHT, 1);
        draw_telemetry_bar(fb, 58, 88, 66, 7, ram, COLOR_PIP_DARK, COLOR_PIP_LIGHT);

        fill_rect(fb, 2, 100, 124, 1, COLOR_PIP_DARK);
        if (strcmp(cfg_outer_footer, "battery") == 0) {
            char est[32];
            int hrs = (bat * 14) / 100;
            snprintf(est, sizeof(est), "EST: %dh LEFT (PWR OK)", hrs > 0 ? hrs : 1);
            draw_string(fb, 4, 106, est, COLOR_PIP_GREEN, 1);
            draw_string(fb, 4, 116, "VAULT-TEC CAT-S22", COLOR_PIP_LIGHT, 1);
        } else {
            draw_string(fb, 4, 106, "> SYS:ONLINE LTE:OK", COLOR_PIP_GREEN, 1);
            draw_string(fb, 4, 116, "> SECURE DUAL-SCREEN", COLOR_PIP_LIGHT, 1);
        }
    }
}

/* 14 CALL UI HUDS (Scaled-Down with Contact Name & Phone Number) */
static void render_call_hud(uint32_t *fb, const char *theme, int call_state, const char *num, const char *name, long duration) {
    char dur_str[16];
    snprintf(dur_str, sizeof(dur_str), "%02ld:%02ld", duration / 60, duration % 60);

    /* Clean display name up to 14 chars */
    char disp_name[16];
    snprintf(disp_name, sizeof(disp_name), "%.14s", (name && name[0]) ? name : ((num && num[0]) ? num : "UNKNOWN"));

    char disp_num[16];
    snprintf(disp_num, sizeof(disp_num), "%.14s", (num && num[0]) ? num : "");

    if (strcmp(theme, "red_alert") == 0) {
        fill_rect(fb, 0, 0, 128, 16, COLOR_RED);
        draw_string_centered(fb, 4, call_state == 1 ? "RED ALERT CALL" : "ACTIVE COMMS", COLOR_WHITE, 1);
        draw_string_centered(fb, 24, disp_name, COLOR_WHITE, 2);
        if (disp_num[0]) draw_string_centered(fb, 44, disp_num, COLOR_AMBER, 1);
        if (call_state == 2) draw_string_centered(fb, 60, dur_str, COLOR_WHITE, 2);
        fill_rect(fb, 6, 88, 116, 16, COLOR_RED);
        draw_string_centered(fb, 92, call_state == 1 ? "PTT: ANSWER" : "PTT: HANG UP", COLOR_WHITE, 1);
        draw_string_centered(fb, 112, "RED KEY: REJECT", COLOR_RED, 1);

    } else if (strcmp(theme, "cyberpunk") == 0 || strcmp(theme, "holo") == 0) {
        fill_rect(fb, 0, 0, 128, 16, COLOR_CYAN);
        draw_string_centered(fb, 4, "NEURAL HOLO-LINK", COLOR_BLACK, 1);
        draw_string_centered(fb, 24, disp_name, COLOR_MAGENTA, 2);
        if (disp_num[0]) draw_string_centered(fb, 44, disp_num, COLOR_CYAN, 1);
        if (call_state == 2) draw_string_centered(fb, 58, dur_str, COLOR_WHITE, 2);
        draw_string_centered(fb, 76, "HD AUDIO VOLTE", COLOR_PIP_LIGHT, 1);
        fill_rect(fb, 6, 92, 116, 16, COLOR_MAGENTA);
        draw_string_centered(fb, 96, "CONNECT COMM", COLOR_WHITE, 1);
        draw_string_centered(fb, 114, "NIGHT CITY LINK", COLOR_GRAY_LIGHT, 1);

    } else if (strcmp(theme, "cat_radio") == 0 || strcmp(theme, "walkie") == 0) {
        fill_rect(fb, 0, 0, 128, 16, COLOR_YELLOW);
        draw_string_centered(fb, 4, "CAT PTT DIRECT CH04", COLOR_BLACK, 1);
        draw_string_centered(fb, 26, disp_name, COLOR_WHITE, 2);
        if (disp_num[0]) draw_string_centered(fb, 46, disp_num, COLOR_YELLOW, 1);
        if (call_state == 2) draw_string_centered(fb, 60, dur_str, COLOR_WHITE, 2);
        draw_string_centered(fb, 78, "SQ: -98dBm  TX:OK", COLOR_YELLOW, 1);
        fill_rect(fb, 6, 94, 116, 16, COLOR_YELLOW);
        draw_string_centered(fb, 98, "HOLD PTT: TRANSMIT", COLOR_BLACK, 1);
        draw_string_centered(fb, 114, "HEAVY DUTY AUDIO", COLOR_GRAY_LIGHT, 1);

    } else if (strcmp(theme, "cassette") == 0) {
        fill_rect(fb, 0, 0, 128, 16, COLOR_MAGENTA);
        draw_string_centered(fb, 4, "AUDIO RECORDER", COLOR_BLACK, 1);
        draw_string_centered(fb, 26, disp_name, COLOR_WHITE, 2);
        if (disp_num[0]) draw_string_centered(fb, 46, disp_num, COLOR_CYAN, 1);
        if (call_state == 2) draw_string_centered(fb, 62, dur_str, COLOR_YELLOW, 2);
        draw_string_centered(fb, 82, "TAPE: SIDE A", COLOR_CYAN, 1);
        fill_rect(fb, 6, 96, 116, 16, COLOR_CYAN);
        draw_string_centered(fb, 100, "PTT: PLAY / REC", COLOR_BLACK, 1);

    } else if (strcmp(theme, "target") == 0 || strcmp(theme, "encrypted") == 0) {
        fill_rect(fb, 0, 0, 128, 16, COLOR_PIP_GREEN);
        draw_string_centered(fb, 4, "SECURE COMMS [AES-256]", COLOR_BLACK, 1);
        draw_string_centered(fb, 24, disp_name, COLOR_PIP_LIGHT, 2);
        if (disp_num[0]) draw_string_centered(fb, 44, disp_num, COLOR_PIP_GREEN, 1);
        if (call_state == 2) draw_string_centered(fb, 58, dur_str, COLOR_WHITE, 2);
        draw_string_centered(fb, 76, "FREQ: 144.825 MHz", COLOR_PIP_GREEN, 1);
        fill_rect(fb, 6, 92, 116, 16, COLOR_PIP_DARK);
        draw_rect(fb, 6, 92, 116, 16, COLOR_PIP_GREEN);
        draw_string_centered(fb, 96, "ENCRYPTED LINK", COLOR_PIP_GREEN, 1);
        draw_string_centered(fb, 114, "MIL-SPEC SECURE", COLOR_GRAY_LIGHT, 1);

    } else if (strcmp(theme, "starfleet") == 0) {
        fill_rect(fb, 0, 0, 16, 128, COLOR_AMBER);
        fill_rect(fb, 16, 0, 112, 14, COLOR_AMBER_BRIGHT);
        draw_string(fb, 20, 3, "SUBSPACE AUDIO", COLOR_BLACK, 1);
        draw_string(fb, 22, 24, disp_name, COLOR_WHITE, 2);
        if (disp_num[0]) draw_string(fb, 22, 44, disp_num, COLOR_AMBER, 1);
        if (call_state == 2) draw_string(fb, 22, 60, dur_str, COLOR_YELLOW, 2);
        draw_string(fb, 22, 82, "HAILING FREQ", COLOR_PURPLE, 1);
        draw_string(fb, 22, 98, "AUDIO CHANNEL ON", COLOR_CYAN, 1);
        draw_string(fb, 22, 114, "LCARS STARFLEET", COLOR_AMBER, 1);

    } else if (strcmp(theme, "pager") == 0) {
        fill_rect(fb, 0, 0, 128, 128, COLOR_GRAY_LIGHT);
        draw_string(fb, 4, 4, "PAGE 01 [URGENT]", COLOR_BLACK, 1);
        draw_string_centered(fb, 26, disp_name, COLOR_BLACK, 2);
        if (disp_num[0]) draw_string_centered(fb, 46, disp_num, COLOR_BLACK, 1);
        if (call_state == 2) draw_string_centered(fb, 62, dur_str, COLOR_BLACK, 2);
        draw_string_centered(fb, 84, "MSG: CALL STAT", COLOR_BLACK, 1);
        draw_string_centered(fb, 106, "MOTOROLA 90S", COLOR_BLACK, 1);

    } else if (strcmp(theme, "mi6") == 0) {
        fill_rect(fb, 0, 0, 128, 16, COLOR_BLUE);
        draw_string_centered(fb, 4, "TRANSMISSION FROM M", COLOR_WHITE, 1);
        draw_string_centered(fb, 26, disp_name, COLOR_WHITE, 2);
        if (disp_num[0]) draw_string_centered(fb, 46, disp_num, COLOR_CYAN, 1);
        if (call_state == 2) draw_string_centered(fb, 62, dur_str, COLOR_WHITE, 2);
        draw_string_centered(fb, 82, "PRIORITY 1 DISPATCH", COLOR_YELLOW, 1);
        fill_rect(fb, 6, 96, 116, 16, COLOR_BLUE);
        draw_string_centered(fb, 100, "DECODE SATELLITE", COLOR_WHITE, 1);

    } else if (strcmp(theme, "minimal") == 0) {
        draw_string_centered(fb, 6, call_state == 1 ? "INCOMING CALL" : "ACTIVE CALL", COLOR_GRAY_LIGHT, 1);
        draw_string_centered(fb, 28, disp_name, COLOR_WHITE, 2);
        if (disp_num[0]) draw_string_centered(fb, 48, disp_num, COLOR_GRAY_LIGHT, 1);
        if (call_state == 2) draw_string_centered(fb, 68, dur_str, COLOR_WHITE, 2);
        draw_string_centered(fb, 94, "PTT: ANSWER / HANGUP", COLOR_WHITE, 1);
        draw_string_centered(fb, 112, "RED KEY: REJECT", COLOR_GRAY_LIGHT, 1);

    } else {
        /* Pip-Boy Incoming & Active HUD (Default) */
        if (call_state == 1) {
            fill_rect(fb, 0, 0, 128, 14, COLOR_AMBER);
            draw_string_centered(fb, 3, "> TRANSMISSION <", COLOR_BLACK, 1);
            draw_string_centered(fb, 20, "INCOMING CALL", COLOR_PIP_GREEN, 1);
            draw_string_centered(fb, 34, disp_name, COLOR_WHITE, 2);
            if (disp_num[0]) draw_string_centered(fb, 52, disp_num, COLOR_PIP_LIGHT, 1);
            draw_string_centered(fb, 68, "SIG: [||||||||||] 98%", COLOR_PIP_LIGHT, 1);
            fill_rect(fb, 6, 84, 116, 16, COLOR_PIP_GREEN);
            draw_string_centered(fb, 88, "HOLD [PTT]: ANSWER SPK", COLOR_BLACK, 1);
            draw_string_centered(fb, 108, "RED KEY: REJECT", COLOR_RED, 1);
        } else {
            fill_rect(fb, 0, 0, 128, 14, COLOR_PIP_GREEN);
            draw_string(fb, 4, 3, "COMM ACTIVE", COLOR_BLACK, 1);
            draw_string(fb, 78, 3, "SPK:ON", COLOR_BLACK, 1);
            draw_string_centered(fb, 20, disp_name, COLOR_WHITE, 2);
            if (disp_num[0]) draw_string_centered(fb, 38, disp_num, COLOR_PIP_LIGHT, 1);
            draw_string_centered(fb, 52, dur_str, COLOR_WHITE, 3);
            draw_rect(fb, 6, 96, 116, 16, COLOR_RED);
            draw_string_centered(fb, 101, "HOLD [PTT]: END CALL", COLOR_RED, 1);
        }
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    int fb_fd = open(FB_DEV, O_RDWR);
    if (fb_fd < 0) return 1;

    struct fb_var_screeninfo vinfo;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        close(fb_fd);
        return 1;
    }

    size_t screensize = 128 * 128 * 4;
    uint32_t *fb = (uint32_t *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb == MAP_FAILED) {
        close(fb_fd);
        return 1;
    }

    /* Open input devices for event monitoring */
    int hall_fd = open_input_device_by_name("hall_switch");
    int ptt_fd  = open_input_device_by_name("matrix_keypad");
    if (ptt_fd < 0) ptt_fd = open_input_device_by_name("gpio_keys");

    int call_state = 0;
    char incoming_num[64] = {0};
    char contact_name[64] = {0};
    char last_resolved_num[64] = {0};
    time_t call_start_time = 0;
    unsigned long loop_count = 0;

    int last_lid_open = -1;
    time_t wake_expire_time = time(NULL) + WAKE_TIMEOUT_SEC; /* Wake on boot */
    int display_active = 1;

    for (;;) {
        loop_count++;
        load_outer_config();

        /* Always refresh watchdog heartbeat so watchdog does not restart daemon while idle */
        int hbfd = open("/dev/shm_outerscreen_heartbeat", O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (hbfd >= 0) close(hbfd);

        /* Re-open input fds if disconnected */
        if (hall_fd < 0) hall_fd = open_input_device_by_name("hall_switch");
        if (ptt_fd < 0) {
            ptt_fd = open_input_device_by_name("matrix_keypad");
            if (ptt_fd < 0) ptt_fd = open_input_device_by_name("gpio_keys");
        }

        /* 1. Poll input devices (Hall sensor + PTT) non-blocking */
        struct pollfd pfds[2];
        int num_pfds = 0;
        if (hall_fd >= 0) { pfds[num_pfds].fd = hall_fd; pfds[num_pfds].events = POLLIN; num_pfds++; }
        if (ptt_fd >= 0)  { pfds[num_pfds].fd = ptt_fd;  pfds[num_pfds].events = POLLIN; num_pfds++; }

        if (num_pfds > 0) {
            int ret = poll(pfds, num_pfds, 0);
            if (ret > 0) {
                for (int i = 0; i < num_pfds; i++) {
                    if (pfds[i].revents & POLLIN) {
                        struct input_event ev;
                        while (read(pfds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                            /* Hall sensor LID event */
                            if (ev.type == EV_SW && ev.code == 0) {
                                int lid_open = (ev.value == 0) ? 1 : 0;
                                if (last_lid_open != lid_open) {
                                    if (lid_open == 0) {
                                        /* Flip just closed -> Turn ON outer screen for 10s */
                                        wake_expire_time = time(NULL) + WAKE_TIMEOUT_SEC;
                                    }
                                    last_lid_open = lid_open;
                                }
                            }
                            /* PTT key event */
                            else if (ev.type == EV_KEY && (ev.code == PTT_KEY_1 || ev.code == PTT_KEY_2)) {
                                if (ev.value == 1) {
                                    /* PTT pressed -> Wake outer screen for 10s */
                                    wake_expire_time = time(NULL) + WAKE_TIMEOUT_SEC;
                                }
                            }
                        }
                    }
                }
            }
        }

        /* If hall sensor node exists, verify current switch state */
        if (hall_fd >= 0 && last_lid_open == -1) {
            last_lid_open = get_lid_state(hall_fd);
            if (last_lid_open == 0) {
                wake_expire_time = time(NULL) + WAKE_TIMEOUT_SEC;
            }
        }

        /* 2. Check Telephony (Call state keeps screen active) */
        if (call_state != 0 || loop_count % 2 == 1) {
            call_state = poll_telephony(incoming_num, sizeof(incoming_num));
        }

        /* 3. Determine if outer display should be active */
        time_t now = time(NULL);
        int should_display = 0;

        if (!cfg_outer_enabled) {
            should_display = 0;
        } else if (call_state > 0) {
            /* Always display during incoming or active call */
            should_display = 1;
            wake_expire_time = now + WAKE_TIMEOUT_SEC;
        } else if (last_lid_open == 1) {
            /* Flip is OPEN -> Outer display MUST be OFF */
            should_display = 0;
        } else {
            /* Flip is CLOSED -> Display active if within 10 seconds of close or PTT press */
            if (now < wake_expire_time) {
                should_display = 1;
            } else {
                should_display = 0;
            }
        }

        /* 4. Display state transitions */
        if (!should_display) {
            if (display_active) {
                /* Transition to OFF: blank FB, turn off backlight, release wakelock */
                set_backlight(0);
                clear_screen(fb);
                vinfo.yoffset = 0;
                ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo);
                ioctl(fb_fd, FBIOBLANK, FB_BLANK_POWERDOWN);
                release_wakelock();
                display_active = 0;
            }
            sleep(1);
            continue;
        }

        /* Transition to ON / Active rendering */
        if (!display_active) {
            acquire_wakelock();
            ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
            set_backlight(cfg_outer_brightness);
            display_active = 1;
        }

        set_backlight(cfg_outer_brightness);
        clear_screen(fb);

        /* Render UI */
        if (call_state > 0) {
            if (incoming_num[0] && strcmp(incoming_num, last_resolved_num) != 0) {
                lookup_contact_name(incoming_num, contact_name, sizeof(contact_name));
                snprintf(last_resolved_num, sizeof(last_resolved_num), "%s", incoming_num);
            }
            if (call_state == 2) {
                if (call_start_time == 0) call_start_time = time(NULL);
            } else {
                call_start_time = 0;
            }
            long dur = call_start_time > 0 ? (long)(time(NULL) - call_start_time) : 0;
            render_call_hud(fb, cfg_outer_call, call_state, incoming_num, contact_name, dur);
        } else {
            call_start_time = 0;
            last_resolved_num[0] = '\0';
            contact_name[0] = '\0';
            struct tm *tm_info = localtime(&now);
            int bat = get_battery();
            int cpu = get_cpu_usage();
            int ram = get_ram_usage();
            render_watchface(fb, cfg_outer_theme, tm_info, bat, cpu, ram);
        }

        vinfo.yoffset = 0;
        ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo);
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);

        sleep(1);
    }

    if (hall_fd >= 0) close(hall_fd);
    if (ptt_fd >= 0) close(ptt_fd);
    close(fb_fd);
    return 0;
}
