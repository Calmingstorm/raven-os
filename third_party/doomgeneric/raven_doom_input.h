/* RavenOS key input for doomgeneric: physical key transitions in, Doom key events out.
 *
 * Keys are mapped by scancode, never by shifted ASCII, so a release always matches its press. Left and right
 * Shift, Ctrl and Alt are one Doom key each: pressed while either is down. Typematic repeats are ignored.
 * Delivery is bounded: at most RAVEN_INPUT_PER_FRAME events between raven_input_frame() calls, because the
 * engine's event ring holds 64 entries and doesn't check for overflow. If the transition queue itself
 * overflows, ordering for that burst is given up and the engine is brought back to the keys actually held,
 * so movement and fire can never stay stuck down.
 */
#ifndef RAVEN_DOOM_INPUT_H
#define RAVEN_DOOM_INPUT_H

#include <stdint.h>

#define RAVEN_INPUT_PER_FRAME 32u
#define RAVEN_INPUT_QUEUE 128u

/* Forget everything: no keys held, nothing queued. */
void raven_input_reset(void);
/* One physical transition: scancode set 1 (0xE0xx when extended), make or break, typematic repeat flag. */
void raven_input_feed(uint16_t scancode, int pressed, int repeat);
/* The next event for the engine: 1 with *pressed and *doomkey filled, or 0 (none, or this frame's quota used). */
int raven_input_next(int *pressed, unsigned char *doomkey);
/* A new frame: the delivery quota starts again. */
void raven_input_frame(void);
/* The Doom key for a scancode, or 0 when the key has no use in Doom. */
unsigned char raven_input_doom_key(uint16_t scancode);

#endif
