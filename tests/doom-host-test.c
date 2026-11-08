/* Host test for the RavenOS Doom port: the vendored engine, doomgeneric_ravenos.c and the key mapper, built as a
 * 32-bit Linux program and played by script on the real Doom data disk.
 *
 * The kernel side is stood in for, following the round-3 contract: the gfx session is a framebuffer here with a
 * wider stride than the picture, the clock has the PIT's 10 ms resolution and only moves when Doom sleeps, the
 * session boundary is a setjmp, and files follow RavenOS's rules (only the two WAD paths open, read-only; every
 * other read is ENOENT and every write EROFS). exit() and abort() end the session, as RavenOS's libc does.
 *
 *   doom-host-test play <logical wad> [screenshot dir]   title, new game, move, fire, try to save, quit
 *   doom-host-test nographics <logical wad>              graphics unavailable: refused before anything loads
 *   doom-host-test badwad <file>                         a file that isn't a WAD: Doom's error ends the session
 */
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "d_event.h"
#include "raven_doom_bridge.h"
#include "raven_doom_runtime.h"
#include "raven_clock.h"
#include "raven_wad.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); __real_exit(1); } } while (0)

_Noreturn void __real_exit(int status);
FILE *__real_fopen(const char *path, const char *mode);
size_t __real_fread(void *dst, size_t size, size_t count, FILE *stream);

extern int messageToPrint;
extern char *messageString;
extern boolean menuactive;

/* ---- the session boundary ---- */
static jmp_buf session;
static int in_session;
static RavenDoomResultKind ended;
static int exit_status;
static char abort_reason[600];

_Noreturn void raven_doom_exit(int status) {
    CHECK(in_session);
    ended = RAVEN_DOOM_EXITED;
    exit_status = status;
    longjmp(session, 1);
}

_Noreturn void raven_doom_abort(const char *reason) {
    CHECK(in_session);
    ended = RAVEN_DOOM_ABORTED;
    snprintf(abort_reason, sizeof abort_reason, "%s", reason);
    longjmp(session, 1);
}

_Noreturn void __wrap_exit(int status) { raven_doom_exit(status); }
_Noreturn void __wrap_abort(void) { raven_doom_abort("abort"); }

/* ---- files: RavenOS's rules, and a record of which 256 KiB chunks of the disk Doom reads ---- */
#define CHUNK_BYTES 262144u
static const char *wad_path;
static FILE *wad_stream;
static unsigned char chunk_read[512];
static unsigned long long wad_bytes_read;

FILE *__wrap_fopen(const char *path, const char *mode) {
    if (strcmp(mode, "r") && strcmp(mode, "rb")) { errno = EROFS; return NULL; }
    if (strcmp(path, RAVEN_WAD_PATH) && strcmp(path, RAVEN_WAD_BASENAME)) { errno = ENOENT; return NULL; }
    FILE *stream = __real_fopen(wad_path, "rb");
    if (stream) wad_stream = stream;
    return stream;
}

size_t __wrap_fread(void *dst, size_t size, size_t count, FILE *stream) {
    if (stream == wad_stream && size && count) {
        long at = ftell(stream);
        unsigned long long end = (unsigned long long)at + (unsigned long long)size * count;
        for (unsigned long long chunk = (unsigned long long)at / CHUNK_BYTES; chunk * CHUNK_BYTES < end && chunk < 512; chunk++)
            chunk_read[chunk] = 1;
        wad_bytes_read += (unsigned long long)size * count;
    }
    return __real_fread(dst, size, count, stream);
}

static unsigned chunks_read(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < 512; i++) n += chunk_read[i];
    return n;
}

/* ---- the clock: PIT resolution (10 ms), moving only when Doom sleeps ---- */
static uint32_t now = 1000;
uint32_t raven_clock_ms(void) { return now; }
void raven_sleep_ms(uint32_t ms) {
    if (ms) now = (now + ms + 9) / 10 * 10;
}

/* ---- video: the gfx session as a framebuffer wider than the picture ---- */
#define STRIDE 704u
#define SENTINEL 0xdeadbeefu
static uint32_t screen[DOOMGENERIC_RESY][STRIDE];
static int video_available = 1;
static int checks, begins, ends;
static unsigned chunks_at_begin;

int raven_doom_video_check(const char **why) {
    checks++;
    if (!video_available) { *why = "no graphics here (test)"; return -1; }
    return 0;
}

