#include "kernel.h"

/* Graphics through the Bochs VBE display interface (BGA), which QEMU's std-vga, Bochs, VirtualBox and v86 all
 * provide. The framebuffer is BAR0 of the PCI display device, mapped through paging_map_mmio, never assumed. */

#define BGA_INDEX 0x01CE
#define BGA_DATA 0x01CF
enum {
    BGA_REG_ID = 0, BGA_REG_XRES = 1, BGA_REG_YRES = 2, BGA_REG_BPP = 3,
    BGA_REG_ENABLE = 4, BGA_REG_VIRT_WIDTH = 6, BGA_REG_VIDEO_MEMORY_64K = 0x0A
};
#define BGA_ID_MIN 0xB0C0
#define BGA_ID_MAX 0xB0C5
#define BGA_DISABLED 0x00
#define BGA_ENABLED 0x01
#define BGA_GETCAPS 0x02
#define BGA_LFB_ENABLED 0x40

#define GFX_WIDTH 640
#define GFX_HEIGHT 400
#define GFX_BPP 32

/* Plane 2 holds the text-mode font. On QEMU the linear framebuffer shares video memory with the VGA planes, so
 * graphics can overwrite it; v86 keeps them apart. Either way it is saved before and written back after, with
 * the DAC palette and the VGA register set. */
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF
#define VGA_DAC_READ_INDEX 0x3C7
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA 0x3C9
#define VGA_MISC_READ 0x3CC
#define VGA_MISC_WRITE 0x3C2
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_AC_INDEX 0x3C0
#define VGA_AC_READ 0x3C1
#define VGA_INSTAT 0x3DA
#define VGA_FONT_BYTES (256 * 32)
#define VGA_GLYPH_ROWS 16

/* The text mode's own register set. QEMU programs these for graphics when the Bochs interface is enabled and
 * doesn't put them back when it is disabled, so the console only returns if they are restored. */
typedef struct {
    uint8_t misc;
    uint8_t seq[5];
    uint8_t crtc[25];
    uint8_t gc[9];
    uint8_t ac[21];
} VgaRegisters;

/* The legacy VGA window (0xA0000, 64 KiB) is device memory, not identity-mapped RAM: mapped on first use. */
#define VGA_WINDOW_PHYS 0xA0000u
#define VGA_WINDOW_BYTES 0x10000u
static volatile uint8_t *vga_window;
static VgaRegisters saved_registers;
static uint8_t saved_font[VGA_FONT_BYTES];
static uint8_t saved_palette[256 * 3];
static uint32_t entries, refusals;

typedef struct {
    PciDevice pci;
    uint32_t bar_base, bar_size;
    uint16_t id, max_x, max_y, max_bpp;
    uint32_t video_memory;
    const char *problem;
} GfxProbe;

static void bga_write(uint16_t reg, uint16_t value) {
    outw(BGA_INDEX, reg);
    outw(BGA_DATA, value);
}

static uint16_t bga_read(uint16_t reg) {
    outw(BGA_INDEX, reg);
    return inw(BGA_DATA);
}

/* The Bochs VBE ports aren't tied to a PCI function, so the interface is only trusted on a known
 * Bochs-compatible controller that is the machine's only VGA controller: with two, the ports may belong to the
 * other one and the pattern would be written into the wrong card's memory. */
static int bochs_compatible(const PciDevice *d) {
    return (d->vendor == 0x1234 && d->device == 0x1111)     /* QEMU std-vga, Bochs, v86 */
        || (d->vendor == 0x80EE && d->device == 0xBEEF);    /* VirtualBox VGA */
}

