/*
 * Cat S22 Flip Outer Display Boot Logger
 * Streams raw kernel (/dev/kmsg) and early logcat to outer 128x128 OLED (/dev/graphics/fb2)
 * Runs from early boot until sys.boot_completed=1
 * - Scale-1 font (5x7px): fits 21 chars wide, 14 rows tall (starting at y=14 after header)
 * - Circular scrolling buffer: always shows the LATEST lines, scrolls up as new lines arrive
 * - Strips syslog/kmsg priority prefixes before display
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <poll.h>

#define FB_DEV          "/dev/graphics/fb2"
#define BACKLIGHT_NODE  "/sys/devices/platform/soc/1a00000.qcom,mdss_mdp/1a00000.qcom,mdss_mdp:qcom,mdss_fb_secondary/leds/lcd-backlight2/brightness"
#define BACKLIGHT_ALT   "/sys/class/leds/lcd-backlight2/brightness"

#define COLOR_BLACK     0xFF000000
#define COLOR_GREEN     0xFF10B981
#define COLOR_DIM_GREEN 0xFF064E3B

static volatile int running = 1;

static void sig_handler(int sig) {
    running = 0;
}

static void set_backlight(int val) {
    int fd = open(BACKLIGHT_NODE, O_WRONLY);
    if (fd < 0) fd = open(BACKLIGHT_ALT, O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%d\n", val);
        write(fd, buf, len);
        close(fd);
    }
}

// 5x7 ASCII font table
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
    ['_'] = {0x80, 0x80, 0x80, 0x80, 0x80},
    ['a'] = {0x20, 0x54, 0x54, 0x54, 0x78},
    ['b'] = {0x7F, 0x48, 0x44, 0x44, 0x38},
    ['c'] = {0x38, 0x44, 0x44, 0x44, 0x20},
    ['d'] = {0x38, 0x44, 0x44, 0x48, 0x7F},
    ['e'] = {0x38, 0x54, 0x54, 0x54, 0x18},
    ['f'] = {0x08, 0x7E, 0x09, 0x01, 0x02},
    ['g'] = {0x08, 0x54, 0x54, 0x54, 0x3C},
    ['h'] = {0x7F, 0x08, 0x04, 0x04, 0x78},
    ['i'] = {0x00, 0x44, 0x7D, 0x40, 0x00},
    ['j'] = {0x20, 0x40, 0x44, 0x3D, 0x00},
    ['k'] = {0x7F, 0x10, 0x28, 0x44, 0x00},
    ['l'] = {0x00, 0x41, 0x7F, 0x40, 0x00},
    ['m'] = {0x7C, 0x04, 0x18, 0x04, 0x78},
    ['n'] = {0x7C, 0x08, 0x04, 0x04, 0x78},
    ['o'] = {0x38, 0x44, 0x44, 0x44, 0x38},
    ['p'] = {0x7C, 0x14, 0x14, 0x14, 0x08},
    ['q'] = {0x08, 0x14, 0x14, 0x18, 0x7C},
    ['r'] = {0x7C, 0x08, 0x04, 0x04, 0x08},
    ['s'] = {0x48, 0x54, 0x54, 0x54, 0x20},
    ['t'] = {0x04, 0x3F, 0x44, 0x40, 0x20},
    ['u'] = {0x3C, 0x40, 0x40, 0x20, 0x7C},
    ['v'] = {0x1C, 0x20, 0x40, 0x20, 0x1C},
    ['w'] = {0x3C, 0x40, 0x30, 0x40, 0x3C},
    ['x'] = {0x44, 0x28, 0x10, 0x28, 0x44},
    ['y'] = {0x0C, 0x50, 0x50, 0x50, 0x3C},
    ['z'] = {0x44, 0x64, 0x54, 0x4C, 0x44},
};

static void draw_char(uint32_t *fb, int x, int y, char c, uint32_t color) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font5x7[(uint8_t)c];
    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < 128 && py >= 0 && py < 128) {
                    fb[py * 128 + px] = color;
                }
            }
        }
    }
}

static void draw_string(uint32_t *fb, int x, int y, const char *str, uint32_t color) {
    while (*str) {
        draw_char(fb, x, y, *str, color);
        x += 6;
        str++;
    }
}

#define MAX_LINES 14   /* 128px - 14px header = 114px / 8px row = 14 rows */
#define LINE_CHARS 21  /* 128px / 6px per char = 21 chars per line */

static char term_lines[MAX_LINES][LINE_CHARS + 1];
static int num_lines = 0;

static void add_line(const char *raw) {
    // Strip timestamps or syslog prefixes if any
    const char *p = raw;
    if (*p == '<') {
        const char *end = strchr(p, '>');
        if (end) p = end + 1;
    }

    if (num_lines >= MAX_LINES) {
        for (int i = 0; i < MAX_LINES - 1; i++) {
            strncpy(term_lines[i], term_lines[i + 1], LINE_CHARS);
            term_lines[i][LINE_CHARS] = '\0';
        }
        num_lines = MAX_LINES - 1;
    }

    // Copy up to LINE_CHARS
    char clean[LINE_CHARS + 1];
    int idx = 0;
    while (*p && idx < LINE_CHARS) {
        if (*p >= 32 && *p <= 126) {
            clean[idx++] = *p;
        } else if (*p == '\n') {
            break;
        }
        p++;
    }
    clean[idx] = '\0';
    strncpy(term_lines[num_lines], clean, LINE_CHARS);
    term_lines[num_lines][LINE_CHARS] = '\0';
    num_lines++;
}

static void render_terminal(uint32_t *fb) {
    // Header
    for (int i = 0; i < 128 * 128; i++) fb[i] = COLOR_BLACK;

    draw_string(fb, 2, 2, "CAT S22 Flip Booting", COLOR_GREEN);
    for (int x = 0; x < 128; x++) fb[10 * 128 + x] = COLOR_DIM_GREEN;

    int y = 14;
    for (int i = 0; i < num_lines; i++) {
        draw_string(fb, 2, y, term_lines[i], COLOR_GREEN);
        y += 8;
    }
}

int main(void) {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    int fb_fd = -1;
    while (fb_fd < 0 && running) {
        fb_fd = open(FB_DEV, O_RDWR);
        if (fb_fd < 0) usleep(200000);
    }
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

    ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    set_backlight(255);

    int kmsg_fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    /* Seek to end of ring buffer so we see only new messages from here on */
    if (kmsg_fd >= 0) lseek(kmsg_fd, 0, SEEK_END);

    add_line("Kernel display init");
    add_line("OLED 128x128 online");
    render_terminal(fb);
    vinfo.yoffset = 0;
    ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);

    char buf[512];
    struct pollfd pfd;
    pfd.fd = kmsg_fd;
    pfd.events = POLLIN;

    while (running) {
        ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
        set_backlight(255);

        int ret = poll(&pfd, 1, 150);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(kmsg_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                add_line(buf);
            }
        }

        render_terminal(fb);
        vinfo.yoffset = 0;
        ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo);
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
    }

    if (kmsg_fd >= 0) close(kmsg_fd);
    munmap(fb, screensize);
    close(fb_fd);
    return 0;
}