int raven_doom_video_begin(volatile uint32_t **framebuffer, uint32_t *stride_pixels, const char **why) {
    (void)why;
    CHECK(video_available);
    begins++;
    chunks_at_begin = chunks_read();
    for (unsigned y = 0; y < DOOMGENERIC_RESY; y++)
        for (unsigned x = 0; x < STRIDE; x++) screen[y][x] = x < DOOMGENERIC_RESX ? 0 : SENTINEL;
    *framebuffer = &screen[0][0];
    *stride_pixels = STRIDE;
    return 0;
}

void raven_doom_video_end(void) { ends++; }

static unsigned distinct_colors(void) {
    static uint32_t seen[4096];
    unsigned count = 0;
    for (unsigned y = 0; y < DOOMGENERIC_RESY; y++) {
        for (unsigned x = 0; x < DOOMGENERIC_RESX; x++) {
            uint32_t c = screen[y][x] & 0xffffff, i;
            for (i = 0; i < count && seen[i] != c; i++) {}
            if (i == count && count < 4096) seen[count++] = c;
        }
        for (unsigned x = DOOMGENERIC_RESX; x < STRIDE; x++) CHECK(screen[y][x] == SENTINEL);
    }
    return count;
}

static const char *shots;
static void screenshot(const char *name) {
    if (!shots) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", shots, name);
    FILE *f = __real_fopen(path, "wb");
    CHECK(f);
    fprintf(f, "P6\n%d %d\n255\n", DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    for (unsigned y = 0; y < DOOMGENERIC_RESY; y++)
        for (unsigned x = 0; x < DOOMGENERIC_RESX; x++) {
            uint32_t c = screen[y][x];
            fputc((c >> 16) & 0xff, f); fputc((c >> 8) & 0xff, f); fputc(c & 0xff, f);
        }
    fclose(f);
}

/* ---- the script: key transitions and checks at clock times ---- */
typedef struct { uint32_t at; uint16_t scancode; uint8_t pressed; } Key;
static const Key *keys;
static unsigned key_count, next_key;
typedef void (*Step)(void);
typedef struct { uint32_t at; Step step; } Check;
static const Check *steps;
static unsigned step_count, next_step;

/* The scripted keyboard never drops a transition. */
uint32_t raven_doom_key_losses(void) { return 0; }
uint32_t raven_doom_key_held(uint8_t held[32]) { memset(held, 0, 32); return 0; }

int raven_doom_key_next(RavenDoomKey *out) {
    while (next_step < step_count && steps[next_step].at <= now) steps[next_step++].step();
    if (next_key >= key_count || keys[next_key].at > now) return 0;
    out->scancode = keys[next_key].scancode;
    out->pressed = keys[next_key].pressed;
    out->repeat = 0;
    next_key++;
    return 1;
}

#define TAP(t, sc) { (t), (sc), 1 }, { (t) + 100, (sc), 0 }
static const Key play_keys[] = {
    TAP(3000, 0x01),                         /* title: any key opens the menu */
    TAP(3500, 0x1c), TAP(4000, 0x1c), TAP(4500, 0x1c),   /* New Game, episode 1, default skill */
    { 9000, 0xe048, 1 }, { 10000, 0xe048, 0 },  /* hold up: walk forward */
    { 13000, 0x1d, 1 }, { 14000, 0x1d, 0 },     /* hold Ctrl: fire */
    TAP(16000, 0x3c),                        /* F2: save */
    TAP(17000, 0x39),                        /* dismiss the message */
    TAP(18000, 0x44),                        /* F10: quit */
    { 18500, 0x15, 1 },                      /* y: the session ends on the press */
};

static fixed_t x_at[4], y_at[4];
static int ammo_at[3];
static void title(void) {
    CHECK(begins == 1);
    CHECK(gamestate == GS_DEMOSCREEN || demoplayback);
    CHECK(distinct_colors() >= 32);
    screenshot("1-title");
}
static void menu_open(void) { CHECK(menuactive); screenshot("2-menu"); }
static void in_level(void) {
    CHECK(gamestate == GS_LEVEL && usergame && !menuactive);
    CHECK(distinct_colors() >= 32);
    screenshot("3-e1m1");
    x_at[0] = players[consoleplayer].mo->x; y_at[0] = players[consoleplayer].mo->y;
}
static void walking(void) {
    CHECK(players[consoleplayer].cmd.forwardmove > 0);
    x_at[1] = players[consoleplayer].mo->x; y_at[1] = players[consoleplayer].mo->y;
    CHECK(x_at[1] != x_at[0] || y_at[1] != y_at[0]);
}
/* A walking player slides for about 1.4 s after the key is released (friction 0.906 per tic, stopping below 4096). */
static void stopped_walking(void) {
    CHECK(players[consoleplayer].cmd.forwardmove == 0);   /* this tic's command: the key is up */
    CHECK(players[consoleplayer].mo->momx == 0 && players[consoleplayer].mo->momy == 0);
    x_at[2] = players[consoleplayer].mo->x; y_at[2] = players[consoleplayer].mo->y;
}
static void still_stopped(void) {
    x_at[3] = players[consoleplayer].mo->x; y_at[3] = players[consoleplayer].mo->y;
    CHECK(x_at[3] == x_at[2] && y_at[3] == y_at[2]);
    ammo_at[0] = players[consoleplayer].ammo[am_clip];
}
static void firing(void) { CHECK(players[consoleplayer].attackdown && (players[consoleplayer].cmd.buttons & BT_ATTACK)); }
static void stopped_firing(void) {
    CHECK(!players[consoleplayer].attackdown && !(players[consoleplayer].cmd.buttons & BT_ATTACK));
    ammo_at[1] = players[consoleplayer].ammo[am_clip];
    CHECK(ammo_at[1] < ammo_at[0]);
    screenshot("4-fired");
}
static void still_not_firing(void) {
    ammo_at[2] = players[consoleplayer].ammo[am_clip];
    CHECK(ammo_at[2] == ammo_at[1] && !players[consoleplayer].attackdown);
}
static void save_refused(void) {
    CHECK(messageToPrint && strstr(messageString, "saving and loading aren't available"));
    screenshot("5-no-saves");
}
static void quit_asked(void) { CHECK(messageToPrint); }
static const Check play_steps[] = {
    { 2500, title }, { 3300, menu_open }, { 8500, in_level }, { 9800, walking }, { 12000, stopped_walking },
    { 12800, still_stopped }, { 13800, firing }, { 14500, stopped_firing }, { 15800, still_not_firing },
    { 16500, save_refused }, { 18300, quit_asked },
};

static char *doom_argv[] = { "doom", "-iwad", RAVEN_WAD_PATH, NULL };

static void run(void) {
    in_session = 1;
    if (!setjmp(session)) {
        raven_doom_port_main(3, doom_argv);
        CHECK(!"raven_doom_port_main returned");
    }
    in_session = 0;
    raven_doom_port_cleanup();
    raven_doom_port_cleanup();   /* idempotent */
}

int main(int argc, char **argv) {
    CHECK(argc >= 3);
    wad_path = argv[2];
    if (!strcmp(argv[1], "play")) {
        shots = argc > 3 ? argv[3] : NULL;
        keys = play_keys; key_count = sizeof play_keys / sizeof *play_keys;
        steps = play_steps; step_count = sizeof play_steps / sizeof *play_steps;
        run();
        if (ended != RAVEN_DOOM_EXITED) fprintf(stderr, "session ended by abort: %s\n", abort_reason);
        CHECK(ended == RAVEN_DOOM_EXITED && exit_status == 0);
        CHECK(next_step == step_count && next_key == key_count);
        CHECK(checks == 1 && begins == 1 && ends == 2);
        printf("doom-host-test play: title, menu, new game, walk and stop, fire and stop, save refused, quit (exit 0)\n");
        printf("  disk: %u of 110 chunks read before the first frame, %u by the end (%.1f MB read in all)\n",
               chunks_at_begin, chunks_read(), wad_bytes_read / 1048576.0);
    } else if (!strcmp(argv[1], "nographics")) {
        video_available = 0;
        run();
        CHECK(ended == RAVEN_DOOM_ABORTED && !strcmp(abort_reason, "no graphics here (test)"));
        CHECK(checks == 1 && begins == 0 && chunks_read() == 0);
        printf("doom-host-test nographics: refused before reading the disk: %s\n", abort_reason);
    } else if (!strcmp(argv[1], "badwad")) {
        run();
        CHECK(ended == RAVEN_DOOM_ABORTED && strstr(abort_reason, "doesn't have IWAD or PWAD id"));
        CHECK(begins == 0);
        printf("doom-host-test badwad: the session ended with Doom's error: %s", abort_reason);
    } else {
        CHECK(!"unknown mode");
    }
    return 0;
}