static void probe(GfxProbe *p) {
    PciDevice second;
    memset(p, 0, sizeof(*p));
    if (!pci_find_class(0x03, 0x00, 0, &p->pci)) { p->problem = "no PCI VGA display device"; return; }
    if (pci_find_class(0x03, 0x00, 1, &second)) {
        p->problem = "more than one VGA controller";
        return;
    }
    if (!bochs_compatible(&p->pci)) { p->problem = "the display has no Bochs VBE interface"; return; }
    p->id = bga_read(BGA_REG_ID);
    if (p->id < BGA_ID_MIN || p->id > BGA_ID_MAX) { p->problem = "the display has no Bochs VBE interface"; return; }
    if (!pci_memory_bar(&p->pci, 0, &p->bar_base, &p->bar_size)) {
        p->problem = "display BAR0 is not a usable 32-bit memory BAR";
        return;
    }
    if (p->id >= 0xB0C2) {
        /* GETCAPS turns the resolution registers into maximums until it is cleared. */
        uint16_t enable = bga_read(BGA_REG_ENABLE);
        bga_write(BGA_REG_ENABLE, (uint16_t)(enable | BGA_GETCAPS));
        p->max_x = bga_read(BGA_REG_XRES);
        p->max_y = bga_read(BGA_REG_YRES);
        p->max_bpp = bga_read(BGA_REG_BPP);
        bga_write(BGA_REG_ENABLE, enable);
        if (p->max_x < GFX_WIDTH || p->max_y < GFX_HEIGHT || p->max_bpp < GFX_BPP) {
            p->problem = "the display can't do 640x400 at 32 bits";
            return;
        }
    }
    if (p->id >= 0xB0C4) p->video_memory = (uint32_t)bga_read(BGA_REG_VIDEO_MEMORY_64K) * 65536u;
    if (p->bar_size < (uint32_t)GFX_WIDTH * 4 * GFX_HEIGHT) p->problem = "the framebuffer aperture is too small";
}

static void registers_save(VgaRegisters *r) {
    r->misc = inb(VGA_MISC_READ);
    for (uint8_t i = 0; i < sizeof(r->seq); i++) { outb(VGA_SEQ_INDEX, i); r->seq[i] = inb(VGA_SEQ_DATA); }
    for (uint8_t i = 0; i < sizeof(r->crtc); i++) { outb(VGA_CRTC_INDEX, i); r->crtc[i] = inb(VGA_CRTC_DATA); }
    for (uint8_t i = 0; i < sizeof(r->gc); i++) { outb(VGA_GC_INDEX, i); r->gc[i] = inb(VGA_GC_DATA); }
    /* Reading 0x3DA resets the attribute controller to expect an index. An index without bit 5 hands the
     * palette to the CPU and blanks the screen, so bit 5 goes back on at the end. */
    for (uint8_t i = 0; i < sizeof(r->ac); i++) { inb(VGA_INSTAT); outb(VGA_AC_INDEX, i); r->ac[i] = inb(VGA_AC_READ); }
    inb(VGA_INSTAT);
    outb(VGA_AC_INDEX, 0x20);
}

static void registers_restore(const VgaRegisters *r) {
    outb(VGA_MISC_WRITE, r->misc);
    outb(VGA_SEQ_INDEX, 0); outb(VGA_SEQ_DATA, 0x01);   /* synchronous reset while the clocking changes */
    for (uint8_t i = 1; i < sizeof(r->seq); i++) { outb(VGA_SEQ_INDEX, i); outb(VGA_SEQ_DATA, r->seq[i]); }
    outb(VGA_SEQ_INDEX, 0); outb(VGA_SEQ_DATA, r->seq[0]);
    /* CRTC registers 0-7 are write-protected while bit 7 of register 0x11 is set. */
    outb(VGA_CRTC_INDEX, 0x11); outb(VGA_CRTC_DATA, (uint8_t)(r->crtc[0x11] & 0x7F));
    for (uint8_t i = 0; i < sizeof(r->crtc); i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, i == 0x11 ? (uint8_t)(r->crtc[i] & 0x7F) : r->crtc[i]);
    }
    outb(VGA_CRTC_INDEX, 0x11); outb(VGA_CRTC_DATA, r->crtc[0x11]);
    for (uint8_t i = 0; i < sizeof(r->gc); i++) { outb(VGA_GC_INDEX, i); outb(VGA_GC_DATA, r->gc[i]); }
    for (uint8_t i = 0; i < sizeof(r->ac); i++) { inb(VGA_INSTAT); outb(VGA_AC_INDEX, i); outb(VGA_AC_INDEX, r->ac[i]); }
    inb(VGA_INSTAT);
    outb(VGA_AC_INDEX, 0x20);
}

