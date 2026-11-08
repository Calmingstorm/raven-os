#include "kernel.h"

/* Single producer (IRQ1), single consumer (main loop), on one CPU. One slot
 * stays empty; on overflow the newest event is dropped, not modifier state. */
static KeyEvent queue[KEYBUF_CAP];
static volatile uint32_t head, tail;
/* IRQ1 is the only writer; aligned 32-bit reads are atomic on this CPU.
 * Saturate rather than wrapping to zero and hiding a history of lost input. */
static volatile uint32_t dropped_events;
static uint8_t left_shift, right_shift, left_ctrl, right_ctrl, left_alt, right_alt;
static uint8_t caps_lock, caps_down, extended, pause_remaining;
/* Keys currently held, by (extended << 7 | scancode): a make for a held key is a typematic repeat. */
static uint8_t held[32];

static const char plain[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',
    [0x0c]='-',[0x0d]='=',[0x10]='q',[0x11]='w',[0x12]='e',
    [0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',
    [0x18]='o',[0x19]='p',[0x1a]='[',[0x1b]=']',[0x1e]='a',
    [0x1f]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
    [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
    [0x29]='`',[0x2b]='\\',[0x2c]='z',[0x2d]='x',[0x2e]='c',
    [0x2f]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',
    [0x34]='.',[0x35]='/',[0x37]='*',[0x39]=' '
};
static const char shifted[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0a]='(',[0x0b]=')',
    [0x0c]='_',[0x0d]='+',[0x10]='Q',[0x11]='W',[0x12]='E',
    [0x13]='R',[0x14]='T',[0x15]='Y',[0x16]='U',[0x17]='I',
    [0x18]='O',[0x19]='P',[0x1a]='{',[0x1b]='}',[0x1e]='A',
    [0x1f]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
    [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',
    [0x29]='~',[0x2b]='|',[0x2c]='Z',[0x2d]='X',[0x2e]='C',
    [0x2f]='V',[0x30]='B',[0x31]='N',[0x32]='M',[0x33]='<',
    [0x34]='>',[0x35]='?',[0x37]='*',[0x39]=' '
};

static void compiler_barrier(void) { __asm__ volatile ("" ::: "memory"); }

/* Private instruction seam, as in raven_clock.c: the host test replaces the interrupt-flag instructions. */
#ifndef KEYBOARD_TEST
static uint32_t keyboard_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static void keyboard_irq_restore(uint32_t flags) {
    if (flags & (1u << 9)) __asm__ volatile ("sti" ::: "memory");
}
#else
uint32_t keyboard_irq_save(void);
void keyboard_irq_restore(uint32_t flags);
#endif

uint32_t keyboard_held_snapshot(uint8_t out[32]) {
    uint32_t flags = keyboard_irq_save();
    for (unsigned i = 0; i < sizeof(held); i++) out[i] = held[i];
    uint32_t dropped = dropped_events;
    keyboard_irq_restore(flags);
    return dropped;
}

static void push_event(KeyEvent event) {
    uint32_t h = head, next = (h + 1) % KEYBUF_CAP;
    if (next == tail) {
        if (dropped_events != UINT32_MAX) ++dropped_events;
        return;
    }
    queue[h] = event;
    compiler_barrier();
    head = next;
}

void keyboard_init(void) {
    head = tail = 0;
    dropped_events = 0;
    left_shift = right_shift = left_ctrl = right_ctrl = left_alt = right_alt = 0;
    caps_lock = caps_down = extended = pause_remaining = 0;
    memset(held, 0, sizeof(held));
    wyrd_append("driver.keyboard", "init", "ps2", "ok", "irq1 line enabled");
}

uint32_t keyboard_dropped_events(void) { return dropped_events; }

int keyboard_pop(KeyEvent *out) {
    uint32_t t = tail;
    if (!out || t == head) return 0;
    compiler_barrier();
    *out = queue[t];
    compiler_barrier();
    tail = (t + 1) % KEYBUF_CAP;
    return 1;
}

void keyboard_feed_scancode(uint8_t byte) {
    uint8_t sc, down, ext, mods, shift;
    KeyEvent event;
    KeyCode code = KEY_NONE;
    char ascii = 0;

    /* Pause is the six-byte E1 1D 45 E1 9D C5 sequence. Do not let its
     * embedded control bytes alter modifier state. */
    if (pause_remaining) { --pause_remaining; return; }
    if (byte == 0xe1) { extended = 0; pause_remaining = 5; return; }
    if (byte == 0xe0) { extended = 1; return; }
    ext = extended; extended = 0;
    sc = byte & 0x7f;
    down = !(byte & 0x80);
    if (ext) {
        switch (sc) {
        case 0x1c: code = KEY_ENTER; break; /* keypad Enter */
        case 0x35: code = KEY_CHARACTER; ascii = '/'; break; /* keypad slash */
        case 0x1d: right_ctrl = down; code = KEY_CTRL; break;
        case 0x38: right_alt = down; code = KEY_ALT; break;
        case 0x47: code = KEY_HOME; break;
        case 0x48: code = KEY_UP; break;
        case 0x49: code = KEY_PAGEUP; break;
        case 0x4b: code = KEY_LEFT; break;
        case 0x4d: code = KEY_RIGHT; break;
        case 0x4f: code = KEY_END; break;
        case 0x50: code = KEY_DOWN; break;
        case 0x51: code = KEY_PAGEDOWN; break;
        case 0x53: code = KEY_DELETE; break;
        /* PrintScreen's two halves (the second is the fake Shift some keyboards also send around other extended
         * keys) aren't keys. Every other extended key is queued, KEY_NONE when the shell has no use for it
         * (Insert, the Windows keys), so raw consumers like Doom still see it. */
        case 0x2a: case 0x37: return;
        default: code = KEY_NONE; break;
        }
    } else {
        switch (sc) {
        case 0x2a: left_shift = down; code = KEY_SHIFT; break;
        case 0x36: right_shift = down; code = KEY_SHIFT; break;
        case 0x1d: left_ctrl = down; code = KEY_CTRL; break;
        case 0x38: left_alt = down; code = KEY_ALT; break;
        case 0x3a:
            if (down && !caps_down) caps_lock ^= 1;
            caps_down = down; code = KEY_CAPSLOCK; break;
        case 0x01: code = KEY_ESCAPE; break;
        case 0x0e: code = KEY_BACKSPACE; break;
        case 0x0f: code = KEY_TAB; break;
        case 0x1c: code = KEY_ENTER; break;
        default:
            /* Keys with no character (F1-F12, NumLock, the keypad) queue as KEY_NONE for raw consumers. Bytes past
             * the standard keys are controller replies and overruns, not keys. */
            if (!plain[sc]) {
                if (sc == 0 || sc > 0x58) return;
                code = KEY_NONE;
                break;
            }
            shift = !!(left_shift || right_shift);
            if (plain[sc] >= 'a' && plain[sc] <= 'z') shift ^= caps_lock;
            ascii = shift ? shifted[sc] : plain[sc];
            code = KEY_CHARACTER;
        }
    }
    mods = (left_shift || right_shift ? KEY_MOD_SHIFT : 0) |
           (left_ctrl || right_ctrl ? KEY_MOD_CTRL : 0) |
           (left_alt || right_alt ? KEY_MOD_ALT : 0);
    uint8_t slot = (uint8_t)((ext ? 0x80 : 0) | sc), bit = (uint8_t)(1u << (slot % 8));
    event.repeat = (uint8_t)(down && (held[slot / 8] & bit));
    if (down) held[slot / 8] |= bit;
    else held[slot / 8] &= (uint8_t)~bit;
    event.scancode = (uint16_t)(ext ? 0xe000 | sc : sc);
    event.code = code;
    event.ascii = ascii;
    event.modifiers = mods;
    event.pressed = down;
    push_event(event);
}

void keyboard_on_irq(void) { keyboard_feed_scancode(inb(0x60)); }
