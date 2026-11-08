#include "kernel.h"
#include "raven_doom_bridge.h"

int raven_doom_video_check(const char **why) {
    return gfx_available(why) ? 0 : -1;
}

int raven_doom_video_begin(volatile uint32_t **framebuffer, uint32_t *stride_pixels, const char **why) {
    uint32_t stride;
    volatile uint32_t *fb = gfx_begin(&stride, why);
    if (!fb) return -1;
    *framebuffer = fb;
    *stride_pixels = stride;
    return 0;
}

void raven_doom_video_end(void) {
    gfx_end();
}

uint32_t raven_doom_key_losses(void) {
    return keyboard_dropped_events();
}

uint32_t raven_doom_key_held(uint8_t held[32]) {
    return keyboard_recovery_snapshot(held);
}

int raven_doom_key_next(RavenDoomKey *out) {
    KeyEvent key;
    if (!keyboard_pop(&key)) return 0;
    out->scancode = key.scancode;
    out->pressed = key.pressed;
    out->repeat = key.repeat;
    return 1;
}
