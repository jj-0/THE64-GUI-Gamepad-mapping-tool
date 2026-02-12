/*
 * gamepad_map - THEC64 Mini Gamepad Mapping Tool
 *
 * Interactive GUI tool that runs on THEC64 Mini's framebuffer to create
 * gamecontrollerdb.txt mapping entries for USB controllers.
 *
 * Shows a graphic of THEJOYSTICK, allows selecting a connected USB
 * controller, and interactively mapping each button/axis.
 *
 * Dependencies: only libc (uses Linux framebuffer and evdev ioctls directly)
 *
 * Cross-compile:
 *   arm-linux-gnueabihf-gcc -static -O2 -o gamepad_map gamepad_map.c
 *
 * Host compile (for testing):
 *   gcc -O2 -o gamepad_map gamepad_map.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>

/* ================================================================
 * Constants
 * ================================================================ */

#define MAX_CONTROLLERS   8
#define MAX_PATH_LEN      512
#define MAX_NAME_LEN      256
#define MAX_DIR_ENTRIES   256
#define GUID_STR_LEN      33
#define NUM_MAPPINGS       10

#define FONT_W             8
#define FONT_H             16

#define DEBOUNCE_MS        300
#define RESCAN_MS         2000
#define BLINK_MS           400
#define NAV_REPEAT_FIRST   400
#define NAV_REPEAT_RATE    120
#define FRAME_MS            16

#define BITS_PER_LONG     (sizeof(long) * 8)
#define NBITS(x)          ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, a)  ((a[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

/* Colours (0xAARRGGBB) */
#define COL_BG          0xFF101828
#define COL_PANEL       0xFF1E2840
#define COL_BODY        0xFF4A4A6A
#define COL_BODY_DARK   0xFF36364E
#define COL_STICK_BASE  0xFF5A5A7A
#define COL_STICK       0xFF6E6E90
#define COL_STICK_TOP   0xFF8888AA
#define COL_BTN         0xFF505078
#define COL_BTN_FIRE    0xFF6E4444
#define COL_HIGHLIGHT   0xFFFFCC00
#define COL_MAPPED      0xFF22BB66
#define COL_TEXT         0xFFD0D0E0
#define COL_TEXT_DIM     0xFF707088
#define COL_TEXT_TITLE   0xFFFFFFFF
#define COL_SELECTED     0xFF2A4488
#define COL_BORDER       0xFF5566AA
#define COL_ERROR        0xFFFF4444
#define COL_SUCCESS      0xFF44FF88
#define COL_HEADER_BG    0xFF182040

/* ================================================================
 * Built-in 8x16 VGA bitmap font (printable ASCII 0x20..0x7E)
 * ================================================================ */

