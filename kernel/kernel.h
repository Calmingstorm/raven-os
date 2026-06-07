#ifndef RAVEN_KERNEL_H
#define RAVEN_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#define RAVEN_VERSION "0.4.0"
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

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);
void io_wait(void);

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putc(char c);
void vga_write(const char *s);
void vga_write_dec(uint32_t n);
void vga_write_hex(uint32_t n);
void vga_status(const char *left, const char *right);
void vga_draw_prompt(const char *line);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void strcopy(char *dst, const char *src, size_t cap);

void wyrd_init(void);
void wyrd_append(const char *actor, const char *verb, const char *object, const char *result, const char *note);
void wyrd_render(uint32_t limit);
uint32_t wyrd_count(void);

void caps_init(void);
void caps_render(void);
int cap_check(const char *name, const char *holder);

void objects_init(void);
void objects_render(void);

void idt_init(void);
void isr_handler(Registers *regs);
void irq_handler(Registers *regs);

void timer_init(uint32_t frequency);
uint32_t timer_ticks(void);
void timer_wait(uint32_t ticks);

void keyboard_init(void);
int keyboard_pop(char *out);
void keyboard_on_irq(void);

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

void task_init(void);
void task_render(void);
void task_tick(void);
uint32_t task_spawn(const char *name, const char *state);

void paging_init(void);
void paging_render(void);

void ramfs_init(void);
void ramfs_render(void);
const char *ramfs_read(const char *path);
int ramfs_write(const char *path, const char *content);

void syscall_init(void);
void syscall_render(void);
uint32_t syscall_dispatch(uint32_t num, uint32_t a, uint32_t b, uint32_t c);

void user_init(void);
void user_render(void);
void user_run_demo(void);

void storage_init(void);
void storage_render(void);
int storage_write_sector(uint32_t lba, const uint8_t *src);
int storage_read_sector(uint32_t lba, uint8_t *dst);

void net_init(void);
void net_render(void);
void net_ping_self(void);

void installer_init(void);
void installer_render(void);
void installer_plan(void);

#endif
