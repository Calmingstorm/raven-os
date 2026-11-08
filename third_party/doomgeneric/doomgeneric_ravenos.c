// RavenOS platform layer for doomgeneric: video through the gfx session, keys through the Doom key mapper,
// time through the runtime clock. Part of the RavenOS Doom port, distributed with doomgeneric under the
// GNU GPL version 2.

#include <stdint.h>

#include "doomgeneric.h"
#include "raven_clock.h"
#include "raven_doom_bridge.h"
#include "raven_doom_input.h"
#include "raven_doom_runtime.h"

static volatile uint32_t *framebuffer;
static uint32_t framebuffer_stride;
static uint32_t known_losses;

// Fail before anything loads when graphics can't work. The switch itself waits for the first frame, so Doom's
// startup log stays readable in the text console while the WAD loads.
void DG_Init(void) {
    const char *why;
    raven_input_reset();
    known_losses = raven_doom_key_losses();
    framebuffer = 0;
    if (raven_doom_video_check(&why) != 0) raven_doom_abort(why);
}

// doomgeneric has already scaled the 320x200 frame by two into DG_ScreenBuffer (640x400, 0x00RRGGBB), the
// exact size and pixel format of the gfx mode. The framebuffer is device memory with its own stride.
void DG_DrawFrame(void) {
    const pixel_t *source = DG_ScreenBuffer;
    if (!framebuffer) {
        const char *why;
        if (raven_doom_video_begin(&framebuffer, &framebuffer_stride, &why) != 0) raven_doom_abort(why);
    }
    for (uint32_t y = 0; y < DOOMGENERIC_RESY; y++) {
        volatile uint32_t *row = framebuffer + y * framebuffer_stride;
        for (uint32_t x = 0; x < DOOMGENERIC_RESX; x++) row[x] = source[y * DOOMGENERIC_RESX + x];
    }
    raven_input_frame();
}

void DG_SleepMs(uint32_t ms) {
    raven_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return raven_clock_ms();
}

int DG_GetKey(int *pressed, unsigned char *key) {
    RavenDoomKey physical;
    while (raven_doom_key_next(&physical)) raven_input_feed(physical.scancode, physical.pressed, physical.repeat);
    // The driver drops transitions while its queue is full, a long stall reading the disk for instance. Then bring
    // the mapper back to the keys physically down, so a lost release can't leave movement or fire held. The
    // snapshot also discards whatever was queued since the drain above: it already reflects those transitions.
    if (raven_doom_key_losses() != known_losses) {
        uint8_t held[32];
        known_losses = raven_doom_key_held(held);
        raven_input_resync(held);
    }
    return raven_input_next(pressed, key);
}

void DG_SetWindowTitle(const char *title) {
    (void)title;
}

// The runtime boundary calls these (see raven_doom_runtime.h). Doom's own loop never returns: I_Quit ends the
// session through raven_doom_exit and I_Error through raven_doom_abort (patched in i_system.c).
void raven_doom_port_main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    for (;;) doomgeneric_Tick();
}

void raven_doom_port_cleanup(void) {
    raven_doom_video_end();
    raven_input_reset();
    framebuffer = 0;
}
