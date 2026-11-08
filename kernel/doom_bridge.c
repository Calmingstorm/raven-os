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

int raven_doom_key_next(RavenDoomKey *out) {
    KeyEvent key;
    if (!keyboard_pop(&key)) return 0;
    out->scancode = key.scancode;
    out->pressed = key.pressed;
    out->repeat = key.repeat;
    return 1;
}
