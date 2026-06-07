#include "kernel.h"

typedef struct {
    char name[24];
    char path[48];
    uint32_t entry;
    uint32_t size;
    uint8_t loaded;
} ExecImage;

#define EXEC_CAP 8
static ExecImage images[EXEC_CAP];
static uint32_t image_count;

static void add_image(const char *name, const char *path, uint32_t entry, uint32_t size, uint8_t loaded) {
    if (image_count >= EXEC_CAP) return;
    strcopy(images[image_count].name, name, sizeof(images[image_count].name));
    strcopy(images[image_count].path, path, sizeof(images[image_count].path));
    images[image_count].entry = entry;
    images[image_count].size = size;
    images[image_count].loaded = loaded;
    image_count++;
}

void exec_init(void) {
    image_count = 0;
    memset(images, 0, sizeof(images));
    add_image("hello", "/bin/hello.rvn", 0x00400000, 4096, 1);
    add_image("clock", "/bin/clock.rvn", 0x00401000, 4096, 1);
    add_image("writer", "/bin/writer.rvn", 0x00402000, 4096, 1);
    add_image("netdemo", "/bin/netdemo.rvn", 0x00403000, 4096, 1);
    ramfs_write("/bin/hello.rvn", "RVNEXE hello builtin image manifest");
    ramfs_write("/bin/clock.rvn", "RVNEXE clock builtin image manifest");
    ramfs_write("/bin/writer.rvn", "RVNEXE writer builtin image manifest");
    ramfs_write("/bin/netdemo.rvn", "RVNEXE netdemo builtin image manifest");
    wyrd_append("exec", "init", "rvnexe", "ok", "executable registry mounted");
}

int exec_load(const char *path) {
    if (!path || !path[0]) return 0;
    for (uint32_t i = 0; i < image_count; i++) {
        if (!strcmp(images[i].path, path) || !strcmp(images[i].name, path)) {
            images[i].loaded = 1;
            wyrd_append("exec", "load", images[i].path, "ok", images[i].name);
            return 1;
        }
    }
    const char *content = ramfs_read(path);
    if (!content) content = storage_load_file(path);
    if (!content || !strprefix(content, "RVNEXE")) {
        wyrd_append("exec", "load", path, "fail", "missing or invalid RVNEXE header");
        return 0;
    }
    add_image("custom", path, 0x00410000 + image_count * 0x1000, strlen(content), 1);
    wyrd_append("exec", "load", path, "ok", "custom manifest accepted");
    return 1;
}

int exec_run(const char *name) {
    if (!exec_load(name)) return 0;
    return user_run_named(name);
}

void exec_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("executable images\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < image_count; i++) {
        vga_write("  "); vga_write(images[i].loaded ? "[loaded] " : "[cold] ");
        vga_write(images[i].name); vga_write(" path="); vga_write(images[i].path);
        vga_write(" entry="); vga_write_hex(images[i].entry);
        vga_write(" size="); vga_write_dec(images[i].size); vga_putc('\n');
    }
    vga_write("  format: RVNEXE manifest scaffold; ELF-like relocatable loader pending\n");
}