static void font_access(int save) {
    outb(VGA_SEQ_INDEX, 2); uint8_t seq2 = inb(VGA_SEQ_DATA);
    outb(VGA_SEQ_INDEX, 4); uint8_t seq4 = inb(VGA_SEQ_DATA);
    outb(VGA_GC_INDEX, 4); uint8_t gc4 = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX, 5); uint8_t gc5 = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX, 6); uint8_t gc6 = inb(VGA_GC_DATA);
    outb(VGA_SEQ_INDEX, 2); outb(VGA_SEQ_DATA, 0x04);  /* write plane 2 only */
    outb(VGA_SEQ_INDEX, 4); outb(VGA_SEQ_DATA, 0x07);  /* sequential addressing, extended memory */
    outb(VGA_GC_INDEX, 4); outb(VGA_GC_DATA, 0x02);    /* read plane 2 */
    outb(VGA_GC_INDEX, 5); outb(VGA_GC_DATA, 0x00);    /* write mode 0, odd/even off */
    outb(VGA_GC_INDEX, 6); outb(VGA_GC_DATA, 0x04);    /* window at 0xA0000, 64 KiB, no text addressing */
    for (uint32_t i = 0; i < VGA_FONT_BYTES; i++) {
        if (save) saved_font[i] = vga_window[i];
        else vga_window[i] = saved_font[i];
    }
    outb(VGA_SEQ_INDEX, 2); outb(VGA_SEQ_DATA, seq2);
    outb(VGA_SEQ_INDEX, 4); outb(VGA_SEQ_DATA, seq4);
    outb(VGA_GC_INDEX, 4); outb(VGA_GC_DATA, gc4);
    outb(VGA_GC_INDEX, 5); outb(VGA_GC_DATA, gc5);
    outb(VGA_GC_INDEX, 6); outb(VGA_GC_DATA, gc6);
}

static void palette_access(int save) {
    if (save) {
        outb(VGA_DAC_READ_INDEX, 0);
        for (uint32_t i = 0; i < sizeof(saved_palette); i++) saved_palette[i] = inb(VGA_DAC_DATA);
    } else {
        outb(VGA_DAC_WRITE_INDEX, 0);
        for (uint32_t i = 0; i < sizeof(saved_palette); i++) outb(VGA_DAC_DATA, saved_palette[i]);
    }
}

static void fill(volatile uint32_t *fb, uint32_t stride, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    for (uint32_t row = y; row < y + h && row < GFX_HEIGHT; row++)
        for (uint32_t col = x; col < x + w && col < GFX_WIDTH; col++) fb[row * stride + col] = rgb;
}

/* Text in graphics mode reuses the saved VGA font: 8x16 glyphs, scaled. */
static uint32_t draw_text(volatile uint32_t *fb, uint32_t stride, uint32_t x, uint32_t y, uint32_t scale,
                          const char *text, uint32_t rgb) {
    for (; *text; text++, x += 8 * scale) {
        const uint8_t *glyph = &saved_font[(uint8_t)*text * 32];
        for (uint32_t gy = 0; gy < VGA_GLYPH_ROWS; gy++)
            for (uint32_t gx = 0; gx < 8; gx++)
                if (glyph[gy] & (0x80 >> gx)) fill(fb, stride, x + gx * scale, y + gy * scale, scale, scale, rgb);
    }
    return x;
}

