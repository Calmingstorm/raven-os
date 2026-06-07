#include "kernel.h"

typedef struct {
    const char *path;
    char content[256];
    uint32_t used;
} RamFile;

#define RAMFS_CAP 16
static RamFile files[RAMFS_CAP];
static uint32_t file_count;

static void add_file(const char *path, const char *content) {
    if (file_count >= RAMFS_CAP) return;
    files[file_count].path = path;
    strcopy(files[file_count].content, content, sizeof(files[file_count].content));
    files[file_count].used = 1;
    file_count++;
}

void ramfs_init(void) {
    file_count = 0;
    add_file("/etc/motd", "RavenOS: the machine remembers. Type help.");
    add_file("/notes/first", "This RAM filesystem is volatile, but it proves the object/storage path.");
    add_file("/sys/version", RAVEN_VERSION);
    wyrd_append("vfs", "init", "ramfs", "ok", "volatile file store mounted");
}

const char *ramfs_read(const char *path) {
    for (uint32_t i = 0; i < file_count; i++) {
        if (files[i].used && !strcmp(files[i].path, path)) return files[i].content;
    }
    return 0;
}

int ramfs_write(const char *path, const char *content) {
    for (uint32_t i = 0; i < file_count; i++) {
        if (files[i].used && !strcmp(files[i].path, path)) {
            strcopy(files[i].content, content, sizeof(files[i].content));
            wyrd_append("vfs", "write", path, "ok", "ramfs file updated");
            return 1;
        }
    }
    if (file_count < RAMFS_CAP) {
        add_file(path, content);
        wyrd_append("vfs", "create", path, "ok", "ramfs file created");
        return 1;
    }
    wyrd_append("vfs", "write", path, "fail", "ramfs capacity reached");
    return 0;
}

void ramfs_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("ramfs /\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < file_count; i++) {
        if (!files[i].used) continue;
        vga_write("  "); vga_write(files[i].path);
        vga_write("  "); vga_write_dec(strlen(files[i].content)); vga_write(" bytes\n");
    }
}
