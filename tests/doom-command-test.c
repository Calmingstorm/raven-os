#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/kernel.h"
#include "../kernel/raven_doom_runtime.h"
#include "../kernel/raven_wad.h"
#include "../libc/include/errno.h"

int errno;
static unsigned checks;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)
static char output[4096], serial_output[1024];
static unsigned mounts, unmounts, probe_calls, probe_frees;
static unsigned entries, cleanups, resets, logs;
static int missing_wad, fail_probe, interrupts_enabled = 1;
static int port_action, port_status;
static unsigned char *port_memory;
static void *probe;
static char log_result[32];

void vga_write(const char *s) {
    CHECK(strlen(output) + strlen(s) < sizeof output);
    strcat(output, s);
}
void vga_putc(char c) { char s[] = {c, 0}; vga_write(s); }
void vga_write_dec(uint32_t n) {
    char s[32];
    snprintf(s, sizeof s, "%u", (unsigned)n);
    vga_write(s);
}
void serial_write(const char *s) {
    CHECK(strlen(serial_output) + strlen(s) < sizeof serial_output);
    strcat(serial_output, s);
}
void wyrd_append(const char *actor, const char *verb, const char *object,
                 const char *result, const char *note) {
    CHECK(strcmp(actor, "doom") == 0 && strcmp(verb, "session") == 0);
    CHECK(strcmp(object, "runtime") == 0);
    CHECK(strcmp(note, "console ownership restored") == 0);
    CHECK(!raven_doom_active());
    CHECK(strlen(result) < sizeof log_result);
    strcpy(log_result, result);
    logs++;
}
int raven_runtime_interrupts_enabled(void) { return interrupts_enabled; }
int keyboard_pop(KeyEvent *event) { (void)event; return 0; }
int serial_read_char(char *c) { (void)c; return 0; }
int raven_wad_mount(void) {
    CHECK(!raven_doom_active());
    mounts++;
    if (missing_wad) { errno = ENOENT; return -1; }
    return 0;
}
void raven_wad_unmount(void) {
    if (raven_doom_active()) {
        CHECK(resets > 0);
        if (port_memory) CHECK(port_memory[0] == 0x69);
    }
    unmounts++;
}
void raven_libc_reset_streams(void) {
    CHECK(raven_doom_active());
    if (port_memory) {
        CHECK(cleanups == 1 && entries == 1);
        CHECK(port_memory[0] == 0x69);
    }
    resets++;
}
void *kmalloc(size_t size) {
    if (size == 8u * 1024u * 1024u) {
        CHECK(!raven_doom_active() && !probe);
        probe_calls++;
        if (fail_probe) return NULL;
        probe = malloc(size);
        CHECK(probe != NULL);
        return probe;
    }
    CHECK(raven_doom_active());
    void *p = malloc(size);
    CHECK(p != NULL);
    return p;
}
void kfree(void *p) {
    CHECK(p != NULL);
    if (p == probe) {
        CHECK(!raven_doom_active());
        probe_frees++;
        probe = NULL;
    } else {
        CHECK(raven_doom_active() && entries == 1 && cleanups == 1);
        CHECK(resets > 0);
    }
    free(p);
}

#ifdef HAVE_PORT_MAIN
void raven_doom_port_main(int argc, char **argv) {
    CHECK(entries++ == 0);
    CHECK(raven_doom_active());
    CHECK(argc == 3 && argv && argv[3] == NULL);
    CHECK(strcmp(argv[0], "doom") == 0);
    CHECK(strcmp(argv[1], "-iwad") == 0);
    CHECK(strcmp(argv[2], RAVEN_WAD_PATH) == 0);
    CHECK(probe == NULL && probe_frees >= 1);
    port_memory = raven_doom_malloc(29);
    CHECK(port_memory != NULL);
    memset(port_memory, 0x69, 29);
    if (port_action == 1) raven_doom_exit(port_status);
    if (port_action == 2) raven_doom_abort("mock port partial initialization");
}
#endif
#ifdef HAVE_PORT_CLEANUP
void raven_doom_port_cleanup(void) {
    CHECK(cleanups++ == 0);
    CHECK(raven_doom_active() && entries == 1);
    CHECK(port_memory && port_memory[0] == 0x69 && port_memory[28] == 0x69);
    CHECK(raven_doom_allocation_count() == 1);
}
#endif