static void hex_text(char *out, uint32_t value, uint32_t digits) {
    for (uint32_t i = 0; i < digits; i++) out[i] = "0123456789ABCDEF"[(value >> (4 * (digits - 1 - i))) & 15];
    out[digits] = '\0';
}

/* Eight colour bars, a grey ramp and labels. check-web.mjs and gfx-qemu-test.py sample these exact pixels. */
static void draw_pattern(volatile uint32_t *fb, uint32_t stride, const GfxProbe *p) {
    static const uint32_t bars[8] = {
        0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000
    };
    fill(fb, stride, 0, 0, GFX_WIDTH, GFX_HEIGHT, 0x101418);
    for (uint32_t i = 0; i < 8; i++) fill(fb, stride, i * 80, 56, 80, 240, bars[i]);
    for (uint32_t x = 0; x < GFX_WIDTH; x++) fill(fb, stride, x, 304, 1, 40, (x * 255 / (GFX_WIDTH - 1)) * 0x010101u);
    draw_text(fb, stride, 8, 12, 2, "RavenOS 640x400x32", 0x55FFFF);
    char hex[9], line[64];
    char *at = line;
    const char *parts[] = { "PCI ", 0, ":", 0, "  BGA ", 0, "  any key returns" };
    for (uint32_t i = 0; i < 7; i++) {
        const char *part = parts[i];
        if (!part) {
            hex_text(hex, i == 1 ? p->pci.vendor : i == 3 ? p->pci.device : p->id, 4);
            part = hex;
        }
        while (*part && at < line + sizeof(line) - 1) *at++ = *part++;
    }
    *at = '\0';
    draw_text(fb, stride, 8, 360, 1, line, 0xC0C0C0);
}

/* Input queued before graphics appears doesn't count. This runs before the mode switch, not after the pattern
 * is drawn: on a slow machine a key pressed while the pattern draws is a real answer and must not be dropped. */
static void discard_queued_input(void) {
    KeyEvent key;
    char c;
    while (keyboard_pop(&key)) {}
    while (serial_read_char(&c)) {}
}

/* Only a new press of a non-modifier key (or a serial character) returns. Releases don't count, including the
 * release of the Enter that launched gfx, and neither do typematic repeats of a key held since before. */
static void wait_for_fresh_key(void) {
    KeyEvent key;
    char c;
    for (;;) {
        while (keyboard_pop(&key)) {
            if (key.pressed && !key.repeat && key.code != KEY_NONE && key.code != KEY_SHIFT &&
                key.code != KEY_CTRL && key.code != KEY_ALT && key.code != KEY_CAPSLOCK) return;
        }
        if (serial_read_char(&c)) return;
        __asm__ volatile ("hlt");
    }
}

static void back_to_text(void) {
    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    registers_restore(&saved_registers);
    font_access(0);
    palette_access(0);
    vga_hardware(1);
    vga_refresh();
}

static void refuse(const char *why) {
    refusals++;
    wyrd_append("gfx", "enter", "bga", "fail", why);
    vga_write("gfx: "); vga_write(why); vga_write("; staying in text mode\n");
}

/* A graphics session at 640x400x32 for any caller (gfx itself, Doom): the same device checks, register, font and
 * palette saving, and mapping as before. Returns the mapped framebuffer and its stride in pixels, or 0 with the
 * reason in *why and the text console untouched. gfx_end() restores the console; it is idempotent. */
static int session_active;
static GfxProbe session_probe;

int gfx_available(const char **why) {
    GfxProbe p;
    probe(&p);
    *why = p.problem;
    return !p.problem;
}

