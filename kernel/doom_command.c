#include "kernel.h"
#include "raven_doom_runtime.h"
#include "raven_wad.h"
#include "../libc/include/errno.h"

extern void raven_doom_port_main(int argc, char **argv) __attribute__((weak));
extern void raven_doom_port_cleanup(void) __attribute__((weak));
static int engine_attempted;

void doom_command(void) {
    if (!raven_doom_port_main || !raven_doom_port_cleanup) {
        vga_write("Doom unavailable: engine port is not linked in this build.\n");
        return;
    }
    if (engine_attempted) {
        vga_write("Doom already initialized. Reboot before another launch.\n");
        return;
    }
    if (raven_doom_active()) { vga_write("Doom session is already active.\n"); return; }
    if (raven_wad_mount() != 0) {
        vga_write("Doom unavailable: primary-slave WAD validation failed, errno=");
        vga_write_dec((uint32_t)errno); vga_putc('\n');
        return;
    }
    /* 6 MiB zone plus 2 MiB headroom, before invoking any engine global init.
     * This is a preflight, not a reservation or a substitute for malloc OOM. */
    void *probe = kmalloc(8u * 1024u * 1024u);
    if (!probe) {
        raven_wad_unmount();
        vga_write("Doom unavailable: need 8 MiB contiguous virtual heap headroom.\n");
        return;
    }
    kfree(probe);
    char name[] = "doom", iwad[] = "-iwad", path[] = RAVEN_WAD_PATH;
    char *argv[] = { name, iwad, path, 0 };
    engine_attempted = 1;
    RavenDoomResult r = raven_doom_run(raven_doom_port_main,
                                       raven_doom_port_cleanup, 3, argv);
    if (r.kind == RAVEN_DOOM_UNAVAILABLE || r.kind == RAVEN_DOOM_BUSY) {
        engine_attempted = 0;
        raven_wad_unmount();
    }
    wyrd_append("doom", "session", "runtime",
        r.kind == RAVEN_DOOM_ABORTED ? "error" : "returned", "console ownership restored");
    vga_write("Doom session ended: ");
    if (r.kind == RAVEN_DOOM_ABORTED) vga_write("session error");
    else if (r.kind == RAVEN_DOOM_EXITED) vga_write("exit");
    else if (r.kind == RAVEN_DOOM_RETURNED) vga_write("returned");
    else vga_write("unavailable");
    vga_write(" (status=");
    if (r.status < 0) { vga_putc('-'); vga_write_dec(0u - (uint32_t)r.status); }
    else vga_write_dec((uint32_t)r.status);
    vga_write(").\n");
}
