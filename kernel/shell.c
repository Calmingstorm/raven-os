#include "kernel.h"

static char line[128];
static uint32_t line_len;

static void prompt(void) {
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_write("raven> ");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
}

void shell_render_home(void) {
    vga_clear();
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("RavenOS " RAVEN_VERSION " - wyrd-log native kernel\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("One machine. One timeline. Fewer excuses.\n\n");
    wyrd_render(8);
    vga_write("\nCommands: help, timeline, caps, objects, stats, clear, about, panic-test\n\n");
    prompt();
}

void shell_init(void) {
    line_len = 0;
    line[0] = '\0';
    wyrd_append("init", "spawn", "shell", "ok", "interactive command palette online");
    shell_render_home();
}

static void stats(void) {
    vga_write("ticks="); vga_write_dec(timer_ticks());
    vga_write(" wyrd_events="); vga_write_dec(wyrd_count());
    vga_write(" version=" RAVEN_VERSION "\n");
}

void shell_execute(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    wyrd_append("shell", "exec", cmd, "ok", "command accepted");
    if (!strcmp(cmd, "help")) {
        vga_write("Commands:\n");
        vga_write("  help        show this command list\n");
        vga_write("  timeline    render recent wyrd events\n");
        vga_write("  caps        inspect capability grants/denials\n");
        vga_write("  objects     inspect kernel object namespace\n");
        vga_write("  stats       show timer and kernel counters\n");
        vga_write("  clear       redraw the home screen\n");
        vga_write("  about       describe RavenOS\n");
        vga_write("  panic-test  verify denied dangerous capability\n");
    } else if (!strcmp(cmd, "timeline")) {
        wyrd_render(18);
    } else if (!strcmp(cmd, "caps")) {
        caps_render();
    } else if (!strcmp(cmd, "objects")) {
        objects_render();
    } else if (!strcmp(cmd, "stats")) {
        stats();
    } else if (!strcmp(cmd, "clear")) {
        shell_render_home();
        return;
    } else if (!strcmp(cmd, "about")) {
        vga_write("RavenOS is a tiny 32-bit research OS built around a native audit timeline.\n");
        vga_write("It is not POSIX-with-a-new-logo. It is an inspectable machine first.\n");
    } else if (!strcmp(cmd, "panic-test")) {
        if (!cap_check("panic.trigger", "shell")) {
            wyrd_append("capability", "deny", "panic.trigger", "blocked", "shell lacks grant");
            vga_write("Denied: shell does not hold panic.trigger. Good.\n");
        } else {
            vga_write("Capability granted unexpectedly. Refusing anyway.\n");
        }
    } else {
        wyrd_append("shell", "lookup", cmd, "miss", "unknown command");
        vga_write("unknown command: "); vga_write(cmd); vga_putc('\n');
    }
}

void shell_tick(void) {
    char c;
    while (keyboard_pop(&c)) {
        if (c == '\n') {
            vga_putc('\n');
            line[line_len] = '\0';
            shell_execute(line);
            line_len = 0;
            line[0] = '\0';
            prompt();
        } else if (c == '\b') {
            if (line_len) {
                line[--line_len] = '\0';
                vga_write("\b");
            }
        } else if (c >= 32 && c <= 126 && line_len + 1 < sizeof(line)) {
            line[line_len++] = c;
            line[line_len] = '\0';
            vga_putc(c);
        }
    }
}
