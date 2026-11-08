/* The kernel services the Doom port uses, in plain C types: the engine side includes the freestanding libc
 * headers and must never include kernel.h. Implemented in kernel/doom_bridge.c. Main-loop context only. */
#ifndef RAVEN_DOOM_BRIDGE_H
#define RAVEN_DOOM_BRIDGE_H

#include <stdint.h>

typedef struct {
    uint16_t scancode;   /* scancode set 1; extended keys are 0xE0xx */
    uint8_t pressed;     /* 1 make, 0 break */
    uint8_t repeat;      /* a make for a key already held */
} RavenDoomKey;

/* 0 when 640x400x32 graphics can be entered, else -1 with *why. Changes nothing on screen. */
int raven_doom_video_check(const char **why);
/* Enter 640x400x32 graphics: 0 with the framebuffer and its stride in pixels, or -1 with *why. */
int raven_doom_video_begin(volatile uint32_t **framebuffer, uint32_t *stride_pixels, const char **why);
/* Back to the text console. Idempotent; safe when graphics never started. */
void raven_doom_video_end(void);
/* The next physical key transition from the keyboard driver: 1 with *out filled, or 0 when none is queued. */
int raven_doom_key_next(RavenDoomKey *out);

#endif