static const uint8_t font8x16[95][16] = {
    /* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x21 '!' */ {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x22 '"' */ {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x23 '#' */ {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
    /* 0x24 '$' */ {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
    /* 0x25 '%' */ {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00},
    /* 0x26 '&' */ {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x27 ''' */ {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x28 '(' */ {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
    /* 0x29 ')' */ {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00},
    /* 0x2A '*' */ {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2B '+' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2C ',' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00},
    /* 0x2D '-' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2E '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x2F '/' */ {0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00},
    /* 0x30 '0' */ {0x00,0x00,0x7C,0xC6,0xC6,0xCE,0xDE,0xF6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x31 '1' */ {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},
    /* 0x32 '2' */ {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 0x33 '3' */ {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x34 '4' */ {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00},
    /* 0x35 '5' */ {0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x36 '6' */ {0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x37 '7' */ {0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00},
    /* 0x38 '8' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x39 '9' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00},
    /* 0x3A ':' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x3B ';' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
    /* 0x3C '<' */ {0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00},
    /* 0x3D '=' */ {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x3E '>' */ {0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
    /* 0x3F '?' */ {0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x40 '@' */ {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00},
    /* 0x41 'A' */ {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x42 'B' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
    /* 0x43 'C' */ {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00},
    /* 0x44 'D' */ {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
    /* 0x45 'E' */ {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    /* 0x46 'F' */ {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 0x47 'G' */ {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00},
    /* 0x48 'H' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x49 'I' */ {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x4A 'J' */ {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
    /* 0x4B 'K' */ {0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x4C 'L' */ {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    /* 0x4D 'M' */ {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x4E 'N' */ {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x4F 'O' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x50 'P' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 0x51 'Q' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00},
    /* 0x52 'R' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x53 'S' */ {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x54 'T' */ {0x00,0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x55 'U' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x56 'V' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
    /* 0x57 'W' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
    /* 0x58 'X' */ {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x59 'Y' */ {0x00,0x00,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x5A 'Z' */ {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 0x5B '[' */ {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},
    /* 0x5C '\' */ {0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00},
    /* 0x5D ']' */ {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},
    /* 0x5E '^' */ {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x5F '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00},
    /* 0x60 '`' */ {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x61 'a' */ {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x62 'b' */ {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},
    /* 0x63 'c' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x64 'd' */ {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x65 'e' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x66 'f' */ {0x00,0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
    /* 0x67 'g' */ {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00},
    /* 0x68 'h' */ {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x69 'i' */ {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x6A 'j' */ {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00},
    /* 0x6B 'k' */ {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x6C 'l' */ {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x6D 'm' */ {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x6E 'n' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
    /* 0x6F 'o' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x70 'p' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00},
    /* 0x71 'q' */ {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00},
    /* 0x72 'r' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 0x73 's' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x74 't' */ {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00},
    /* 0x75 'u' */ {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x76 'v' */ {0x00,0x00,0x00,0x00,0x00,0xC3,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0x00,0x00,0x00,0x00},
    /* 0x77 'w' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00},
    /* 0x78 'x' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    /* 0x79 'y' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00,0x00},
    /* 0x7A 'z' */ {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 0x7B '{' */ {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},
    /* 0x7C '|' */ {0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x7D '}' */ {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00},
    /* 0x7E '~' */ {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* ================================================================
 * Data structures
 * ================================================================ */

typedef struct {
    int       fd;
    uint32_t *pixels;      /* mmap'd framebuffer */
    uint32_t *backbuf;     /* double buffer */
    int       width;
    int       height;
    int       stride_px;   /* pixels per line (may be > width) */
    size_t    size;
} Framebuffer;

typedef struct {
    int              fd;
    char             path[MAX_PATH_LEN];
    char             name[MAX_NAME_LEN];
    char             guid[GUID_STR_LEN];
    struct input_id  id;
    int              num_buttons;
    int              num_axes;
    int              num_hats;
    int              btn_map[KEY_MAX];
    int              abs_map[ABS_MAX];
    int              hat_map[ABS_MAX];
    int              axis_initial[ABS_MAX];
    int              axis_min[ABS_MAX];
    int              axis_max[ABS_MAX];
} Controller;

typedef enum { MAP_NONE = 0, MAP_BUTTON, MAP_AXIS, MAP_HAT } MapType;

typedef struct {
    const char *the64_label;
    const char *gcdb_name;
    int         is_axis;       /* 1 = axis prompt, 0 = button prompt */
    const char *prompt;        /* mapping instruction */
    MapType     mapped_type;
    int         mapped_index;
    int         hat_mask;
} MappingEntry;

typedef struct {
    char name[MAX_NAME_LEN];
    int  is_dir;
} DirEntry;

typedef struct {
    char     path[MAX_PATH_LEN];
    DirEntry entries[MAX_DIR_ENTRIES];
    int      count;
    int      selected;
    int      scroll;
} DirBrowser;

typedef enum {
    STATE_DETECT,
    STATE_MAPPING,
    STATE_REVIEW,
    STATE_BROWSE,
    STATE_DONE,
    STATE_EXIT
} AppState;

/* Review screen action items (after the 10 mapping rows) */
#define REVIEW_ACTION_SAVE    NUM_MAPPINGS       /* index 10 */
#define REVIEW_ACTION_RESTART (NUM_MAPPINGS + 1) /* index 11 */
#define REVIEW_ACTION_ANOTHER (NUM_MAPPINGS + 2) /* index 12 */
#define REVIEW_ACTION_QUIT    (NUM_MAPPINGS + 3) /* index 13 */
#define REVIEW_TOTAL_ITEMS    (NUM_MAPPINGS + 4) /* 14 items */

typedef struct {
    Framebuffer  fb;
    AppState     state;
    Controller   controllers[MAX_CONTROLLERS];
    int          num_controllers;
    int          sel_ctrl;
    MappingEntry mappings[NUM_MAPPINGS];
    int          cur_map;
    int          redo_single;        /* -1 = normal, >=0 = redo that one */
    DirBrowser   browser;
    int          blink;
    uint64_t     blink_time;
    uint64_t     last_scan;
    int          review_sel;
    char         save_path[MAX_PATH_LEN];
    char         mapping_str[1024];
    /* navigation repeat */
    int          nav_held_dir;       /* -1=up, 1=down, 0=none */
    uint64_t     nav_repeat_time;
    /* keyboard input */
    int          kbd_fds[8];
    int          num_kbd_fds;
    /* THEJOYSTICK as always-available navigator (-1 = not available) */
    int          thec64_nav_idx;
} App;

static volatile sig_atomic_t g_quit = 0;

static void sig_handler(int sig) {
    (void)sig;
    g_quit = 1;
}

/* ================================================================
 * Utility
 * ================================================================ */

static uint64_t time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ================================================================
 * Framebuffer
 * ================================================================ */

static int fb_init(Framebuffer *fb)
{
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    memset(fb, 0, sizeof(*fb));

    fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) {
        perror("open /dev/fb0");
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        close(fb->fd);
        return -1;
    }
    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("FBIOGET_FSCREENINFO");
        close(fb->fd);
        return -1;
    }

    fb->width     = vinfo.xres;
    fb->height    = vinfo.yres;
    fb->stride_px = finfo.line_length / (vinfo.bits_per_pixel / 8);
    fb->size      = (size_t)finfo.line_length * vinfo.yres;

    fb->pixels = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fb->fd, 0);
    if (fb->pixels == MAP_FAILED) {
        perror("mmap framebuffer");
        close(fb->fd);
        return -1;
    }

    fb->backbuf = malloc(fb->size);
    if (!fb->backbuf) {
        munmap(fb->pixels, fb->size);
        close(fb->fd);
        return -1;
    }

    memset(fb->backbuf, 0, fb->size);
    fprintf(stderr, "Framebuffer: %dx%d, stride=%d px\n",
            fb->width, fb->height, fb->stride_px);
    return 0;
}

static void fb_flip(Framebuffer *fb)
{
    memcpy(fb->pixels, fb->backbuf, fb->size);
}

static void fb_clear(Framebuffer *fb, uint32_t color)
{
    int total = fb->stride_px * fb->height;
    for (int i = 0; i < total; i++)
        fb->backbuf[i] = color;
}

static void fb_destroy(Framebuffer *fb)
{
    if (fb->backbuf) free(fb->backbuf);
    if (fb->pixels && fb->pixels != MAP_FAILED)
        munmap(fb->pixels, fb->size);
    if (fb->fd >= 0) close(fb->fd);
}

/* ================================================================
 * Drawing primitives
 * ================================================================ */

static inline void draw_pixel(Framebuffer *fb, int x, int y, uint32_t c)
{
    if (x >= 0 && x < fb->width && y >= 0 && y < fb->height)
        fb->backbuf[y * fb->stride_px + x] = c;
}

static void draw_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t c)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            draw_pixel(fb, col, row, c);
}

static void draw_circle(Framebuffer *fb, int cx, int cy, int r, uint32_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while (dx * dx + dy * dy <= r * r) dx++;
        draw_rect(fb, cx - dx + 1, cy + dy, 2 * dx - 1, 1, c);
    }
}

static void draw_rounded_rect(Framebuffer *fb, int x, int y, int w, int h,
                                int r, uint32_t c)
{
    if (r < 1) { draw_rect(fb, x, y, w, h, c); return; }
    /* centre fill */
    draw_rect(fb, x + r, y, w - 2 * r, h, c);
    /* left and right strips */
    draw_rect(fb, x, y + r, r, h - 2 * r, c);
    draw_rect(fb, x + w - r, y + r, r, h - 2 * r, c);
    /* four corners */
    for (int dy = -r; dy <= 0; dy++) {
        int dx = 0;
        while (dx * dx + dy * dy <= r * r) dx++;
        /* top-left */
        draw_rect(fb, x + r - dx + 1, y + r + dy, dx - 1, 1, c);
        /* top-right */
        draw_rect(fb, x + w - r, y + r + dy, dx - 1, 1, c);
        /* bottom-left */
        draw_rect(fb, x + r - dx + 1, y + h - 1 - r - dy, dx - 1, 1, c);
        /* bottom-right */
        draw_rect(fb, x + w - r, y + h - 1 - r - dy, dx - 1, 1, c);
    }
}

static void draw_triangle_filled(Framebuffer *fb, int x0, int y0,
                                  int x1, int y1, int x2, int y2, uint32_t c)
{
    /* sort vertices by y */
    int tx, ty;
    if (y0 > y1) { tx=x0;ty=y0;x0=x1;y0=y1;x1=tx;y1=ty; }
    if (y0 > y2) { tx=x0;ty=y0;x0=x2;y0=y2;x2=tx;y2=ty; }
    if (y1 > y2) { tx=x1;ty=y1;x1=x2;y1=y2;x2=tx;y2=ty; }

    for (int y = y0; y <= y2; y++) {
        int xa, xb;
        if (y2 != y0)
            xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        else
            xa = x0;
        if (y < y1) {
            if (y1 != y0)
                xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
            else
                xb = x0;
        } else {
            if (y2 != y1)
                xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
            else
                xb = x1;
        }
        if (xa > xb) { tx = xa; xa = xb; xb = tx; }
        draw_rect(fb, xa, y, xb - xa + 1, 1, c);
    }
}

/* ================================================================
 * Text rendering (built-in 8x16 font)
 * ================================================================ */

static void draw_char(Framebuffer *fb, int x, int y, char ch, uint32_t c,
                       int scale)
{
    int idx = (unsigned char)ch - 0x20;
    if (idx < 0 || idx >= 95) return;
    const uint8_t *glyph = font8x16[idx];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80 >> col)) {
                if (scale == 1) {
                    draw_pixel(fb, x + col, y + row, c);
                } else {
                    draw_rect(fb, x + col * scale, y + row * scale,
                              scale, scale, c);
                }
            }
        }
    }
}

static void draw_text(Framebuffer *fb, int x, int y, const char *text,
                       uint32_t c, int scale)
{
    while (*text) {
        draw_char(fb, x, y, *text, c, scale);
        x += FONT_W * scale;
        text++;
    }
}

static int text_width(const char *text, int scale)
{
    return (int)strlen(text) * FONT_W * scale;
}

static void draw_text_centered(Framebuffer *fb, int cx, int y, const char *text,
                                uint32_t c, int scale)
{
    int w = text_width(text, scale);
    draw_text(fb, cx - w / 2, y, text, c, scale);
}

/* ================================================================
 * GUID construction (same as gamepad_guid.c)
 * ================================================================ */

static void build_guid(const struct input_id *id, char *guid_str)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char guid[16];

    guid[0]  = id->bustype & 0xFF;
    guid[1]  = (id->bustype >> 8) & 0xFF;
    guid[2]  = 0; guid[3]  = 0;
    guid[4]  = id->vendor & 0xFF;
    guid[5]  = (id->vendor >> 8) & 0xFF;
    guid[6]  = 0; guid[7]  = 0;
    guid[8]  = id->product & 0xFF;
    guid[9]  = (id->product >> 8) & 0xFF;
    guid[10] = 0; guid[11] = 0;
    guid[12] = id->version & 0xFF;
    guid[13] = (id->version >> 8) & 0xFF;
    guid[14] = 0; guid[15] = 0;

    for (int i = 0; i < 16; i++) {
        guid_str[i * 2]     = hex[guid[i] >> 4];
        guid_str[i * 2 + 1] = hex[guid[i] & 0x0F];
    }
    guid_str[32] = '\0';
}

/* ================================================================
 * Controller detection and input
 * ================================================================ */

static int is_gamepad(int fd)
{
    unsigned long evbits[NBITS(EV_MAX)];
    unsigned long absbits[NBITS(ABS_MAX)];
    unsigned long keybits[NBITS(KEY_MAX)];

    memset(evbits, 0, sizeof(evbits));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;

    if (TEST_BIT(EV_ABS, evbits)) {
        memset(absbits, 0, sizeof(absbits));
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0)
            if (TEST_BIT(ABS_X, absbits) && TEST_BIT(ABS_Y, absbits))
                return 1;
    }

    if (TEST_BIT(EV_KEY, evbits)) {
        memset(keybits, 0, sizeof(keybits));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
            for (int i = BTN_JOYSTICK; i < BTN_JOYSTICK + 16; i++)
                if (TEST_BIT(i, keybits)) return 1;
            for (int i = BTN_GAMEPAD; i < BTN_GAMEPAD + 16; i++)
                if (TEST_BIT(i, keybits)) return 1;
        }
    }
    return 0;
}

static void enumerate_buttons_axes(Controller *c)
{
    unsigned long keybits[NBITS(KEY_MAX)];
    unsigned long absbits[NBITS(ABS_MAX)];
    struct input_absinfo absinfo;

    c->num_buttons = 0;
    c->num_axes = 0;
    c->num_hats = 0;
    memset(c->btn_map, 0xFF, sizeof(c->btn_map));  /* -1 */
    memset(c->abs_map, 0xFF, sizeof(c->abs_map));
    memset(c->hat_map, 0xFF, sizeof(c->hat_map));
    memset(c->axis_initial, 0, sizeof(c->axis_initial));

    /* Buttons: SDL2 order - BTN_JOYSTICK..KEY_MAX, then BTN_MISC..BTN_JOYSTICK-1 */
    memset(keybits, 0, sizeof(keybits));
    ioctl(c->fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);

    for (int i = BTN_JOYSTICK; i < KEY_MAX; i++)
        if (TEST_BIT(i, keybits))
            c->btn_map[i] = c->num_buttons++;
    for (int i = BTN_MISC; i < BTN_JOYSTICK; i++)
        if (TEST_BIT(i, keybits))
            c->btn_map[i] = c->num_buttons++;

    /* Axes: sequential, skip HAT range */
    memset(absbits, 0, sizeof(absbits));
    ioctl(c->fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);

    for (int i = 0; i < ABS_MAX; i++) {
        if (!TEST_BIT(i, absbits)) continue;

        memset(&absinfo, 0, sizeof(absinfo));
        ioctl(c->fd, EVIOCGABS(i), &absinfo);
        c->axis_min[i] = absinfo.minimum;
        c->axis_max[i] = absinfo.maximum;
        /* Use midpoint of range as center for axes where initial value
         * might be at the extreme (e.g. triggers starting at 0) */
        c->axis_initial[i] = (absinfo.minimum + absinfo.maximum) / 2;

        if (i >= ABS_HAT0X && i <= ABS_HAT3Y) {
            c->hat_map[i] = (i - ABS_HAT0X) / 2;
            if (c->hat_map[i] >= c->num_hats)
                c->num_hats = c->hat_map[i] + 1;
        } else {
            c->abs_map[i] = c->num_axes++;
        }
    }
}

static void scan_controllers(App *app)
{
    DIR *dir;
    struct dirent *entry;
    char path[MAX_PATH_LEN];

    /* close previously opened fds */
    for (int i = 0; i < app->num_controllers; i++)
        if (app->controllers[i].fd >= 0)
            close(app->controllers[i].fd);
    app->num_controllers = 0;

    dir = opendir("/dev/input");
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL) {
        if (app->num_controllers >= MAX_CONTROLLERS) break;
        if (strlen(entry->d_name) <= 5) continue;
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        if (!is_gamepad(fd)) { close(fd); continue; }

        Controller *c = &app->controllers[app->num_controllers];
        memset(c, 0, sizeof(*c));
        c->fd = fd;
        snprintf(c->path, sizeof(c->path), "%s", path);

        if (ioctl(fd, EVIOCGID, &c->id) < 0) { close(fd); continue; }

        memset(c->name, 0, sizeof(c->name));
        if (ioctl(fd, EVIOCGNAME(sizeof(c->name) - 1), c->name) < 0)
            strcpy(c->name, "Unknown Controller");

        build_guid(&c->id, c->guid);
        enumerate_buttons_axes(c);
        app->num_controllers++;
    }
    closedir(dir);
}

static void close_controllers(App *app)
{
    for (int i = 0; i < app->num_controllers; i++)
        if (app->controllers[i].fd >= 0)
            close(app->controllers[i].fd);
    app->num_controllers = 0;
}

static void drain_events(int fd)
{
    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
        ;
}

static void drain_nav_events(App *app)
{
    drain_events(app->controllers[app->sel_ctrl].fd);
    if (app->thec64_nav_idx >= 0)
        drain_events(app->controllers[app->thec64_nav_idx].fd);
}

/* ================================================================
 * THEJOYSTICK detection
 * ================================================================ */

static int is_thec64_joystick(Controller *c)
{
    if (strstr(c->name, "THEC64 Joystick") != NULL)
        return 1;
    if (strcmp(c->guid, "03000000591c00002300000010010000") == 0)
        return 1;
    if (strcmp(c->guid, "03000000591c00002400000010010000") == 0)
        return 1;
    return 0;
}

/* Find THEJOYSTICK among detected controllers (excluding selected one) */
static void find_thec64_nav(App *app)
{
    app->thec64_nav_idx = -1;
    for (int i = 0; i < app->num_controllers; i++) {
        if (i == app->sel_ctrl) continue;
        if (is_thec64_joystick(&app->controllers[i])) {
            app->thec64_nav_idx = i;
            return;
        }
    }
}

/* Read THEJOYSTICK navigation input using hardcoded mappings:
 *   ABS_X (0-255, center 127) → dx
 *   ABS_Y (0-255, center 127) → dy
 *   BTN_TRIGGER (288) / BTN_TOP2 (292) → btn_a (Left Fire / Menu 1)
 *   BTN_PINKIE  (293) → btn_b (Menu 2)
 *   BTN_BASE2   (295) → btn_start (Menu 4)
 */
static int read_thec64_nav(App *app, int *dy, int *dx,
                           int *btn_a, int *btn_b, int *btn_start)
{
    if (app->thec64_nav_idx < 0) return 0;
    Controller *c = &app->controllers[app->thec64_nav_idx];
    struct input_event ev;
    int got = 0;

    while (read(c->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            if (ev.code == BTN_TRIGGER || ev.code == BTN_TOP2)
                { *btn_a = 1; got = 1; }
            else if (ev.code == BTN_PINKIE)
                { *btn_b = 1; got = 1; }
            else if (ev.code == BTN_BASE2)
                { *btn_start = 1; got = 1; }
        }
        else if (ev.type == EV_ABS) {
            /* ABS_Y → dy, ABS_X → dx  (range 0-255, center ~127) */
            int delta = ev.value - 127;
            int thresh = 50; /* ~40% of half-range (127) */
            if (ev.code == ABS_Y) {
                if (delta < -thresh) { *dy = -1; got = 1; }
                else if (delta > thresh) { *dy = 1; got = 1; }
            }
            if (ev.code == ABS_X) {
                if (delta < -thresh) { *dx = -1; got = 1; }
                else if (delta > thresh) { *dx = 1; got = 1; }
            }
        }
    }
    return got;
}

/* ================================================================
 * Keyboard detection and input
 * ================================================================ */

static int is_keyboard(int fd)
{
    unsigned long evbits[NBITS(EV_MAX)];
    unsigned long keybits[NBITS(KEY_MAX)];

    memset(evbits, 0, sizeof(evbits));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;
    if (!TEST_BIT(EV_KEY, evbits))
        return 0;

    memset(keybits, 0, sizeof(keybits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0)
        return 0;

    /* Must have letter keys (KEY_Q=16..KEY_P=25) to be a real keyboard */
    return TEST_BIT(KEY_Q, keybits) && TEST_BIT(KEY_A, keybits);
}

static void scan_keyboards(App *app)
{
    DIR *dir;
    struct dirent *entry;
    char path[MAX_PATH_LEN];

    app->num_kbd_fds = 0;

    dir = opendir("/dev/input");
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL) {
        if (app->num_kbd_fds >= 8) break;
        if (strlen(entry->d_name) <= 5) continue;
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        if (is_keyboard(fd)) {
            app->kbd_fds[app->num_kbd_fds++] = fd;
        } else {
            close(fd);
        }
    }
    closedir(dir);
}

static void close_keyboards(App *app)
{
    for (int i = 0; i < app->num_kbd_fds; i++)
        close(app->kbd_fds[i]);
    app->num_kbd_fds = 0;
}

/* Read keyboard events, return key code of pressed key (0 if none) */
static int read_keyboard(App *app)
{
    struct input_event ev;
    for (int i = 0; i < app->num_kbd_fds; i++) {
        while (read(app->kbd_fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1)
                return ev.code;
        }
    }
    return 0;
}

/* ================================================================
 * Mapping definitions
 * ================================================================ */

static void init_mappings(MappingEntry *m)
{
    m[0] = (MappingEntry){"Left Fire",      "lefttrigger", 0,
                           "Press LEFT FIRE button",       MAP_NONE, 0, 0};
    m[1] = (MappingEntry){"Right Fire",     "righttrigger", 0,
                           "Press RIGHT FIRE button",      MAP_NONE, 0, 0};
    m[2] = (MappingEntry){"Left Triangle",  "x", 0,
                           "Press LEFT TRIANGLE button",   MAP_NONE, 0, 0};
    m[3] = (MappingEntry){"Right Triangle", "y", 0,
                           "Press RIGHT TRIANGLE button",  MAP_NONE, 0, 0};
    m[4] = (MappingEntry){"Menu 1",         "a", 0,
                           "Press MENU 1 button",          MAP_NONE, 0, 0};
    m[5] = (MappingEntry){"Menu 2",         "b", 0,
                           "Press MENU 2 button",          MAP_NONE, 0, 0};
    m[6] = (MappingEntry){"Menu 3",         "back", 0,
                           "Press MENU 3 button",          MAP_NONE, 0, 0};
    m[7] = (MappingEntry){"Menu 4",         "start", 0,
                           "Press MENU 4 button",          MAP_NONE, 0, 0};
    m[8] = (MappingEntry){"Left/Right",     "leftx", 1,
                           "Move stick LEFT or RIGHT",     MAP_NONE, 0, 0};
    m[9] = (MappingEntry){"Up/Down",        "lefty", 1,
                           "Move stick UP or DOWN",        MAP_NONE, 0, 0};
}

/* ================================================================
 * Mapping string generation
 * ================================================================ */

static void build_mapping_string(App *app, char *out, size_t sz)
{
    Controller *c = &app->controllers[app->sel_ctrl];
    int pos = 0;

    pos += snprintf(out + pos, sz - pos, "%s,%s,", c->guid, c->name);

    for (int i = 0; i < NUM_MAPPINGS; i++) {
        MappingEntry *m = &app->mappings[i];
        pos += snprintf(out + pos, sz - pos, "%s:", m->gcdb_name);
        switch (m->mapped_type) {
        case MAP_BUTTON:
            pos += snprintf(out + pos, sz - pos, "b%d", m->mapped_index);
            break;
        case MAP_AXIS:
            pos += snprintf(out + pos, sz - pos, "a%d", m->mapped_index);
            break;
        case MAP_HAT:
            pos += snprintf(out + pos, sz - pos, "h%d.%d",
                            m->mapped_index, m->hat_mask);
            break;
        default:
            break;
        }
        pos += snprintf(out + pos, sz - pos, ",");
    }
    pos += snprintf(out + pos, sz - pos, "platform:Linux,");
    (void)pos;
}

/* ================================================================
 * Draw THEJOYSTICK graphic
 * ================================================================ */

/* joystick element bounding boxes (relative to origin) */
#define JOY_W  600
#define JOY_H  300

/* Element colour based on mapping state */
static uint32_t elem_color(App *app, int idx, uint32_t normal)
{
    if (app->state == STATE_MAPPING && app->cur_map == idx && app->blink)
        return COL_HIGHLIGHT;
    if (app->mappings[idx].mapped_type != MAP_NONE)
        return COL_MAPPED;
    return normal;
}

/* Colour for the stick (shared by leftx=8 and lefty=9) */
static uint32_t stick_color(App *app)
{
    if (app->state == STATE_MAPPING &&
        (app->cur_map == 8 || app->cur_map == 9) && app->blink)
        return COL_HIGHLIGHT;
    if (app->mappings[8].mapped_type != MAP_NONE &&
        app->mappings[9].mapped_type != MAP_NONE)
        return COL_MAPPED;
    if (app->mappings[8].mapped_type != MAP_NONE ||
        app->mappings[9].mapped_type != MAP_NONE)
        return 0xFF66AA44;  /* partial = yellow-green */
    return COL_STICK_TOP;
}

static void draw_joystick(Framebuffer *fb, App *app, int ox, int oy)
{
    /* Body shadow */
    draw_rounded_rect(fb, ox + 33, oy + 53, 540, 180, 20, COL_BODY_DARK);
    /* Body */
    draw_rounded_rect(fb, ox + 30, oy + 50, 540, 180, 20, COL_BODY);

    /* Left fire button */
    draw_rounded_rect(fb, ox + 38, oy + 100, 108, 40, 10,
                       elem_color(app, 0, COL_BTN_FIRE));
    draw_text_centered(fb, ox + 92, oy + 108, "L.Fire", COL_TEXT, 1);

    /* Right fire button */
    draw_rounded_rect(fb, ox + 454, oy + 100, 108, 40, 10,
                       elem_color(app, 1, COL_BTN_FIRE));
    draw_text_centered(fb, ox + 508, oy + 108, "R.Fire", COL_TEXT, 1);

    /* Stick base circle */
    draw_circle(fb, ox + 220, oy + 135, 50, COL_STICK_BASE);
    /* Stick shaft */
    draw_rect(fb, ox + 213, oy + 60, 14, 75, COL_STICK);
    /* Stick ball */
    draw_circle(fb, ox + 220, oy + 55, 22, stick_color(app));

    /* Stick direction labels */
    if (app->state == STATE_MAPPING && app->cur_map == 8) {
        /* leftx: show L/R arrows */
        draw_text(fb, ox + 155, oy + 48, "<", COL_HIGHLIGHT, 2);
        draw_text(fb, ox + 262, oy + 48, ">", COL_HIGHLIGHT, 2);
    }
    if (app->state == STATE_MAPPING && app->cur_map == 9) {
        /* lefty: show U/D arrows */
        draw_text_centered(fb, ox + 220, oy + 15, "^", COL_HIGHLIGHT, 2);
        draw_text_centered(fb, ox + 220, oy + 185, "v", COL_HIGHLIGHT, 2);
    }

    /* Left triangle button */
    {
        uint32_t tc = elem_color(app, 2, COL_BTN);
        int cx = ox + 290, cy = oy + 205;
        draw_triangle_filled(fb, cx, cy - 16, cx - 14, cy + 10, cx + 14, cy + 10, tc);
        draw_text_centered(fb, cx, cy + 16, "L.Tri", COL_TEXT, 1);
    }
    /* Right triangle button */
    {
        uint32_t tc = elem_color(app, 3, COL_BTN);
        int cx = ox + 365, cy = oy + 205;
        draw_triangle_filled(fb, cx, cy - 16, cx - 14, cy + 10, cx + 14, cy + 10, tc);
        draw_text_centered(fb, cx, cy + 16, "R.Tri", COL_TEXT, 1);
    }

    /* Menu buttons 1-4 */
    {
        int mw = 50, mh = 22, gap = 10;
        int total = 4 * mw + 3 * gap;
        int sx = ox + (JOY_W - total) / 2;
        int sy = oy + 248;
        const char *labels[] = {"M1", "M2", "M3", "M4"};
        for (int i = 0; i < 4; i++) {
            int mx = sx + i * (mw + gap);
            uint32_t mc = elem_color(app, 4 + i, COL_BTN);
            draw_rounded_rect(fb, mx, sy, mw, mh, 6, mc);
            draw_text_centered(fb, mx + mw / 2, sy + 3, labels[i], COL_TEXT, 1);
        }
    }

    /* Label stick */
    draw_text_centered(fb, ox + 220, oy + 190, "Stick", COL_TEXT_DIM, 1);
}

/* ================================================================
 * Directory browser
 * ================================================================ */

static int dir_entry_cmp(const void *a, const void *b)
{
    const DirEntry *da = a, *db = b;
    /* dirs before files */
    if (da->is_dir != db->is_dir)
        return db->is_dir - da->is_dir;
    return strcasecmp(da->name, db->name);
}

static void browser_load(DirBrowser *b, const char *path)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char fullpath[MAX_PATH_LEN];

    strncpy(b->path, path, MAX_PATH_LEN - 1);
    b->count = 0;
    b->selected = 0;
    b->scroll = 0;

    /* add ".." unless root */
    if (strcmp(b->path, "/") != 0) {
        strcpy(b->entries[0].name, "..");
        b->entries[0].is_dir = 1;
        b->count = 1;
    }

    dir = opendir(path);
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL && b->count < MAX_DIR_ENTRIES) {
        if (entry->d_name[0] == '.') continue;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (stat(fullpath, &st) < 0) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        snprintf(b->entries[b->count].name, sizeof(b->entries[b->count].name),
                 "%s", entry->d_name);
        b->entries[b->count].is_dir = 1;
        b->count++;
    }
    closedir(dir);

    /* sort (skip ".." at index 0 if present) */
    int start = (b->count > 0 && strcmp(b->entries[0].name, "..") == 0) ? 1 : 0;
    if (b->count - start > 1)
        qsort(&b->entries[start], b->count - start, sizeof(DirEntry),
              dir_entry_cmp);

    /* add export action at the end */
    if (b->count < MAX_DIR_ENTRIES) {
        snprintf(b->entries[b->count].name, sizeof(b->entries[b->count].name),
                 ">> Export here <<");
        b->entries[b->count].is_dir = 0;
        b->count++;
    }
}

/* ================================================================
 * Navigation input (using mapped controls)
 * ================================================================ */

static int read_nav_input(App *app, int *dy, int *dx, int *btn_a, int *btn_b,
                           int *btn_start)
{
    Controller *c = &app->controllers[app->sel_ctrl];
    struct input_event ev;

    *dy = 0; *dx = 0; *btn_a = 0; *btn_b = 0; *btn_start = 0;

    while (read(c->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            int idx = c->btn_map[ev.code];
            if (idx < 0) continue;
            /* compare with mapped buttons */
            if (app->mappings[0].mapped_type == MAP_BUTTON &&
                idx == app->mappings[0].mapped_index) *btn_a = 1; /* Left Fire = confirm */
            if (app->mappings[4].mapped_type == MAP_BUTTON &&
                idx == app->mappings[4].mapped_index) *btn_a = 1;
            if (app->mappings[5].mapped_type == MAP_BUTTON &&
                idx == app->mappings[5].mapped_index) *btn_b = 1;
            if (app->mappings[7].mapped_type == MAP_BUTTON &&
                idx == app->mappings[7].mapped_index) *btn_start = 1;
        }
        else if (ev.type == EV_ABS) {
            /* lefty (index 9) for vertical nav */
            MappingEntry *my = &app->mappings[9];
            if (my->mapped_type == MAP_AXIS && c->abs_map[ev.code] == my->mapped_index) {
                int range = c->axis_max[ev.code] - c->axis_min[ev.code];
                int thresh = range > 0 ? range * 2 / 5 : 1;
                int delta = ev.value - c->axis_initial[ev.code];
                if (delta < -thresh) *dy = -1;
                else if (delta > thresh) *dy = 1;
            }
            if (my->mapped_type == MAP_HAT && c->hat_map[ev.code] == my->mapped_index) {
                if (ev.value < 0) *dy = -1;
                else if (ev.value > 0) *dy = 1;
            }
            /* leftx (index 8) for horizontal nav */
            MappingEntry *mx = &app->mappings[8];
            if (mx->mapped_type == MAP_AXIS && c->abs_map[ev.code] == mx->mapped_index) {
                int range = c->axis_max[ev.code] - c->axis_min[ev.code];
                int thresh = range > 0 ? range * 2 / 5 : 1;
                int delta = ev.value - c->axis_initial[ev.code];
                if (delta < -thresh) *dx = -1;
                else if (delta > thresh) *dx = 1;
            }
            if (mx->mapped_type == MAP_HAT && c->hat_map[ev.code] == mx->mapped_index) {
                if (ev.value < 0) *dx = -1;
                else if (ev.value > 0) *dx = 1;
            }
        }
    }

    /* Also read THEJOYSTICK if available (merges into same outputs) */
    read_thec64_nav(app, dy, dx, btn_a, btn_b, btn_start);

    return (*dy || *dx || *btn_a || *btn_b || *btn_start);
}

/* ================================================================
 * Mapping input detection
 * ================================================================ */

static int poll_mapping_input(App *app, MappingEntry *entry)
{
    Controller *c = &app->controllers[app->sel_ctrl];
    struct input_event ev;

    while (read(c->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            int idx = c->btn_map[ev.code];
            if (idx >= 0) {
                entry->mapped_type = MAP_BUTTON;
                entry->mapped_index = idx;
                return 1;
            }
        }
        else if (ev.type == EV_ABS) {
            if (ev.code >= ABS_HAT0X && ev.code <= ABS_HAT3Y && ev.value != 0) {
                int hat = (ev.code - ABS_HAT0X) / 2;
                int mask;
                if ((ev.code - ABS_HAT0X) % 2 == 0)
                    mask = (ev.value < 0) ? 8 : 2;   /* L=8, R=2 */
                else
                    mask = (ev.value < 0) ? 1 : 4;   /* U=1, D=4 */
                entry->mapped_type = MAP_HAT;
                entry->mapped_index = hat;
                entry->hat_mask = mask;
                return 1;
            }
            else {
                int aidx = c->abs_map[ev.code];
                if (aidx >= 0) {
                    /* Use 40% of half-range as threshold, works for all axis sizes */
                    int range = c->axis_max[ev.code] - c->axis_min[ev.code];
                    int thresh = range > 0 ? range * 2 / 5 : 1;
                    int delta = ev.value - c->axis_initial[ev.code];
                    if (delta > thresh || delta < -thresh) {
                        entry->mapped_type = MAP_AXIS;
                        entry->mapped_index = aidx;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

/* ================================================================
 * State: detect controller
 * ================================================================ */

static void update_detect(App *app)
{
    uint64_t now = time_ms();

    /* Periodic rescan */
    if (now - app->last_scan > RESCAN_MS) {
        scan_controllers(app);
        app->last_scan = now;
    }

    /* Check for button press on any controller */
    for (int i = 0; i < app->num_controllers; i++) {
        struct input_event ev;
        while (read(app->controllers[i].fd, &ev, sizeof(ev)) ==
               (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                app->sel_ctrl = i;
                find_thec64_nav(app);
                /* drain all controllers */
                for (int j = 0; j < app->num_controllers; j++)
                    drain_events(app->controllers[j].fd);
                app->state = STATE_MAPPING;
                app->cur_map = 0;
                app->redo_single = -1;
                return;
            }
        }
    }
}

static void render_detect(App *app)
{
    Framebuffer *fb = &app->fb;
    int cx = fb->width / 2;

    draw_text_centered(fb, cx, 60, "THEC64 GAMEPAD MAPPER", COL_TEXT_TITLE, 3);

    draw_text_centered(fb, cx, 180, "Press any button on the controller",
                        COL_TEXT, 2);
    draw_text_centered(fb, cx, 220, "you want to map", COL_TEXT, 2);

    int y = 320;
    if (app->num_controllers == 0) {
        draw_text_centered(fb, cx, y,
                            "No controllers detected. Connect a USB controller.",
                            COL_TEXT_DIM, 1);
    } else {
        draw_text_centered(fb, cx, y - 30, "Detected controllers:", COL_TEXT, 1);
        for (int i = 0; i < app->num_controllers; i++) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%d. %s  [%s]",
                     i + 1, app->controllers[i].name, app->controllers[i].path);
            draw_text(fb, 100, y + i * 24, buf, COL_TEXT, 1);
        }
    }
}

/* ================================================================
 * State: mapping
 * ================================================================ */

static void update_mapping(App *app)
{
    MappingEntry *m = &app->mappings[app->cur_map];
    if (poll_mapping_input(app, m)) {
        drain_events(app->controllers[app->sel_ctrl].fd);
        usleep(DEBOUNCE_MS * 1000);
        drain_events(app->controllers[app->sel_ctrl].fd);

        if (app->redo_single >= 0) {
            /* was redoing a single mapping, go back to review */
            app->redo_single = -1;
            app->state = STATE_REVIEW;
            return;
        }

        app->cur_map++;
        if (app->cur_map >= NUM_MAPPINGS) {
            app->state = STATE_REVIEW;
            app->review_sel = 0;
            /* generate mapping string */
            build_mapping_string(app, app->mapping_str, sizeof(app->mapping_str));
        }
    }
}

static void render_mapping(App *app)
{
    Framebuffer *fb = &app->fb;
    int cx = fb->width / 2;
    MappingEntry *m = &app->mappings[app->cur_map];
    char buf[256];

    /* Header bar */
    draw_rect(fb, 0, 0, fb->width, 36, COL_HEADER_BG);
    snprintf(buf, sizeof(buf), "Mapping: %s (%d/%d)",
             app->controllers[app->sel_ctrl].name,
             app->cur_map + 1, NUM_MAPPINGS);
    draw_text(fb, 16, 10, buf, COL_TEXT, 1);

    snprintf(buf, sizeof(buf), "GUID: %s", app->controllers[app->sel_ctrl].guid);
    draw_text(fb, fb->width - text_width(buf, 1) - 16, 10, buf, COL_TEXT_DIM, 1);

    /* Joystick graphic */
    int jx = cx - JOY_W / 2;
    int jy = 50;
    draw_joystick(fb, app, jx, jy);

    /* Prompt */
    int py = jy + JOY_H + 20;
    snprintf(buf, sizeof(buf), ">>> %s <<<", m->prompt);
    draw_text_centered(fb, cx, py, buf, app->blink ? COL_HIGHLIGHT : COL_TEXT, 2);

    /* Sub-label */
    snprintf(buf, sizeof(buf), "for: %s (%s)", m->the64_label, m->gcdb_name);
    draw_text_centered(fb, cx, py + 40, buf, COL_TEXT_DIM, 1);

    /* Already mapped summary */
    int sy = py + 70;
    draw_text(fb, 100, sy, "Mapped so far:", COL_TEXT_DIM, 1);
    sy += 20;
    for (int i = 0; i < app->cur_map; i++) {
        MappingEntry *mi = &app->mappings[i];
        switch (mi->mapped_type) {
        case MAP_BUTTON:
            snprintf(buf, sizeof(buf), "  %s = b%d", mi->gcdb_name,
                     mi->mapped_index);
            break;
        case MAP_AXIS:
            snprintf(buf, sizeof(buf), "  %s = a%d", mi->gcdb_name,
                     mi->mapped_index);
            break;
        case MAP_HAT:
            snprintf(buf, sizeof(buf), "  %s = h%d.%d", mi->gcdb_name,
                     mi->mapped_index, mi->hat_mask);
            break;
        default:
            snprintf(buf, sizeof(buf), "  %s = (none)", mi->gcdb_name);
            break;
        }
        draw_text(fb, 100, sy + i * 18, buf, COL_MAPPED, 1);
    }
}

/* ================================================================
 * State: review
 * ================================================================ */

/* Helper: redo the currently selected mapping row */
static void review_redo_selected(App *app)
{
    if (app->review_sel >= 0 && app->review_sel < NUM_MAPPINGS) {
        app->redo_single = app->review_sel;
        app->cur_map = app->review_sel;
        app->mappings[app->cur_map].mapped_type = MAP_NONE;
        app->state = STATE_MAPPING;
        drain_nav_events(app);
    }
}

/* Helper: start mapping all over */
static void review_restart(App *app)
{
    init_mappings(app->mappings);
    app->cur_map = 0;
    app->redo_single = -1;
    app->state = STATE_MAPPING;
    drain_nav_events(app);
}

/* Helper: go to directory browser to save */
static void review_save(App *app)
{
    browser_load(&app->browser, "/mnt");
    app->state = STATE_BROWSE;
    drain_nav_events(app);
}

static void update_review(App *app)
{
    int dy, dx, btn_a, btn_b, btn_start;
    int got_ctrl = read_nav_input(app, &dy, &dx, &btn_a, &btn_b, &btn_start);

    /* Keyboard input */
    int key = read_keyboard(app);
    if (key == KEY_UP)    dy = -1;
    if (key == KEY_DOWN)  dy = 1;
    if (key == KEY_RIGHT) dx = 1;
    if (key == KEY_1)     { review_redo_selected(app); return; }
    if (key == KEY_2)     { review_save(app); return; }
    if (key == KEY_3)     { review_restart(app); return; }
    if (key == KEY_4) {
        init_mappings(app->mappings);
        app->sel_ctrl = -1;
        app->thec64_nav_idx = -1;
        app->state = STATE_DETECT;
        app->save_path[0] = '\0';
        return;
    }
    if (key == KEY_Q || key == KEY_ESC) { app->state = STATE_EXIT; return; }

    if (!got_ctrl && !key)
        return;

    /* Vertical navigation */
    if (dy) {
        app->review_sel += dy;
        if (app->review_sel < 0) app->review_sel = 0;
        if (app->review_sel >= REVIEW_TOTAL_ITEMS)
            app->review_sel = REVIEW_TOTAL_ITEMS - 1;
    }

    /* Right on a mapping row (0..9) = redo that mapping */
    if (dx > 0 && app->review_sel >= 0 && app->review_sel < NUM_MAPPINGS) {
        review_redo_selected(app);
        return;
    }

    /* Confirm on action rows or mapping rows */
    if (btn_a || key == KEY_ENTER || key == KEY_SPACE) {
        if (app->review_sel >= 0 && app->review_sel < NUM_MAPPINGS) {
            /* selecting a mapping row = redo it */
            review_redo_selected(app);
            return;
        }
        if (app->review_sel == REVIEW_ACTION_SAVE) {
            review_save(app);
            return;
        }
        if (app->review_sel == REVIEW_ACTION_RESTART) {
            review_restart(app);
            return;
        }
        if (app->review_sel == REVIEW_ACTION_ANOTHER) {
            init_mappings(app->mappings);
            app->sel_ctrl = -1;
            app->thec64_nav_idx = -1;
            app->state = STATE_DETECT;
            app->save_path[0] = '\0';
            return;
        }
        if (app->review_sel == REVIEW_ACTION_QUIT) {
            app->state = STATE_EXIT;
            return;
        }
    }

    /* Shortcut buttons still work regardless of cursor position */
    if (btn_b) {
        if (app->review_sel >= 0 && app->review_sel < NUM_MAPPINGS) {
            review_redo_selected(app);
            return;
        }
    }
    if (btn_start) {
        review_save(app);
        return;
    }
}

static void render_review(App *app)
{
    Framebuffer *fb = &app->fb;
    int cx = fb->width / 2;
    char buf[256];

    /* Header */
    draw_rect(fb, 0, 0, fb->width, 36, COL_HEADER_BG);
    draw_text(fb, 16, 10, "Review Mappings", COL_TEXT_TITLE, 1);

    int y = 50;

    /* Check for duplicate assignments */
    int has_dupes = 0;
    for (int i = 0; i < NUM_MAPPINGS && !has_dupes; i++) {
        if (app->mappings[i].mapped_type == MAP_NONE) continue;
        for (int j = i + 1; j < NUM_MAPPINGS; j++) {
            if (app->mappings[j].mapped_type == app->mappings[i].mapped_type &&
                app->mappings[j].mapped_index == app->mappings[i].mapped_index &&
                (app->mappings[i].mapped_type != MAP_HAT ||
                 app->mappings[j].hat_mask == app->mappings[i].hat_mask)) {
                has_dupes = 1;
                break;
            }
        }
    }

    /* Column headers */
    draw_text(fb, 60, y, "THE64 Input", COL_TEXT_DIM, 1);
    draw_text(fb, 260, y, "Mapped To", COL_TEXT_DIM, 1);
    draw_text(fb, 460, y, "gamecontrollerdb", COL_TEXT_DIM, 1);
    if (has_dupes)
        draw_text(fb, 660, y, "Duplicate Assignment", COL_TEXT_DIM, 1);

    y += 24;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 8;

    for (int i = 0; i < NUM_MAPPINGS; i++) {
        MappingEntry *m = &app->mappings[i];
        int hl = (i == app->review_sel);

        if (hl)
            draw_rect(fb, 50, y - 2, fb->width - 100, 22, COL_SELECTED);

        draw_text(fb, 60, y, m->the64_label, hl ? COL_TEXT_TITLE : COL_TEXT, 1);

        switch (m->mapped_type) {
        case MAP_BUTTON:
            snprintf(buf, sizeof(buf), "Button %d", m->mapped_index);
            break;
        case MAP_AXIS:
            snprintf(buf, sizeof(buf), "Axis %d", m->mapped_index);
            break;
        case MAP_HAT:
            snprintf(buf, sizeof(buf), "Hat %d.%d", m->mapped_index,
                     m->hat_mask);
            break;
        default:
            snprintf(buf, sizeof(buf), "(none)");
            break;
        }
        draw_text(fb, 260, y, buf, hl ? COL_TEXT_TITLE : COL_TEXT, 1);

        snprintf(buf, sizeof(buf), "%s:", m->gcdb_name);
        switch (m->mapped_type) {
        case MAP_BUTTON:
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     "b%d", m->mapped_index);
            break;
        case MAP_AXIS:
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     "a%d", m->mapped_index);
            break;
        case MAP_HAT:
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     "h%d.%d", m->mapped_index, m->hat_mask);
            break;
        default:
            break;
        }
        draw_text(fb, 460, y, buf, COL_MAPPED, 1);

        /* Show duplicate assignments for this row */
        if (has_dupes && m->mapped_type != MAP_NONE) {
            char dups[256] = "";
            for (int j = 0; j < NUM_MAPPINGS; j++) {
                if (j == i) continue;
                if (app->mappings[j].mapped_type == m->mapped_type &&
                    app->mappings[j].mapped_index == m->mapped_index &&
                    (m->mapped_type != MAP_HAT ||
                     app->mappings[j].hat_mask == m->hat_mask)) {
                    if (dups[0] != '\0')
                        strncat(dups, ", ", sizeof(dups) - strlen(dups) - 1);
                    strncat(dups, app->mappings[j].the64_label,
                            sizeof(dups) - strlen(dups) - 1);
                }
            }
            if (dups[0] != '\0')
                draw_text(fb, 660, y, dups, COL_ERROR, 1);
        }

        y += 24;
    }

    /* Action buttons */
    y += 12;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 10;

    {
        struct { int idx; const char *label; const char *key; uint32_t col; } actions[] = {
            { REVIEW_ACTION_SAVE,    "Save to File",          "2", COL_SUCCESS },
            { REVIEW_ACTION_RESTART, "Start Over",            "3", COL_HIGHLIGHT },
            { REVIEW_ACTION_ANOTHER, "Map Another Controller","4", COL_TEXT },
            { REVIEW_ACTION_QUIT,    "Quit",                  "Q", COL_ERROR },
        };
        for (int i = 0; i < 4; i++) {
            int hl = (app->review_sel == actions[i].idx);
            if (hl)
                draw_rect(fb, 50, y - 2, fb->width - 100, 22, COL_SELECTED);
            snprintf(buf, sizeof(buf), "[%s] %s", actions[i].key, actions[i].label);
            draw_text(fb, 70, y, buf,
                      hl ? COL_TEXT_TITLE : actions[i].col, 1);
            y += 24;
        }
    }

    /* Help */
    y += 6;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 8;
    draw_text(fb, 60, y,
              "Keyboard: Arrows=Navigate  Right/Enter=Redo  1=Redo sel  "
              "2=Save  3=Restart  4=Another  Q=Quit",
              COL_TEXT_DIM, 1);
    y += 16;
    draw_text(fb, 60, y,
              "Controller: Stick=Navigate  Right=Redo  LFire/A=Confirm  "
              "B=Redo  Start=Save",
              COL_TEXT_DIM, 1);

    /* Saved confirmation */
    if (app->save_path[0] != '\0') {
        y += 16;
        snprintf(buf, sizeof(buf), "Saved to: %.200s", app->save_path);
        draw_text(fb, 60, y, buf, COL_SUCCESS, 1);
    }

    /* GUID and full string */
    y += 24;
    snprintf(buf, sizeof(buf), "GUID: %s",
             app->controllers[app->sel_ctrl].guid);
    draw_text(fb, 60, y, buf, COL_TEXT, 1);

    y += 24;
    /* wrap mapping string display */
    build_mapping_string(app, app->mapping_str, sizeof(app->mapping_str));
    int mlen = strlen(app->mapping_str);
    int chars_per_line = (fb->width - 120) / (FONT_W * 1);
    int off = 0;
    while (off < mlen) {
        int chunk = mlen - off;
        if (chunk > chars_per_line) chunk = chars_per_line;
        char line[512];
        memcpy(line, app->mapping_str + off, chunk);
        line[chunk] = '\0';
        draw_text(fb, 60, y, line, COL_TEXT_DIM, 1);
        y += 16;
        off += chunk;
    }
    (void)cx;
}

/* ================================================================
 * State: directory browser
 * ================================================================ */

static void update_browse(App *app)
{
    int dy, dx, btn_a, btn_b, btn_start;
    int got_ctrl = read_nav_input(app, &dy, &dx, &btn_a, &btn_b, &btn_start);
    (void)dx;

    /* Keyboard input */
    int key = read_keyboard(app);
    if (key == KEY_UP)    dy = -1;
    if (key == KEY_DOWN)  dy = 1;
    if (key == KEY_ENTER) btn_a = 1;
    if (key == KEY_LEFT || key == KEY_BACKSPACE) btn_b = 1;
    if (key == KEY_Q || key == KEY_ESC) btn_start = 1;

    if (!got_ctrl && !key)
        return;

    DirBrowser *b = &app->browser;

    if (dy) {
        b->selected += dy;
        if (b->selected < 0) b->selected = 0;
        if (b->selected >= b->count) b->selected = b->count - 1;
        /* scroll */
        int visible = 18;
        if (b->selected < b->scroll) b->scroll = b->selected;
        if (b->selected >= b->scroll + visible) b->scroll = b->selected - visible + 1;
    }
    if (btn_a && b->count > 0) {
        DirEntry *e = &b->entries[b->selected];
        if (strcmp(e->name, "..") == 0) {
            /* go up */
            char *slash = strrchr(b->path, '/');
            if (slash && slash != b->path) {
                *slash = '\0';
            } else {
                strcpy(b->path, "/");
            }
            browser_load(b, b->path);
        } else if (e->is_dir) {
            char newpath[MAX_PATH_LEN];
            if (strcmp(b->path, "/") == 0)
                snprintf(newpath, sizeof(newpath), "/%.250s", e->name);
            else
                snprintf(newpath, sizeof(newpath), "%.250s/%.250s",
                         b->path, e->name);
            browser_load(b, newpath);
        } else if (!e->is_dir) {
            /* save to current directory */
            Controller *c = &app->controllers[app->sel_ctrl];
            build_mapping_string(app, app->mapping_str, sizeof(app->mapping_str));

            char filepath[MAX_PATH_LEN];
            if (strcmp(b->path, "/") == 0)
                snprintf(filepath, sizeof(filepath), "/%.32s.txt", c->guid);
            else
                snprintf(filepath, sizeof(filepath), "%.470s/%.32s.txt",
                         b->path, c->guid);

            FILE *fp = fopen(filepath, "w");
            if (fp) {
                fprintf(fp, "%s\n", app->mapping_str);
                fclose(fp);
                snprintf(app->save_path, sizeof(app->save_path), "%s", filepath);
                app->state = STATE_REVIEW;
            }
            drain_nav_events(app);
        }
    }
    if (btn_b) {
        /* go up */
        char *slash = strrchr(app->browser.path, '/');
        if (slash && slash != app->browser.path) {
            *slash = '\0';
        } else {
            strcpy(app->browser.path, "/");
        }
        browser_load(&app->browser, app->browser.path);
    }
    if (btn_start) {
        /* same button that entered the save menu quits it */
        app->state = STATE_REVIEW;
        return;
    }
}

static void render_browse(App *app)
{
    Framebuffer *fb = &app->fb;
    DirBrowser *b = &app->browser;
    char buf[512];

    /* Header */
    draw_rect(fb, 0, 0, fb->width, 36, COL_HEADER_BG);
    draw_text(fb, 16, 10, "Select Export Directory", COL_TEXT_TITLE, 1);

    int y = 50;
    snprintf(buf, sizeof(buf), "Current: %s/", b->path);
    draw_text(fb, 60, y, buf, COL_TEXT, 1);

    y += 30;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 8;

    int visible = 18;
    for (int i = b->scroll; i < b->count && i < b->scroll + visible; i++) {
        int hl = (i == b->selected);
        if (hl)
            draw_rect(fb, 50, y - 2, fb->width - 100, 22, COL_SELECTED);

        if (b->entries[i].is_dir) {
            snprintf(buf, sizeof(buf), "[%s]", b->entries[i].name);
            draw_text(fb, 70, y, buf, hl ? COL_TEXT_TITLE : COL_TEXT, 1);
        } else {
            /* export action entry */
            draw_text(fb, 70, y, b->entries[i].name,
                      hl ? COL_TEXT_TITLE : COL_SUCCESS, 1);
        }
        y += 24;
    }

    /* Help */
    int hy = fb->height - 80;
    draw_rect(fb, 50, hy, fb->width - 100, 1, COL_BORDER);
    hy += 12;
    draw_text(fb, 60, hy,
              "Controller: Up/Down=Navigate  LFire/A=Select  "
              "B=Go up  Start=Quit",
              COL_TEXT_DIM, 1);
    hy += 16;
    draw_text(fb, 60, hy,
              "Keyboard: Arrows=Navigate  Enter=Select  "
              "Left/Bksp=Go up  Q/Esc=Quit",
              COL_TEXT_DIM, 1);

    hy += 20;
    snprintf(buf, sizeof(buf), "File will be saved as: %s/%s.txt",
             b->path, app->controllers[app->sel_ctrl].guid);
    draw_text(fb, 60, hy, buf, COL_TEXT_DIM, 1);
}

/* ================================================================
 * State: done
 * ================================================================ */

static void update_done(App *app)
{
    Controller *c = &app->controllers[app->sel_ctrl];
    struct input_event ev;
    while (read(c->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            app->state = STATE_EXIT;
            return;
        }
    }
    /* Also accept THEJOYSTICK button press to exit */
    if (app->thec64_nav_idx >= 0) {
        Controller *t = &app->controllers[app->thec64_nav_idx];
        while (read(t->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                app->state = STATE_EXIT;
                return;
            }
        }
    }
}

static void render_done(App *app)
{
    Framebuffer *fb = &app->fb;
    int cx = fb->width / 2;
    int y = 80;

    draw_text_centered(fb, cx, y, "Mapping Saved!", COL_SUCCESS, 3);

    y += 80;
    char buf[512];
    snprintf(buf, sizeof(buf), "File: %.500s", app->save_path);
    draw_text_centered(fb, cx, y, buf, COL_TEXT, 1);

    y += 40;
    draw_text(fb, 60, y, "Contents:", COL_TEXT_DIM, 1);
    y += 24;

    /* wrap mapping string */
    int mlen = strlen(app->mapping_str);
    int chars_per_line = (fb->width - 120) / (FONT_W * 1);
    int off = 0;
    while (off < mlen) {
        int chunk = mlen - off;
        if (chunk > chars_per_line) chunk = chars_per_line;
        char line[512];
        memcpy(line, app->mapping_str + off, chunk);
        line[chunk] = '\0';
        draw_text(fb, 60, y, line, COL_TEXT, 1);
        y += 18;
        off += chunk;
    }

    y += 30;
    draw_text_centered(fb, cx, y, "Press any button to exit", COL_TEXT_DIM, 2);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    App app;
    memset(&app, 0, sizeof(app));

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (fb_init(&app.fb) < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }

    app.state = STATE_DETECT;
    init_mappings(app.mappings);
    app.sel_ctrl = -1;
    app.thec64_nav_idx = -1;
    app.redo_single = -1;
    app.review_sel = 0;

    scan_controllers(&app);
    scan_keyboards(&app);
    app.last_scan = time_ms();

    /* Main loop */
    while (app.state != STATE_EXIT && !g_quit) {
        uint64_t now = time_ms();

        /* Update blink */
        if (now - app.blink_time > BLINK_MS) {
            app.blink = !app.blink;
            app.blink_time = now;
        }

        /* State update */
        switch (app.state) {
        case STATE_DETECT:  update_detect(&app);  break;
        case STATE_MAPPING: update_mapping(&app);  break;
        case STATE_REVIEW:  update_review(&app);   break;
        case STATE_BROWSE:  update_browse(&app);   break;
        case STATE_DONE:    update_done(&app);     break;
        default: break;
        }

        /* Render */
        fb_clear(&app.fb, COL_BG);

        switch (app.state) {
        case STATE_DETECT:  render_detect(&app);   break;
        case STATE_MAPPING: render_mapping(&app);  break;
        case STATE_REVIEW:  render_review(&app);   break;
        case STATE_BROWSE:  render_browse(&app);   break;
        case STATE_DONE:    render_done(&app);     break;
        default: break;
        }

        fb_flip(&app.fb);

        /* Cap frame rate */
        usleep(FRAME_MS * 1000);
    }

    /* Restore framebuffer to black */
    fb_clear(&app.fb, 0xFF000000);
    fb_flip(&app.fb);

    close_controllers(&app);
    close_keyboards(&app);
    fb_destroy(&app.fb);

    return 0;
}
