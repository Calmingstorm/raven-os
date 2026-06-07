#include "kernel.h"

typedef struct {
    char path[48];
    char content[256];
    uint32_t used;
} RamFile;

#define RAMFS_CAP 40
static RamFile files[RAMFS_CAP];
static uint32_t file_count;

static int find_file(const char *path) {
    for (uint32_t i = 0; i < file_count; i++) {
        if (files[i].used && !strcmp(files[i].path, path)) return (int)i;
    }
    return -1;
}

static void add_file(const char *path, const char *content) {
    if (file_count >= RAMFS_CAP) return;
    strcopy(files[file_count].path, path, sizeof(files[file_count].path));
    strcopy(files[file_count].content, content, sizeof(files[file_count].content));
    files[file_count].used = 1;
    file_count++;
}

void ramfs_init(void) {
    file_count = 0;
    memset(files, 0, sizeof(files));
    add_file("/etc/motd", "RavenOS: the machine remembers. Type help.");
    add_file("/notes/first", "This RAM filesystem is volatile, but it proves the object/storage path.");
    add_file("/sys/version", RAVEN_VERSION);
    add_file("/home/readme", "Try edit /home/readme changed, save /home/readme, load /persist/readme.");
    wyrd_append("vfs", "init", "ramfs", "ok", "volatile file store mounted");
}

const char *ramfs_read(const char *path) {
    int i = find_file(path);
    if (i >= 0) return files[i].content;
    return 0;
}

int ramfs_write(const char *path, const char *content) {
    int found = find_file(path);
    if (found >= 0) {
        strcopy(files[found].content, content, sizeof(files[found].content));
        wyrd_append("vfs", "write", path, "ok", "ramfs file updated");
        return 1;
    }
    if (file_count < RAMFS_CAP) {
        add_file(path, content);
        wyrd_append("vfs", "create", path, "ok", "ramfs file created");
        return 1;
    }
    wyrd_append("vfs", "write", path, "fail", "ramfs full");
    return 0;
}

int ramfs_delete(const char *path) {
    int i = find_file(path);
    if (i < 0) return 0;
    files[i].used = 0;
    wyrd_append("vfs", "delete", path, "ok", "ramfs entry removed");
    return 1;
}

int ramfs_exists(const char *path) {
    return find_file(path) >= 0;
}

void ramfs_ls(const char *prefix) {
    const char *pfx = prefix && prefix[0] ? prefix : "/";
    vga_write("ramfs list "); vga_write(pfx); vga_putc('\n');
    for (uint32_t i = 0; i < file_count; i++) {
        if (files[i].used && strprefix(files[i].path, pfx)) {
            vga_write("  "); vga_write(files[i].path); vga_write("  "); vga_write_dec(strlen(files[i].content)); vga_write(" bytes\n");
        }
    }
}

uint32_t ramfs_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < file_count; i++) if (files[i].used) n++;
    return n;
}

const char *ramfs_path_at(uint32_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < file_count; i++) {
        if (files[i].used) {
            if (seen == index) return files[i].path;
            seen++;
        }
    }
    return 0;
}

uint32_t ramfs_bytes_used(void) {
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < file_count; i++) {
        if (files[i].used) bytes += strlen(files[i].path) + strlen(files[i].content);
    }
    return bytes;
}

void ramfs_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("ramfs\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < file_count; i++) {
        if (files[i].used) {
            vga_write("  "); vga_write(files[i].path); vga_write(" = "); vga_write(files[i].content); vga_putc('\n');
        }
    }
}