#if defined(HAVE_PORT_MAIN) && defined(HAVE_PORT_CLEANUP)
static void nested_command_entry(int argc, char **argv) {
    (void)argc; (void)argv;
    doom_command();
    CHECK(strstr(output, "already active") != NULL);
    CHECK(mounts == 0 && probe_calls == 0 && entries == 0);
}
static void linked_test(const char *mode) {
    if (strcmp(mode, "exit0") == 0) { port_action = 1; port_status = 0; }
    else if (strcmp(mode, "exitpositive") == 0) { port_action = 1; port_status = 117; }
    else if (strcmp(mode, "exitnegative") == 0) { port_action = 1; port_status = INT_MIN; }
    else if (strcmp(mode, "abort") == 0) port_action = 2;
    else CHECK(strcmp(mode, "return") == 0);

    /* An externally active mock boundary does not consume the one engine
     * initialization permitted by the shell command. */
    RavenDoomResult r = raven_doom_run(nested_command_entry, NULL, 0, NULL);
    CHECK(r.kind == RAVEN_DOOM_RETURNED && !raven_doom_active());
    CHECK(entries == 0 && cleanups == 0 && resets == 1 && unmounts == 1);

    output[0] = 0;
    missing_wad = 1;
    doom_command();
    CHECK(strstr(output, "WAD validation failed") != NULL);
    CHECK(mounts == 1 && probe_calls == 0 && entries == 0 && logs == 0);

    output[0] = 0;
    missing_wad = 0;
    fail_probe = 1;
    doom_command();
    CHECK(strstr(output, "8 MiB") != NULL);
    CHECK(mounts == 2 && probe_calls == 1 && probe_frees == 0);
    CHECK(unmounts == 2 && entries == 0 && logs == 0);

    /* Runtime preflight refusal must clear engine_attempted and unmount. */
    output[0] = 0;
    fail_probe = 0;
    interrupts_enabled = 0;
    doom_command();
    CHECK(strstr(output, "unavailable") != NULL);
    CHECK(mounts == 3 && probe_calls == 2 && probe_frees == 1);
    CHECK(unmounts == 3 && entries == 0 && cleanups == 0);
    CHECK(logs == 1 && resets == 1);

    output[0] = 0;
    interrupts_enabled = 1;
    doom_command();
    CHECK(mounts == 4 && probe_calls == 3 && probe_frees == 2);
    CHECK(entries == 1 && cleanups == 1 && resets == 2 && unmounts == 4);
    CHECK(logs == 2 && probe == NULL);
    CHECK(!raven_doom_active() && raven_doom_allocation_count() == 0);
    if (port_action == 2) {
        CHECK(strstr(output, "session error (status=1)") != NULL);
        CHECK(strcmp(serial_output, "Doom session error: mock port partial initialization\n") == 0);
        CHECK(strcmp(log_result, "error") == 0);
    } else {
        char expected[96];
        snprintf(expected, sizeof expected, "%s (status=%d)",
                 port_action == 1 ? "exit" : "returned", port_action == 1 ? port_status : 0);
        CHECK(strstr(output, expected) != NULL);
        CHECK(serial_output[0] == 0 && strcmp(log_result, "returned") == 0);
    }
    port_memory = NULL; /* session has reclaimed the underlying allocation */

    for (unsigned i = 0; i < 4; i++) {
        output[0] = 0;
        doom_command();
        CHECK(strstr(output, "already initialized") != NULL);
        CHECK(entries == 1 && cleanups == 1 && mounts == 4);
        CHECK(probe_calls == 3 && resets == 2 && unmounts == 4 && logs == 2);
    }
}
#endif

int main(int argc, char **argv) {
#if defined(HAVE_PORT_MAIN) && defined(HAVE_PORT_CLEANUP)
    CHECK(argc == 2);
    linked_test(argv[1]);
    printf("doom-command-test: linked %s: %u checks passed\n", argv[1], checks);
#else
    (void)argc; (void)argv;
    /* Missing either weak adapter is a stock-kernel no-engine stub. It must
     * never mount disks, reserve 8 MiB, run a partial adapter or set the guard. */
    for (unsigned i = 0; i < 3; i++) {
        output[0] = 0;
        doom_command();
        CHECK(strcmp(output, "Doom unavailable: engine port is not linked in this build.\n") == 0);
        CHECK(!mounts && !unmounts && !probe_calls && !entries && !cleanups);
        CHECK(!resets && !logs && !raven_doom_active());
    }
    /* These are meaningful in one-adapter variants but not both absent. */
    (void)port_action; (void)port_status;
    printf("doom-command-test: absent/partial engine: %u checks passed\n", checks);
#endif
    return 0;
}
