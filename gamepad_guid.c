/*
 * gamepad_guid - Show GUIDs for connected game controllers
 *
 * Generates GUIDs in the same format as the64 binary on THEC64 Mini.
 * The GUID is a 32-character lowercase hex string constructed from the
 * Linux input device's bustype, vendor, product, and version fields,
 * each stored as a little-endian uint16_t with 2 bytes zero padding.
 *
 * Only depends on libc (uses raw Linux input ioctls).
 *
 * Cross-compile:
 *   arm-linux-gnueabihf-gcc -static -o gamepad_guid gamepad_guid.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define BITS_PER_LONG        (sizeof(long) * 8)
#define NBITS(x)             ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, array) ((array[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

/*
 * Check if a device is a joystick or gamepad.
 *
 * A device is considered a joystick/gamepad if it has:
 *   - EV_ABS with ABS_X and ABS_Y (analog axes), or
 *   - EV_KEY with buttons in the BTN_JOYSTICK (0x120-0x12f) or
 *     BTN_GAMEPAD (0x130-0x13f) range
 */
static int is_gamepad(int fd)
{
    unsigned long evbits[NBITS(EV_MAX)];
    unsigned long absbits[NBITS(ABS_MAX)];
    unsigned long keybits[NBITS(KEY_MAX)];
    int i;

    memset(evbits, 0, sizeof(evbits));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;

    /* Check for absolute axes (joystick analog sticks) */
    if (TEST_BIT(EV_ABS, evbits)) {
        memset(absbits, 0, sizeof(absbits));
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0) {
            if (TEST_BIT(ABS_X, absbits) && TEST_BIT(ABS_Y, absbits))
                return 1;
        }
    }

    /* Check for joystick/gamepad buttons */
    if (TEST_BIT(EV_KEY, evbits)) {
        memset(keybits, 0, sizeof(keybits));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
            for (i = BTN_JOYSTICK; i < BTN_JOYSTICK + 16; i++) {
                if (TEST_BIT(i, keybits))
                    return 1;
            }
            for (i = BTN_GAMEPAD; i < BTN_GAMEPAD + 16; i++) {
                if (TEST_BIT(i, keybits))
                    return 1;
            }
        }
    }

    return 0;
}

/*
 * Build a GUID string from an input_id, matching the format used by
 * the64 binary (and SDL2 on Linux):
 *
 *   Bytes 0-1:   bustype  (little-endian)
 *   Bytes 2-3:   0x0000
 *   Bytes 4-5:   vendor   (little-endian)
 *   Bytes 6-7:   0x0000
 *   Bytes 8-9:   product  (little-endian)
 *   Bytes 10-11: 0x0000
 *   Bytes 12-13: version  (little-endian)
 *   Bytes 14-15: 0x0000
 *
 * Each byte is converted to two lowercase hex digits, producing a
 * 32-character string (plus null terminator).
 */
static void build_guid(const struct input_id *id, char *guid_str)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char guid[16];
    int i;

    guid[0]  = id->bustype & 0xFF;
    guid[1]  = (id->bustype >> 8) & 0xFF;
    guid[2]  = 0;
    guid[3]  = 0;
    guid[4]  = id->vendor & 0xFF;
    guid[5]  = (id->vendor >> 8) & 0xFF;
    guid[6]  = 0;
    guid[7]  = 0;
    guid[8]  = id->product & 0xFF;
    guid[9]  = (id->product >> 8) & 0xFF;
    guid[10] = 0;
    guid[11] = 0;
    guid[12] = id->version & 0xFF;
    guid[13] = (id->version >> 8) & 0xFF;
    guid[14] = 0;
    guid[15] = 0;

    for (i = 0; i < 16; i++) {
        guid_str[i * 2]     = hex[guid[i] >> 4];
        guid_str[i * 2 + 1] = hex[guid[i] & 0x0F];
    }
    guid_str[32] = '\0';
}

int main(void)
{
    DIR *dir;
    struct dirent *entry;
    char path[512];
    struct input_id id;
    char name[256];
    char guid_str[33];
    int fd;
    int found = 0;

    dir = opendir("/dev/input");
    if (!dir) {
        perror("Cannot open /dev/input");
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        /* Only process event devices, same as the64 binary */
        if (strlen(entry->d_name) <= 5)
            continue;
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        if (!is_gamepad(fd)) {
            close(fd);
            continue;
        }

        if (ioctl(fd, EVIOCGID, &id) < 0) {
            close(fd);
            continue;
        }

        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
            strcpy(name, "Unknown");

        build_guid(&id, guid_str);

        printf("%s,%s,%s\n", guid_str, name, path);
        found++;

        close(fd);
    }

    closedir(dir);

    if (!found)
        printf("No game controllers found.\n");

    return 0;
}
