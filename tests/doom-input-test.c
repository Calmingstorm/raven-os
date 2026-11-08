/* Host test for third_party/doomgeneric/raven_doom_input.c: the Doom key mapper. */
#include "raven_doom_input.h"
#include "doomkeys.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static void make(uint16_t sc) { raven_input_feed(sc, 1, 0); }
static void brk(uint16_t sc) { raven_input_feed(sc, 0, 0); }

static void expect(int pressed, unsigned char key) {
    int p;
    unsigned char k;
    CHECK(raven_input_next(&p, &k));
    if (p != pressed || k != key) {
        fprintf(stderr, "FAIL: expected %s %#x, got %s %#x\n", pressed ? "press" : "release", key, p ? "press" : "release", k);
        exit(1);
    }
}

static void none(void) {
    int p;
    unsigned char k;
    CHECK(!raven_input_next(&p, &k));
}

int main(void) {
    /* The map: letters lowercase, digits, controls, arrows (extended), and the default Doom bindings. */
    CHECK(raven_input_doom_key(0x1e) == 'a');
    CHECK(raven_input_doom_key(0x15) == 'y');
    CHECK(raven_input_doom_key(0x0b) == '0');
    CHECK(raven_input_doom_key(0x01) == KEY_ESCAPE);
    CHECK(raven_input_doom_key(0x1c) == KEY_ENTER && raven_input_doom_key(0xe01c) == KEY_ENTER);
    CHECK(raven_input_doom_key(0x0f) == KEY_TAB);
    CHECK(raven_input_doom_key(0x1d) == KEY_FIRE && raven_input_doom_key(0xe01d) == KEY_FIRE);
    CHECK(raven_input_doom_key(0x2d) == KEY_FIRE);   /* X: macOS keeps Ctrl+arrows */
    CHECK(raven_input_doom_key(0x39) == KEY_USE);
    CHECK(raven_input_doom_key(0x2a) == KEY_RSHIFT && raven_input_doom_key(0x36) == KEY_RSHIFT);
    CHECK(raven_input_doom_key(0x38) == KEY_RALT && raven_input_doom_key(0xe038) == KEY_RALT);
    CHECK(raven_input_doom_key(0xe048) == KEY_UPARROW && raven_input_doom_key(0xe050) == KEY_DOWNARROW);
    CHECK(raven_input_doom_key(0xe04b) == KEY_LEFTARROW && raven_input_doom_key(0xe04d) == KEY_RIGHTARROW);
    CHECK(raven_input_doom_key(0x33) == KEY_STRAFE_L && raven_input_doom_key(0x34) == KEY_STRAFE_R);
    CHECK(raven_input_doom_key(0x3b) == KEY_F1 && raven_input_doom_key(0x58) == KEY_F12);
    CHECK(raven_input_doom_key(0x48) == 0);          /* keypad 8 without E0: unused */
    CHECK(raven_input_doom_key(0xe12d) == 0);        /* not a set-1 prefix */

    /* A press and its release; a typematic repeat in between is not an event. */
    raven_input_reset();
    make(0x1e); raven_input_feed(0x1e, 1, 1); brk(0x1e);
    expect(1, 'a'); expect(0, 'a'); none();

    /* A release with no press in this session (held from before) is ignored, and so is a duplicate make. */
    brk(0x1f); make(0x1f); make(0x1f); brk(0x1f);
    expect(1, 's'); expect(0, 's'); none();

    /* Left and right modifiers are one Doom key: pressed by the first, released by the last. */
    make(0x2a); make(0x36); brk(0x2a);
    expect(1, KEY_RSHIFT); none();
    brk(0x36);
    expect(0, KEY_RSHIFT); none();
    make(0x1d); make(0xe01d); brk(0xe01d); brk(0x1d);
    expect(1, KEY_FIRE); expect(0, KEY_FIRE); none();
    /* Ctrl and X fire together the same way: fire stays down until the last of them is up. */
    make(0x1d); make(0x2d); brk(0x1d);
    expect(1, KEY_FIRE); none();
    brk(0x2d);
    expect(0, KEY_FIRE); none();

    /* Fire released while movement continues: each key's release is delivered on its own. */
    make(0xe048); make(0x1d); brk(0x1d);
    expect(1, KEY_UPARROW); expect(1, KEY_FIRE); expect(0, KEY_FIRE); none();
    brk(0xe048);
    expect(0, KEY_UPARROW); none();

    /* At most RAVEN_INPUT_PER_FRAME events per frame; the rest wait for the next frame, in order. */
    raven_input_reset();
    static const uint16_t letters[] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
        0x24, 0x25, 0x26, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
    };
    for (unsigned i = 0; i < 20; i++) { make(letters[i]); brk(letters[i]); }   /* 40 transitions */
    for (unsigned i = 0; i < RAVEN_INPUT_PER_FRAME; i++) {
        int p; unsigned char k;
        CHECK(raven_input_next(&p, &k));
        CHECK(k == raven_input_doom_key(letters[i / 2]) && p == !(i % 2));
    }
    none();
    raven_input_frame();
    for (unsigned i = RAVEN_INPUT_PER_FRAME; i < 40; i++) {
        int p; unsigned char k;
        CHECK(raven_input_next(&p, &k));
        CHECK(k == raven_input_doom_key(letters[i / 2]) && p == !(i % 2));
    }
    none();

    /* Queue overflow: the burst's order is given up, and the engine ends with exactly the keys held. Here
     * fire and up stay held through a flood of taps, and right was held then released inside the flood. */
    raven_input_reset();
    make(0xe04d);
    expect(1, KEY_RIGHTARROW);        /* the engine believes right is down */
    make(0x1d); make(0xe048);
    for (unsigned round = 0; round < 40; round++)
        for (unsigned i = 0; i < 4; i++) { make(letters[i]); brk(letters[i]); }
    brk(0xe04d);
    unsigned char down[256] = {0};
    down[KEY_RIGHTARROW] = 1;
    for (unsigned frame = 0; frame < 20; frame++) {
        int p; unsigned char k;
        while (raven_input_next(&p, &k)) down[k] = (unsigned char)p;
        raven_input_frame();
    }
    CHECK(down[KEY_FIRE] == 1 && down[KEY_UPARROW] == 1);
    CHECK(down[KEY_RIGHTARROW] == 0);
    for (unsigned i = 0; i < 4; i++) CHECK(down[raven_input_doom_key(letters[i])] == 0);
    none();

    /* After the reconciliation, ordinary ordered delivery resumes. */
    brk(0x1d); brk(0xe048);
    expect(0, KEY_FIRE); expect(0, KEY_UPARROW); none();

    /* Recovery from transitions the keyboard driver dropped: the mapper takes the physical record, releases first.
     * The engine believes fire is down; physically fire is up and right is down. */
    raven_input_reset();
    make(0x1d);
    expect(1, KEY_FIRE); none();
    {
        uint8_t held[32] = {0};
        held[(0x80 | 0x4d) / 8] |= (uint8_t)(1u << ((0x80 | 0x4d) % 8));   /* extended Right */
        raven_input_resync(held);
        expect(0, KEY_FIRE); expect(1, KEY_RIGHTARROW); none();
        raven_input_resync(held);                                          /* agreeing already: nothing */
        none();
        brk(0xe04d);                                                       /* the real release still counts */
        expect(0, KEY_RIGHTARROW); none();
    }

    puts("doom input tests passed");
    return 0;
}
