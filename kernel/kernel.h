#ifndef RAVEN_KERNEL_H
#define RAVEN_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#define RAVEN_VERSION "0.15.0"
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define WYRD_CAP 256
#define WYRD_ACTOR_CAP 64
#define WYRD_VERB_CAP 32
/* Includes the NUL: the shell's entire supported 159-byte command fits. */
#define WYRD_OBJECT_CAP 160
#define WYRD_RESULT_CAP 32
#define WYRD_NOTE_CAP 192
/* 1023 usable events: a 159-character paste plus Enter fits even when each
 * character needs its own Shift make/break pair (638 events), with margin. */
#define KEYBUF_CAP 1024
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

enum {
    WYRD_TRUNC_ACTOR = 1u << 0, WYRD_TRUNC_VERB = 1u << 1,
    WYRD_TRUNC_OBJECT = 1u << 2, WYRD_TRUNC_RESULT = 1u << 3,
    WYRD_TRUNC_NOTE = 1u << 4
};
typedef struct {
    uint32_t tick;
    char actor[WYRD_ACTOR_CAP];
    char verb[WYRD_VERB_CAP];
    char object[WYRD_OBJECT_CAP];
    char result[WYRD_RESULT_CAP];
    char note[WYRD_NOTE_CAP];
    uint8_t truncated;
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
void vga_scrollback(int lines);
void vga_live(void);
void vga_input_begin(void);
void vga_input_redraw(const char *text, uint32_t len, uint32_t cursor);
void vga_input_end(void);

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
int serial_can_read(void);
int serial_read_char(char *out);
void serial_render(void);

void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
void io_wait(void);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);

/* PCI configuration mechanism #1 (ports 0xCF8/0xCFC). */
typedef struct {
    uint8_t bus, slot, func;
    uint16_t vendor, device;
    uint8_t class_code, subclass;
} PciDevice;
uint32_t pci_read32(const PciDevice *dev, uint8_t offset);
void pci_write32(const PciDevice *dev, uint8_t offset, uint32_t value);
/* The nth (from 0) device of a class, in bus/slot/function order. */
int pci_find_class(uint8_t class_code, uint8_t subclass, uint32_t nth, PciDevice *out);
void pci_enable_memory(const PciDevice *dev);
/* A validated 32-bit memory BAR: returns 1 with its base and size, or 0. */
int pci_memory_bar(const PciDevice *dev, uint8_t index, uint32_t *base, uint32_t *size);

/* 640x400x32 session: framebuffer and stride (pixels), or 0 with *why. gfx_end restores the console. */
/* 1 when the 640x400x32 mode can be entered, else 0 with *why. Changes nothing. */
int gfx_available(const char **why);
volatile uint32_t *gfx_begin(uint32_t *stride, const char **why);
void gfx_end(void);
void gfx_command(void);
void gfx_render_info(void);
void vga_refresh(void);
/* 0 while graphics own the display: text is kept, VRAM and the CRTC are left alone. 1 hands them back. */
void vga_hardware(int on);

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
/* Main-loop API. Copy out a snapshot by oldest-retained index, not a ring pointer. */
int wyrd_get(uint32_t oldest_index, WyrdEvent *out);

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

typedef enum {
    KEY_NONE = 0, KEY_CHARACTER, KEY_ENTER, KEY_BACKSPACE, KEY_TAB, KEY_ESCAPE,
    KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_DELETE, KEY_UP, KEY_DOWN,
    KEY_PAGEUP, KEY_PAGEDOWN, KEY_SHIFT, KEY_CTRL, KEY_ALT, KEY_CAPSLOCK
} KeyCode;
enum { KEY_MOD_SHIFT = 1, KEY_MOD_CTRL = 2, KEY_MOD_ALT = 4 };
typedef struct {
    uint16_t scancode;
    KeyCode code;
    char ascii;
    uint8_t modifiers;
    uint8_t pressed;
    uint8_t repeat;   /* a make for a key already held: typematic repeat, not a new press */
} KeyEvent;

void keyboard_init(void);
void keyboard_irq(void);
void keyboard_on_irq(void);
void keyboard_feed_scancode(uint8_t scancode);
int keyboard_pop(KeyEvent *out);
uint32_t keyboard_dropped_events(void);
/* The keys physically down as the driver records them (dropped events included): bit slot % 8 of out[slot / 8],
 * slot = 0x80 for an extended key | its scancode. Returns the dropped-event count, read together with it. */
uint32_t keyboard_held_snapshot(uint8_t out[32]);

void shell_init(void);
void shell_tick(void);
void shell_render_home(void);
void shell_execute(const char *line);

void bootinfo_init(uint32_t magic, uint32_t info_addr);
void bootinfo_render(void);
uint32_t bootinfo_mem_kib(void);
void bootinfo_visit_memory(void (*visit)(uint64_t base, uint64_t len, int usable));

void pmm_init(void);
uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t frame_addr);
void pmm_render(void);

void heap_init(void);
/* Main-loop only. Zero-size allocation returns NULL; free(NULL) is harmless.
 * realloc(p,0) frees; realloc failure preserves p. Payloads are 16-byte aligned. */
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t size);
void *kcalloc(size_t count, size_t size);
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
/* Main-loop only; byte-granular device range, outward 4KiB rounding, offset
 * preserved. Supervisor RW PCD/PWT, no PAT changes. RAM/direct-map overlaps,
 * zero/overflow/resource failure return NULL transactionally. Persistent until
 * reboot; fully covered ranges reuse mappings, other overlaps fail. The caller
 * validates the PCI BAR/aperture and must use only the returned virtual address.
 * See docs/round2-memory.md for the full contract and resource limits. */
volatile void *paging_map_mmio(uint32_t phys_addr, size_t size);
/* One transactional contiguous virtual arena backed by independent PMM frames.
 * Page-aligned nonzero byte count <= 16MiB. Failed attempts may be retried. */
void *paging_alloc_heap(size_t bytes);

void ramfs_init(void);
void ramfs_render(void);
const char *ramfs_read(const char *path);
int ramfs_write(const char *path, const char *content);
int ramfs_append(const char *path, const char *suffix);
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
int storage_append_file(const char *path, const char *suffix);
int storage_sync_ramfs(void);
const char *storage_load_file(const char *path);
void storage_ls(void);
int storage_delete_file(const char *path);
int storage_has_file(const char *path);
void storage_ls_prefix(const char *prefix);

int storage_fsck(void);
void storage_debug_corrupt(const char *path);
int storage_debug_stage_journal_save(const char *path, const char *content);

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
