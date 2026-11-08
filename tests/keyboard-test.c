#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t port_byte;
uint8_t inb(uint16_t port) { if (port != 0x60) abort(); return port_byte; }
void wyrd_append(const char *a, const char *b, const char *c, const char *d, const char *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
/* The driver's interrupt-flag seam: the snapshot must read with interrupts off, and turn them back on. */
static int irqs_off, snapshots;
uint32_t keyboard_irq_save(void) { CHECK(!irqs_off); irqs_off = 1; return 0x200u; }
void keyboard_irq_restore(uint32_t flags) { CHECK(irqs_off && flags == 0x200u); irqs_off = 0; snapshots++; }
static int held_bit(const uint8_t held[32], unsigned slot) { return (held[slot / 8] >> (slot % 8)) & 1; }

static void feed(uint8_t b) { keyboard_feed_scancode(b); }
static void ext(uint8_t b) { feed(0xe0); feed(b); }
static void expect(uint16_t sc, KeyCode code, char ascii, uint8_t mods, uint8_t down) {
    KeyEvent e;
    CHECK(keyboard_pop(&e));
    CHECK(e.scancode == sc); CHECK(e.code == code); CHECK(e.ascii == ascii);
    CHECK(e.modifiers == mods); CHECK(e.pressed == down);
}
static void empty(void) { KeyEvent e; CHECK(!keyboard_pop(&e)); }

/* No consumer runs while an emulator injects a paste. Preserve Enter as well
 * as every make/break, even with per-character Shift transitions. Repeat to
 * exercise head/tail wraparound rather than only a freshly initialized ring. */
static void burst_line(unsigned length, int shifted) {
    unsigned i;
    for (i = 0; i < length; ++i) {
        if (shifted) feed(0x2a);
        feed(0x2d); feed(0xad);
        if (shifted) feed(0xaa);
    }
    feed(0x1c); feed(0x9c);
    for (i = 0; i < length; ++i) {
        if (shifted) expect(0x2a,KEY_SHIFT,0,KEY_MOD_SHIFT,1);
        expect(0x2d,KEY_CHARACTER,shifted ? 'X' : 'x',shifted ? KEY_MOD_SHIFT : 0,1);
        expect(0x2d,KEY_CHARACTER,shifted ? 'X' : 'x',shifted ? KEY_MOD_SHIFT : 0,0);
        if (shifted) expect(0x2a,KEY_SHIFT,0,0,0);
    }
    expect(0x1c,KEY_ENTER,0,0,1); expect(0x1c,KEY_ENTER,0,0,0);
    empty();
}

int main(void) {
    static const struct { uint8_t sc; KeyCode code; } nav[] = {
        {0x47,KEY_HOME},{0x48,KEY_UP},{0x49,KEY_PAGEUP},{0x4b,KEY_LEFT},
        {0x4d,KEY_RIGHT},{0x4f,KEY_END},{0x50,KEY_DOWN},{0x51,KEY_PAGEDOWN},
        {0x53,KEY_DELETE}
    };
    unsigned i;
    keyboard_init();
    ext(0x1c); ext(0x9c);
    expect(0xe01c,KEY_ENTER,0,0,1); expect(0xe01c,KEY_ENTER,0,0,0);
    ext(0x35); ext(0xb5);
    expect(0xe035,KEY_CHARACTER,'/',0,1); expect(0xe035,KEY_CHARACTER,'/',0,0);
    feed(0x2a); ext(0x35); ext(0xb5); feed(0xaa);
    expect(0x2a,KEY_SHIFT,0,KEY_MOD_SHIFT,1);
    expect(0xe035,KEY_CHARACTER,'/',KEY_MOD_SHIFT,1);
    expect(0xe035,KEY_CHARACTER,'/',KEY_MOD_SHIFT,0);
    expect(0x2a,KEY_SHIFT,0,0,0);
    for (i=0; i<sizeof(nav)/sizeof(nav[0]); ++i) {
        ext(nav[i].sc); ext(nav[i].sc | 0x80);
        expect(0xe000 | nav[i].sc,nav[i].code,0,0,1);
        expect(0xe000 | nav[i].sc,nav[i].code,0,0,0);
    }
    feed(0x2a); feed(0x36); feed(0xaa); feed(0x01);
    expect(0x2a,KEY_SHIFT,0,KEY_MOD_SHIFT,1);
    expect(0x36,KEY_SHIFT,0,KEY_MOD_SHIFT,1);
    expect(0x2a,KEY_SHIFT,0,KEY_MOD_SHIFT,0);
    expect(0x01,KEY_ESCAPE,0,KEY_MOD_SHIFT,1);
    feed(0xb6); expect(0x36,KEY_SHIFT,0,0,0);
    feed(0x1d); ext(0x1d); feed(0x9d); feed(0x16);
    expect(0x1d,KEY_CTRL,0,KEY_MOD_CTRL,1);
    expect(0xe01d,KEY_CTRL,0,KEY_MOD_CTRL,1);
    expect(0x1d,KEY_CTRL,0,KEY_MOD_CTRL,0);
    expect(0x16,KEY_CHARACTER,'u',KEY_MOD_CTRL,1);
    ext(0x9d); expect(0xe01d,KEY_CTRL,0,0,0);
    feed(0x38); ext(0x38); feed(0xb8);
    expect(0x38,KEY_ALT,0,KEY_MOD_ALT,1);
    expect(0xe038,KEY_ALT,0,KEY_MOD_ALT,1);
    expect(0x38,KEY_ALT,0,KEY_MOD_ALT,0);
    ext(0xb8); expect(0xe038,KEY_ALT,0,0,0);

    feed(0x3a); feed(0x3a); feed(0xba); feed(0x1e); feed(0x9e);
    expect(0x3a,KEY_CAPSLOCK,0,0,1); expect(0x3a,KEY_CAPSLOCK,0,0,1);
    expect(0x3a,KEY_CAPSLOCK,0,0,0);
    expect(0x1e,KEY_CHARACTER,'A',0,1); expect(0x1e,KEY_CHARACTER,'A',0,0);
    feed(0x2a); feed(0x1e); feed(0x02);
    expect(0x2a,KEY_SHIFT,0,KEY_MOD_SHIFT,1);
    expect(0x1e,KEY_CHARACTER,'a',KEY_MOD_SHIFT,1);
    expect(0x02,KEY_CHARACTER,'!',KEY_MOD_SHIFT,1);
    feed(0xaa); expect(0x2a,KEY_SHIFT,0,0,0);
    feed(0x3a); feed(0xba); expect(0x3a,KEY_CAPSLOCK,0,0,1); expect(0x3a,KEY_CAPSLOCK,0,0,0);
    { const uint8_t ignored[] = {0xe0,0x2a,0xe0,0x37,0xe0,0xb7,0xe0,0xaa,
                                  0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
      for (i=0; i<sizeof(ignored); ++i) feed(ignored[i]); }
    empty(); port_byte=0x1e; keyboard_on_irq(); expect(0x1e,KEY_CHARACTER,'a',0,1);
    /* Keys the shell has no use for still reach raw consumers (Doom's F10, Insert), as KEY_NONE. Controller
     * replies and overrun bytes are not keys. */
    { static const uint8_t fkeys[] = {0x3b,0x44,0x45,0x47,0x57,0x58};
      for (i=0; i<sizeof(fkeys); ++i) {
          feed(fkeys[i]); feed(fkeys[i] | 0x80);
          expect(fkeys[i],KEY_NONE,0,0,1); expect(fkeys[i],KEY_NONE,0,0,0);
      } }
    ext(0x52); ext(0xd2); expect(0xe052,KEY_NONE,0,0,1); expect(0xe052,KEY_NONE,0,0,0);
    ext(0x5b); ext(0xdb); expect(0xe05b,KEY_NONE,0,0,1); expect(0xe05b,KEY_NONE,0,0,0);
    feed(0x00); feed(0xfa); feed(0xfe); feed(0xee); feed(0xff); empty();
    keyboard_init();
    CHECK(keyboard_dropped_events() == 0);
    burst_line(150,0);
    burst_line(159,0);
    burst_line(159,1);
    burst_line(159,1);
    CHECK(keyboard_dropped_events() == 0);
    keyboard_init();
    for (i=0; i<KEYBUF_CAP-1; ++i) feed(0x1e);
    CHECK(keyboard_dropped_events() == 0); /* every usable slot fits */
    for (i=0; i<21; ++i) feed(0x1e);
    feed(0x2a); /* dropped event, but state must still advance */
    CHECK(keyboard_dropped_events() == 22);
    for (i=0; i<KEYBUF_CAP-1; ++i) expect(0x1e,KEY_CHARACTER,'a',0,1);
    empty(); feed(0x1e); expect(0x1e,KEY_CHARACTER,'A',KEY_MOD_SHIFT,1);
    feed(0xaa); expect(0x2a,KEY_SHIFT,0,0,0); empty();
    CHECK(keyboard_dropped_events() == 22); /* draining does not clear it */
    keyboard_init(); CHECK(keyboard_dropped_events() == 0);
    feed(0x2a); expect(0x2a,KEY_SHIFT,0,KEY_MOD_SHIFT,1);
    for (i=0; i<KEYBUF_CAP-1; ++i) feed(0x1e);
    feed(0xaa); /* a dropped modifier release must also update state */
    CHECK(keyboard_dropped_events() == 1);
    for (i=0; i<KEYBUF_CAP-1; ++i) expect(0x1e,KEY_CHARACTER,'A',KEY_MOD_SHIFT,1);
    empty(); feed(0x1e); expect(0x1e,KEY_CHARACTER,'a',0,1); empty();
    CHECK(keyboard_dropped_events() == 1);
    /* A make for a key that is already held is a typematic repeat; only a make after its release is new. */
    keyboard_init();
    {
        KeyEvent e;
        feed(0x1c); CHECK(keyboard_pop(&e) && e.code == KEY_ENTER && e.pressed && !e.repeat);
        feed(0x1c); CHECK(keyboard_pop(&e) && e.code == KEY_ENTER && e.pressed && e.repeat);
        feed(0x9c); CHECK(keyboard_pop(&e) && e.code == KEY_ENTER && !e.pressed && !e.repeat);
        feed(0x1c); CHECK(keyboard_pop(&e) && e.code == KEY_ENTER && e.pressed && !e.repeat);
        feed(0x9c); CHECK(keyboard_pop(&e) && !e.pressed);
        /* Extended keys are tracked apart from their twins: keypad Enter is not Enter. */
        ext(0x1c); CHECK(keyboard_pop(&e) && e.scancode == 0xe01c && e.pressed && !e.repeat);
        feed(0x1c); CHECK(keyboard_pop(&e) && e.scancode == 0x1c && e.pressed && !e.repeat);
        ext(0x1c); CHECK(keyboard_pop(&e) && e.scancode == 0xe01c && e.pressed && e.repeat);
        empty();
        keyboard_init();
        feed(0x1c); CHECK(keyboard_pop(&e) && !e.repeat); /* init forgets held keys */
    }
    /* The recovery snapshot is the driver's physical record, dropped events included, with their count, and it
     * empties the queue in the same guarded instant; later events queue normally. */
    keyboard_init();
    {
        uint8_t held[32];
        unsigned set = 0;
        KeyEvent e;
        feed(0x1d); ext(0x48);
        CHECK(keyboard_recovery_snapshot(held) == 0 && snapshots == 1 && !irqs_off);
        for (i = 0; i < 256; ++i) set += held_bit(held, i);
        CHECK(set == 2 && held_bit(held, 0x1d) && held_bit(held, 0x80 | 0x48));
        empty();                                       /* both makes are in the snapshot, not the queue */
        for (i = 0; i < KEYBUF_CAP; ++i) ext(0x48);   /* fill the queue with Up repeats */
        feed(0x9d);                                    /* Ctrl's release is dropped... */
        CHECK(keyboard_dropped_events() >= 1);
        CHECK(keyboard_recovery_snapshot(held) == keyboard_dropped_events());
        CHECK(!held_bit(held, 0x1d) && held_bit(held, 0x80 | 0x48));   /* ...but the record knows it's up */
        CHECK(!irqs_off);
        empty();
        feed(0x1e);                                    /* after the snapshot, events queue as before */
        CHECK(keyboard_pop(&e) && e.scancode == 0x1e && e.pressed);
        empty();
    }
    puts("keyboard tests passed");
    return 0;
}
