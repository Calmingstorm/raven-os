#ifndef RAVEN_KERNEL_H
#define RAVEN_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#define RAVEN_VERSION "0.12.0"
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define WYRD_CAP 256
#define KEYBUF_CAP 256
#define CAPS_CAP 64
#define OBJECT_CAP 64
#define PMM_MAX_FRAMES 32768 /* 128 MiB at 4 KiB pages; enough for current QEMU target */
#define PAGE_SIZE 4096
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define PIC1 0x20
#define PIC2 0xA0
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)
#define PIC_EOI 0x20

#define IDT_TYPE_INTERRUPT_GATE 0x8E

#define SECTOR_SIZE 512
#define DISK_SECTORS 128

typedef enum {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_LIGHT_BROWN = 14,
    COLOR_WHITE = 15,
} VgaColor;

typedef struct {
    uint32_t tick;
    const char *actor;
    const char *verb;
    const char *object;
    const char *result;
    const char *note;
} WyrdEvent;

typedef struct {
    const char *name;
    const char *holder;
    uint32_t rights;
    uint8_t granted;
} Capability;

typedef struct {
    const char *path;
    const char *type;
    const char *owner;
    const char *state;
} KernelObject;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} IdtPointer;

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} IdtEntry;

typedef struct {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} Registers;

typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} MultibootInfo;

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} MultibootMmapEntry;

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr);

void gdt_init(void);
void gdt_set_kernel_stack(uint32_t stack_top);
uint32_t gdt_user_code_selector(void);
uint32_t gdt_user_data_selector(void);
void gdt_render(void);

/* core */
void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *s);
void vga_write_dec(uint32_t n);
void vga_write_hex(uint32_t n);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_backspace(void);

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
int serial_can_read(void);
int serial_read_char(char *out);
void serial_render(void);

void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
void io_wait(void);

void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
void strcopy(char *dst, const char *src, size_t cap);
int strprefix(const char *s, const char *prefix);
uint32_t parse_u32(const char *s, uint32_t fallback);

void wyrd_init(void);
void wyrd_append(const char *actor, const char *verb, const char *object, const char *result, const char *note);
void wyrd_render(uint32_t limit);
uint32_t wyrd_count(void);

void caps_init(void);
void cap_grant(const char *name, const char *holder, uint32_t rights);
int cap_check(const char *name, const char *holder);
void caps_render(void);

void objects_init(void);
void object_add(const char *path, const char *type, const char *owner, const char *state);
void objects_render(void);

void idt_init(void);
void isr_install(void);
void irq_handler(Registers *regs);

void timer_init(uint32_t frequency);
uint32_t timer_ticks(void);
void timer_tick(void);

void keyboard_init(void);
void keyboard_irq(void);
void keyboard_on_irq(void);
int keyboard_pop(char *out);

void shell_init(void);
void shell_tick(void);
void shell_render_home(void);
void shell_execute(const char *line);

void bootinfo_init(uint32_t magic, uint32_t info_addr);
void bootinfo_render(void);
uint32_t bootinfo_mem_kib(void);

void pmm_init(void);
uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t frame_addr);
void pmm_render(void);

void heap_init(void);
void *kmalloc(size_t size);
void kfree_all(void);
void heap_render(void);

void process_init(void);
uint32_t process_spawn(const char *name, uint32_t entry);
int process_kill(uint32_t pid);
void process_tick(void);
void process_render(void);

void task_init(void);
void task_render(void);
void task_tick(void);
uint32_t task_switches(void);
void task_set_preemptive(uint32_t enabled);
uint32_t task_spawn(const char *name, const char *state);
int task_kill(uint32_t id);
int task_set_state(uint32_t id, const char *state);

void paging_init(void);
void paging_render(void);

void ramfs_init(void);
void ramfs_render(void);
const char *ramfs_read(const char *path);
int ramfs_write(const char *path, const char *content);
int ramfs_delete(const char *path);
void ramfs_ls(const char *prefix);
int ramfs_exists(const char *path);
uint32_t ramfs_count(void);
const char *ramfs_path_at(uint32_t index);
uint32_t ramfs_bytes_used(void);

void syscall_init(void);
void syscall_render(void);
uint32_t syscall_dispatch(uint32_t num, uint32_t a, uint32_t b, uint32_t c);
uint32_t syscall_from_registers(Registers *regs);

void exec_init(void);
int exec_load(const char *path);
int exec_run(const char *name);
void exec_render(void);

void user_init(void);
void user_render(void);
void user_run_demo(void);
int user_run_named(const char *name);
int user_register_script(const char *name, const char *body);

void ata_init(void);
uint32_t ata_present_flag(void);
uint32_t ata_rw_ready_flag(void);
int ata_read_sector(uint32_t lba, uint8_t *dst);
int ata_write_sector(uint32_t lba, const uint8_t *src);
void ata_render(void);

void storage_init(void);
void storage_render(void);
int storage_write_sector(uint32_t lba, const uint8_t *src);
int storage_read_sector(uint32_t lba, uint8_t *dst);
uint32_t storage_reads(void);
uint32_t storage_writes(void);
int storage_format(void);
int storage_save_file(const char *path, const char *content);
const char *storage_load_file(const char *path);
void storage_ls(void);
int storage_delete_file(const char *path);
int storage_has_file(const char *path);
void storage_ls_prefix(const char *prefix);

void net_init(void);
void net_render(void);
void net_ping_self(void);
int net_open_socket(const char *name, const char *peer);
int net_send(uint32_t id, const char *payload);
void net_sockets_render(void);

void installer_init(void);
void installer_render(void);
void installer_plan(void);
int installer_run(void);

void env_init(void);
void env_render(void);
const char *env_get(const char *key);
int env_set(const char *key, const char *value);

void log_init(void);
void log_write(const char *level, const char *source, const char *message);
void log_render(uint32_t limit);

void package_init(void);
void package_render(void);
int package_install(const char *name);
int package_remove(const char *name);
int package_is_installed(const char *name);
void package_render_installed(void);

void power_init(void);
void power_render(void);
void power_request(const char *action);

void health_render(void);

void service_init(void);
void service_render(void);
int service_restart(const char *name);

void security_init(void);
void security_render(void);
int security_check(const char *subject, const char *action, const char *target);

void metrics_render(void);

void procfs_init(void);
void procfs_render(void);

void console_init(void);
void console_render(void);
void console_switch(const char *name);

void clock_init(void);
void clock_render(void);

void manifest_init(void);
void manifest_render(void);

void snapshot_init(void);
void snapshot_render(void);
void snapshot_take(void);

#endif
