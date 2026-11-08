/* Production-linked synthetic guest: no engine, third-party game code or WAD.
 * Same runtime/libc/ATA objects as the real image, actual IRQs and page tables. */
#include "kernel.h"
#include "raven_doom_runtime.h"
#include "raven_wad.h"
#include "raven_clock.h"
#include "raven_ata.h"
#include "../libc/include/stdio.h"
#include "../libc/include/stdlib.h"
#include "../libc/include/string.h"
#include "../libc/include/errno.h"

static unsigned assertions, cleanups, entries;
static void *retained, *unrelated;
static FILE *live_file;
static _Noreturn void finish(unsigned status) {
    outl(0xf4, status);
    for (;;) __asm__ volatile ("cli; hlt");
}
static void check(int ok, const char *reason) {
    assertions++;
    if (!ok) { serial_write("ROUND3 FAIL "); serial_write(reason); serial_write("\n"); finish(0x11); }
}
static uint32_t master_checksum(void) {
    uint8_t sector[512];
    uint32_t hash = 2166136261u;
    uint32_t count = raven_ata_sector_count(0);
    for (uint32_t lba = 0; lba < count; lba++) {
        check(raven_ata_read_sector(RAVEN_ATA_MASTER, lba, sector), "master still readable");
        for (unsigned i = 0; i < sizeof(sector); i++) hash = (hash ^ sector[i]) * 16777619u;
    }
    return hash;
}
static void cleanup(void) {
    cleanups++;
    check(raven_doom_active(), "session alive during cleanup");
    check(retained && ((unsigned char *)retained)[0] == 0x42, "allocation alive during cleanup");
    check(raven_doom_allocation_count() == 1, "one leaked session allocation before cleanup");
    if (live_file) check(ftell(live_file) == 4, "stream alive during cleanup");
}
static void entry(int argc, char **argv) {
    entries++;
    check(argc == 1 && !strcmp(argv[0], "mock"), "argv intact after setjmp");
    check(raven_doom_run(entry, cleanup, argc, argv).kind == RAVEN_DOOM_BUSY, "nested boundary rejected");
    retained = malloc(513);
    check(retained && !((uintptr_t)retained & 15u), "session allocation alignment");
    memset(retained, 0x42, 513);
    void *zero = calloc(17, 3);
    check(zero != 0, "calloc");
    for (unsigned i = 0; i < 51; i++) check(((unsigned char *)zero)[i] == 0, "calloc zero bytes");
    free(zero);
    check(realloc(retained, SIZE_MAX) == 0 && ((unsigned char *)retained)[512] == 0x42, "realloc failure preserves contents");
    retained = realloc(retained, 1025);
    check(retained && ((unsigned char *)retained)[512] == 0x42, "realloc copies old bytes");
    if (entries == 1) return;
    if (entries == 2) exit(-37);
    if (entries == 3) exit(0);
    raven_doom_abort("deliberate recoverable guest failure");
}
static void test_runtime(void) {
    char arg[] = "mock";
    char *argv[] = {arg, 0};
    unrelated = kmalloc(31);
    check(unrelated != 0, "unrelated kernel allocation");
    memset(unrelated, 0xa7, 31);
    check(raven_doom_run(0, cleanup, 0, 0).kind == RAVEN_DOOM_UNAVAILABLE, "null entry rejected");
    for (unsigned i = 0; i < 4; i++) {
        retained = 0; live_file = 0;
        RavenDoomResult r = raven_doom_run(entry, cleanup, 1, argv);
        check(r.kind == (i == 0 ? RAVEN_DOOM_RETURNED : i == 3 ? RAVEN_DOOM_ABORTED : RAVEN_DOOM_EXITED), "boundary result kind");
        check(r.status == (i == 1 ? -37 : i == 3 ? 1 : 0), "exact exit status");
        check(cleanups == i + 1 && raven_doom_allocation_count() == 0, "cleanup once with reclamation");
        check(!raven_doom_active(), "boundary inactive on return");
        for (unsigned n = 0; n < 31; n++) check(((unsigned char *)unrelated)[n] == 0xa7, "kernel allocation survives cleanup");
    }
    kfree(unrelated);
    serial_write("ROUND3 PASS runtime\n");
}
static void file_entry(int argc, char **argv) {
    (void)argc; (void)argv;
    retained = malloc(32); check(retained != 0, "file session allocation");
    memset(retained, 0x42, 32);
    live_file = fopen(RAVEN_WAD_PATH, "rb");
    check(live_file != 0, "stdio opens WAD");
    char magic[4];
    check(fread(magic, 1, 4, live_file) == 4 && !memcmp(magic, "IWAD", 4), "stdio reads WAD");
    exit(0);
}
static void test_wad(void) {
    check(raven_wad_mount() == 0, "mount valid fixture");
    RavenWadFile *f = raven_wad_open(RAVEN_WAD_PATH);
    check(f != 0, "WAD open");
    uint32_t size = 0;
    check(raven_wad_size(f, &size) == 0 && size == RAVEN_WAD_LOGICAL_BYTES, "logical size excludes padding");
    uint8_t data[1024]; size_t got;
    check(raven_wad_read_at(f, 1021, data, sizeof(data), &got) == 0 && got == sizeof(data), "unaligned multi-sector read");
    for (unsigned i = 0; i < sizeof(data); i++) check(data[i] == (uint8_t)(i * 17 + 5), "exact bytes");
    check(raven_wad_read_at(f, size - 3, data, sizeof(data), &got) == 0 && got == 3 && !memcmp(data, "END", 3), "final partial sector excludes padding");
    check(raven_wad_read_at(f, size, data, 1, &got) == 0 && got == 0, "EOF");
    check(raven_wad_close(f) == 0, "close WAD");
    unsigned count = cleanups;
    RavenDoomResult r = raven_doom_run(file_entry, cleanup, 0, 0);
    check(r.kind == RAVEN_DOOM_EXITED && cleanups == count + 1, "file cleanup boundary");
    check(raven_wad_size(f, &size) == -1, "unmount invalidates old WAD handle");
    serial_write("ROUND3 PASS WAD\n");
}
static void test_clock(void) {
    uint32_t start = raven_clock_ms();
    raven_sleep_ms(0);
    raven_sleep_ms(21);
    uint32_t elapsed = raven_clock_ms() - start;
    check(elapsed >= 21 && elapsed < 1000, "real PIT interrupt-driven sleep");
    serial_write("ROUND3 PASS clock\n");
}
static void test_libc(void) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%lld %llu %#08x %.*s %zu",
                     (-9223372036854775807LL - 1), 18446744073709551615ULL,
                     0x2au, 3, "abcdef", (size_t)12345);
    check(n > 0 && !strcmp(buf,
        "-9223372036854775808 18446744073709551615 0x00002a abc 12345"),
        "i386 variadic lengths and 64-bit integer formatting");
    long long signed_value = 0; unsigned long long unsigned_value = 0;
    size_t size_value = 0; signed char byte = 0;
    check(sscanf("-9223372036854775808 18446744073709551615 12345 -128",
        "%lld %llu %zu %hhd", &signed_value, &unsigned_value, &size_value, &byte) == 4,
        "i386 scanf argument ABI");
    check(signed_value == (-9223372036854775807LL - 1) &&
        unsigned_value == 18446744073709551615ULL && size_value == 12345 && byte == -128,
        "i386 scanf exact integer limits");
    int untouched = 77;
    check(snprintf(buf, sizeof(buf), "not accepted %n", &untouched) == -1 && untouched == 77,
        "unsupported printf does not write destination argument");
    check(sscanf("9", "%d %n", &untouched, &untouched) == -1 && untouched == 77,
        "unsupported scanf prevalidation leaves destination unchanged");
    serial_write("ROUND3 PASS libc\n");
}
void kernel_main(uint32_t magic, uint32_t info) {
    __asm__ volatile ("cli");
    vga_init(); wyrd_init(); serial_init(); gdt_init(); idt_init();
    bootinfo_init(magic, info); pmm_init(); paging_init(); heap_init();
    task_init(); process_init(); keyboard_init(); timer_init(100); ata_init();
    storage_init(); /* 64-sector scratch must remain volatile, never overrun. */
    __asm__ volatile ("sti");
    serial_write("ROUND3 START\n");
    uint32_t before = master_checksum();
    test_runtime(); test_clock(); test_libc();
#if ROUND3_MODE == 0
    test_wad();
#else
    check(raven_wad_mount() == -1, "missing/corrupt/short WAD refused");
    serial_write("ROUND3 PASS WAD refusal\n");
#endif
    check(before == master_checksum(), "master unchanged by slave probes and reads");
    serial_write("ROUND3 PASS master unchanged\n");
    serial_write("ROUND3 PASS all assertions=");
    char count[32]; snprintf(count, sizeof(count), "%u\n", assertions); serial_write(count);
    finish(0x10);
}
