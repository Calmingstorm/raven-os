/* The Doom port's whole input path with the real keyboard driver, bridge, key mapper and DG_GetKey: when the
 * driver's queue is full and it drops transitions, Doom still ends up with exactly the keys physically down.
 * Everything outside that path is stood in for. */
#include "kernel.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "raven_doom_input.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

uint8_t inb(uint16_t port) { (void)port; return 0; }
void wyrd_append(const char *a, const char *b, const char *c, const char *d, const char *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
uint32_t keyboard_irq_save(void) { return 0x200u; }
void keyboard_irq_restore(uint32_t flags) { (void)flags; }
int gfx_available(const char **why) { *why = 0; return 1; }
volatile uint32_t *gfx_begin(uint32_t *stride, const char **why) { (void)stride; *why = "not in this test"; return 0; }
void gfx_end(void) {}
uint32_t raven_clock_ms(void) { return 0; }
void raven_sleep_ms(uint32_t ms) { (void)ms; }
_Noreturn void raven_doom_abort(const char *reason) { fprintf(stderr, "FAIL: session aborted: %s\n", reason); exit(1); }
_Noreturn void raven_doom_exit(int status) { exit(status); }
void doomgeneric_Create(int argc, char **argv) { (void)argc; (void)argv; }
void doomgeneric_Tick(void) {}
pixel_t *DG_ScreenBuffer;

static unsigned char down[256];   /* what Doom has been told */

static void feed(uint8_t byte) { keyboard_feed_scancode(byte); }
static void ext(uint8_t byte) { feed(0xe0); feed(byte); }

/* Doom's loop: each frame takes every event DG_GetKey has, up to the mapper's per-frame limit. */
static void frames(unsigned n) {
    for (unsigned f = 0; f < n; f++) {
        int pressed;
        unsigned char key;
        while (DG_GetKey(&pressed, &key)) down[key] = (unsigned char)pressed;
        raven_input_frame();
    }
}

/* A main-loop stall while a held Up key repeats: the driver's queue fills up. */
static void fill_queue(void) {
    for (unsigned i = 0; i < KEYBUF_CAP; i++) ext(0x48);
}

int main(void) {
    keyboard_init();
    DG_Init();

    /* Fire and Up held; the queue fills; Ctrl's release is dropped. */
    feed(0x1d); ext(0x48);
    frames(1);
    CHECK(down[KEY_FIRE] && down[KEY_UPARROW]);
    uint32_t dropped = keyboard_dropped_events();
    fill_queue();
    feed(0x9d);
    CHECK(keyboard_dropped_events() > dropped);
    frames(100);
    CHECK(!down[KEY_FIRE]);
    CHECK(down[KEY_UPARROW]);

    /* A dropped press is recovered too, and its later release still arrives. */
    fill_queue();
    ext(0x4d);
    frames(100);
    CHECK(down[KEY_RIGHTARROW]);
    ext(0xcd);
    frames(2);
    CHECK(!down[KEY_RIGHTARROW]);

    /* Merged modifiers through a recovery: with left Shift held, right Shift's press and left Shift's release are
     * both dropped. Run stays held, because right Shift is physically down, until that is released. */
    feed(0x2a);
    frames(1);
    CHECK(down[KEY_RSHIFT]);
    fill_queue();
    feed(0x36); feed(0xaa);
    frames(100);
    CHECK(down[KEY_RSHIFT]);
    feed(0xb6);
    frames(2);
    CHECK(!down[KEY_RSHIFT]);

    /* With every key up, nothing is left held. */
    ext(0xc8);
    frames(2);
    for (unsigned k = 0; k < 256; k++) CHECK(!down[k]);

    printf("doom-key-loss-test: dropped releases and presses recovered through the real driver (%u events dropped)\n",
           keyboard_dropped_events());
    return 0;
}
