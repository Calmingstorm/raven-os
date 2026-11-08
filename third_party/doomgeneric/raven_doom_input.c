// RavenOS key input for doomgeneric. See raven_doom_input.h for the contract.
// Part of the RavenOS Doom port, distributed with doomgeneric under the GNU GPL version 2.

#include "raven_doom_input.h"
#include "doomkeys.h"

typedef struct {
    unsigned char key;
    unsigned char pressed;
} Transition;

static Transition queue[RAVEN_INPUT_QUEUE];
static unsigned queue_head, queue_count, delivered;
static unsigned char overflowed;
static unsigned char scancode_down[256];   /* by (extended << 7 | scancode) */
static unsigned char holders[256];         /* physical keys holding each Doom key */
static unsigned char engine_down[256];     /* what the engine was last told */

/* Scancode set 1, non-extended. Letters are lowercase: cheats and menu confirmations compare ASCII. */
static const unsigned char plain_map[128] = {
    [0x01] = KEY_ESCAPE,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0a] = '9', [0x0b] = '0',
    [0x0c] = KEY_MINUS, [0x0d] = KEY_EQUALS, [0x0e] = KEY_BACKSPACE, [0x0f] = KEY_TAB,
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1a] = '[', [0x1b] = ']', [0x1c] = KEY_ENTER, [0x1d] = KEY_FIRE,
    [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`', [0x2a] = KEY_RSHIFT, [0x2b] = '\\',
    [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = KEY_STRAFE_L, [0x34] = KEY_STRAFE_R, [0x35] = '/',
    [0x36] = KEY_RSHIFT, [0x38] = KEY_RALT, [0x39] = KEY_USE,
    [0x3b] = KEY_F1, [0x3c] = KEY_F2, [0x3d] = KEY_F3, [0x3e] = KEY_F4, [0x3f] = KEY_F5,
    [0x40] = KEY_F6, [0x41] = KEY_F7, [0x42] = KEY_F8, [0x43] = KEY_F9, [0x44] = KEY_F10,
    [0x57] = KEY_F11, [0x58] = KEY_F12,
};

/* Extended (0xE0-prefixed) keys. */
static const unsigned char extended_map[128] = {
    [0x1c] = KEY_ENTER, [0x1d] = KEY_FIRE, [0x38] = KEY_RALT,
    [0x47] = KEY_HOME, [0x48] = KEY_UPARROW, [0x49] = KEY_PGUP,
    [0x4b] = KEY_LEFTARROW, [0x4d] = KEY_RIGHTARROW,
    [0x4f] = KEY_END, [0x50] = KEY_DOWNARROW, [0x51] = KEY_PGDN,
    [0x52] = KEY_INS, [0x53] = KEY_DEL,
};

static unsigned slot_of(uint16_t scancode) {
    return ((scancode >> 8) == 0xe0 ? 0x80u : 0u) | (scancode & 0x7fu);
}

unsigned char raven_input_doom_key(uint16_t scancode) {
    unsigned high = scancode >> 8;
    if (high != 0 && high != 0xe0) return 0;
    return high ? extended_map[scancode & 0x7f] : plain_map[scancode & 0x7f];
}

void raven_input_reset(void) {
    for (unsigned i = 0; i < 256; i++) scancode_down[i] = holders[i] = engine_down[i] = 0;
    queue_head = queue_count = delivered = 0;
    overflowed = 0;
}

static void enqueue(unsigned char key, unsigned char pressed) {
    if (overflowed) return;
    if (queue_count == RAVEN_INPUT_QUEUE) {
        /* Give up this burst's order; raven_input_next reconciles held state instead. */
        overflowed = 1;
        queue_head = queue_count = 0;
        return;
    }
    queue[(queue_head + queue_count) % RAVEN_INPUT_QUEUE] = (Transition){ key, pressed };
    queue_count++;
}

void raven_input_feed(uint16_t scancode, int pressed, int repeat) {
    unsigned char key = raven_input_doom_key(scancode);
    unsigned slot = slot_of(scancode);
    if (!key || repeat) return;
    if (pressed) {
        if (scancode_down[slot]) return;
        scancode_down[slot] = 1;
        if (holders[key]++ == 0) enqueue(key, 1);
    } else {
        /* A release for a key pressed before this session started has nothing to undo. */
        if (!scancode_down[slot]) return;
        scancode_down[slot] = 0;
        if (--holders[key] == 0) enqueue(key, 0);
    }
}

int raven_input_next(int *pressed, unsigned char *doomkey) {
    if (delivered >= RAVEN_INPUT_PER_FRAME) return 0;
    if (overflowed) {
        /* Bring the engine back to the physical state: releases first, so nothing stays held. */
        for (int pass = 0; pass < 2; pass++) {
            for (unsigned k = 1; k < 256; k++) {
                unsigned char held = holders[k] != 0;
                if (engine_down[k] == held || held != pass) continue;
                engine_down[k] = held;
                *pressed = held;
                *doomkey = (unsigned char)k;
                delivered++;
                return 1;
            }
        }
        overflowed = 0;
    }
    if (!queue_count) return 0;
    Transition t = queue[queue_head];
    queue_head = (queue_head + 1) % RAVEN_INPUT_QUEUE;
    queue_count--;
    engine_down[t.key] = t.pressed;
    *pressed = t.pressed;
    *doomkey = t.key;
    delivered++;
    return 1;
}

void raven_input_frame(void) {
    delivered = 0;
}