volatile uint32_t *gfx_begin(uint32_t *stride, const char **why) {
    *why = 0;
    if (session_active) { *why = "graphics are already in use"; return 0; }
    probe(&session_probe);
    if (session_probe.problem) { *why = session_probe.problem; return 0; }
    if (!vga_window) vga_window = (volatile uint8_t *)paging_map_mmio(VGA_WINDOW_PHYS, VGA_WINDOW_BYTES);
    if (!vga_window) { *why = "the VGA window could not be mapped"; return 0; }

    vga_hardware(0);
    pci_enable_memory(&session_probe.pci);
    registers_save(&saved_registers);
    font_access(1);
    palette_access(1);
    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    bga_write(BGA_REG_XRES, GFX_WIDTH);
    bga_write(BGA_REG_YRES, GFX_HEIGHT);
    bga_write(BGA_REG_BPP, GFX_BPP);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);
    if (bga_read(BGA_REG_XRES) != GFX_WIDTH || bga_read(BGA_REG_YRES) != GFX_HEIGHT || bga_read(BGA_REG_BPP) != GFX_BPP) {
        back_to_text();
        *why = "the display rejected 640x400x32";
        return 0;
    }
    uint32_t virtual_width = bga_read(BGA_REG_VIRT_WIDTH);
    if (virtual_width < GFX_WIDTH) virtual_width = GFX_WIDTH;
    uint32_t pitch = virtual_width * 4, span = pitch * GFX_HEIGHT;
    if (span > session_probe.bar_size || (session_probe.video_memory && span > session_probe.video_memory)) {
        back_to_text();
        *why = "the framebuffer doesn't fit the display's memory";
        return 0;
    }
    volatile uint32_t *fb = (volatile uint32_t *)paging_map_mmio(session_probe.bar_base, span);
    if (!fb) {
        back_to_text();
        *why = "the framebuffer could not be mapped";
        return 0;
    }
    session_active = 1;
    entries++;
    *stride = pitch / 4;
    return fb;
}

void gfx_end(void) {
    if (!session_active) return;
    session_active = 0;
    back_to_text();
}

void gfx_command(void) {
    uint32_t stride;
    const char *why;
    /* Input queued before graphics appears doesn't count (see discard_queued_input). */
    discard_queued_input();
    volatile uint32_t *fb = gfx_begin(&stride, &why);
    if (!fb) { refuse(why); return; }
    draw_pattern(fb, stride, &session_probe);
    wyrd_append("gfx", "enter", "bga", "ok", "640x400x32 test pattern");
    wait_for_fresh_key();
    gfx_end();
    wyrd_append("gfx", "exit", "bga", "ok", "text console restored");
    vga_write("gfx: back in text mode\n");
}

void gfx_render_info(void) {
    GfxProbe p;
    probe(&p);
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("graphics\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    char hex[9];
    if (p.pci.vendor) {
        vga_write("  pci:     "); hex_text(hex, p.pci.vendor, 4); vga_write(hex); vga_putc(':');
        hex_text(hex, p.pci.device, 4); vga_write(hex);
        vga_write(" at "); vga_write_dec(p.pci.bus); vga_putc(':'); vga_write_dec(p.pci.slot); vga_putc('.');
        vga_write_dec(p.pci.func); vga_putc('\n');
    }
    if (p.id) { vga_write("  bga:     "); hex_text(hex, p.id, 4); vga_write(hex); vga_putc('\n'); }
    if (p.bar_base) {
        vga_write("  bar0:    "); vga_write_hex(p.bar_base); vga_write(" size "); vga_write_dec(p.bar_size / 1024);
        vga_write(" KiB\n");
    }
    if (p.max_x) {
        vga_write("  max:     "); vga_write_dec(p.max_x); vga_putc('x'); vga_write_dec(p.max_y); vga_putc('x');
        vga_write_dec(p.max_bpp); vga_putc('\n');
    }
    vga_write("  mode:    640x400x32 ");
    vga_write(p.problem ? "unavailable: " : "available\n");
    if (p.problem) { vga_write(p.problem); vga_putc('\n'); }
    vga_write("  entered: "); vga_write_dec(entries); vga_write("  refused: "); vga_write_dec(refusals); vga_putc('\n');
}
