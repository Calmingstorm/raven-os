#include "kernel.h"

static char line[160];
static uint32_t line_len;

static void prompt(void) {
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_write("raven> ");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
}

static const char *arg_after(const char *cmd, const char *prefix) {
    if (!strprefix(cmd, prefix)) return 0;
    const char *p = cmd + strlen(prefix);
    while (*p == ' ') p++;
    return p;
}

static void write_help(void) {
    vga_write("Commands:\n");
    vga_write("  help, clear, about, stats, timeline, log, caps, objects, manifest\n");
    vga_write("  boot, mem, alloc, paging, heap, clock, consoles, switchtty\n");
    vga_write("  fs, cat [path], edit <path> <text>, write, rm <path>\n");
    vga_write("  disk, ata, save <path>, load <path>, format, install, install-run\n");
    vga_write("  net, ping, sock, send <socket-id> <text>\n");
    vga_write("  env, sethost, pkgs, pkg-install <name>, health, metrics, proc\n");
    vga_write("  services, restart, security, power, reboot, shutdown\n");
    vga_write("  user, exec, load <path>, run [name], abi, rings, sys, tasks, spawn, kill <id>, snapshot, panic-test\n");
}

void shell_render_home(void) {
    vga_clear();
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("RavenOS " RAVEN_VERSION " - wyrd-log native kernel\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("One machine. One timeline. Fewer excuses.\n\n");
    wyrd_render(8);
    vga_write("\nType help. New in 0.9: GDT/TSS, int80 gate, ATA identify probe, exec registry, scheduler rotation accounting.\n\n");
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
    vga_write(" files="); vga_write_dec(ramfs_count());
    vga_write(" fs_bytes="); vga_write_dec(ramfs_bytes_used());
    vga_write(" disk_rw="); vga_write_dec(storage_reads()); vga_write("/"); vga_write_dec(storage_writes());
    vga_write(" version=" RAVEN_VERSION "\n");
}

static void cmd_cat(const char *path) {
    if (!path || !path[0]) path = "/etc/motd";
    const char *content = ramfs_read(path);
    if (!content) content = storage_load_file(path);
    if (content) {
        vga_write(path); vga_write(": "); vga_write(content); vga_putc('\n');
    } else {
        vga_write("not found: "); vga_write(path); vga_putc('\n');
    }
}

static void cmd_edit(const char *rest) {
    if (!rest || !rest[0]) { vga_write("usage: edit <path> <text>\n"); return; }
    char path[48];
    uint32_t i = 0;
    while (rest[i] && rest[i] != ' ' && i + 1 < sizeof(path)) { path[i] = rest[i]; i++; }
    path[i] = '\0';
    while (rest[i] == ' ') i++;
    if (!path[0] || !rest[i]) { vga_write("usage: edit <path> <text>\n"); return; }
    if (ramfs_write(path, rest + i)) {
        vga_write("wrote "); vga_write(path); vga_putc('\n');
    }
}

static void cmd_save(const char *path) {
    if (!path || !path[0]) { vga_write("usage: save <path>\n"); return; }
    const char *content = ramfs_read(path);
    if (!content) { vga_write("ramfs file not found: "); vga_write(path); vga_putc('\n'); return; }
    if (storage_save_file(path, content)) { vga_write("saved "); vga_write(path); vga_write(" to ravenfs\n"); }
}

static void cmd_load(const char *path) {
    if (!path || !path[0]) { vga_write("usage: load <path>\n"); return; }
    const char *content = storage_load_file(path);
    if (!content) { vga_write("disk file not found: "); vga_write(path); vga_putc('\n'); return; }
    ramfs_write(path, content);
    vga_write("loaded "); vga_write(path); vga_write(" into ramfs\n");
}

void shell_execute(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    wyrd_append("shell", "exec", cmd, "ok", "command accepted");
    const char *arg;
    if (!strcmp(cmd, "help")) write_help();
    else if (!strcmp(cmd, "timeline")) wyrd_render(18);
    else if (!strcmp(cmd, "log")) log_render(20);
    else if (!strcmp(cmd, "caps")) caps_render();
    else if (!strcmp(cmd, "objects")) objects_render();
    else if (!strcmp(cmd, "boot")) bootinfo_render();
    else if (!strcmp(cmd, "mem")) pmm_render();
    else if (!strcmp(cmd, "alloc")) { uint32_t frame = pmm_alloc_frame(); vga_write("allocated frame: "); vga_write_hex(frame); vga_putc('\n'); }
    else if (!strcmp(cmd, "paging")) paging_render();
    else if (!strcmp(cmd, "heap")) heap_render();
    else if (!strcmp(cmd, "fs")) ramfs_render();
    else if (!strcmp(cmd, "cat")) cmd_cat("/etc/motd");
    else if ((arg = arg_after(cmd, "cat "))) cmd_cat(arg);
    else if (!strcmp(cmd, "write")) { ramfs_write("/notes/live", "written through shell command"); vga_write("wrote /notes/live\n"); }
    else if ((arg = arg_after(cmd, "edit "))) cmd_edit(arg);
    else if ((arg = arg_after(cmd, "rm "))) { vga_write(ramfs_delete(arg) ? "deleted\n" : "not found\n"); }
    else if (!strcmp(cmd, "disk")) storage_render();
    else if (!strcmp(cmd, "ata")) ata_render();
    else if ((arg = arg_after(cmd, "save "))) cmd_save(arg);
    else if ((arg = arg_after(cmd, "load "))) cmd_load(arg);
    else if (!strcmp(cmd, "format")) { storage_format(); vga_write("ravenfs formatted\n"); }
    else if (!strcmp(cmd, "net")) net_render();
    else if (!strcmp(cmd, "ping")) net_ping_self();
    else if (!strcmp(cmd, "sock")) { int id = net_open_socket("shell.extra", "127.0.0.1"); vga_write("opened socket #"); vga_write_dec((uint32_t)id); vga_putc('\n'); }
    else if ((arg = arg_after(cmd, "send "))) {
        uint32_t id = parse_u32(arg, 1);
        while (*arg && *arg != ' ') arg++;
        while (*arg == ' ') arg++;
        if (net_send(id, arg && *arg ? arg : "shell payload")) vga_write("sent\n");
    }
    else if (!strcmp(cmd, "env")) env_render();
    else if (!strcmp(cmd, "sethost")) { env_set("hostname", "raven-lab"); vga_write("hostname set\n"); }
    else if (!strcmp(cmd, "pkgs")) package_render();
    else if ((arg = arg_after(cmd, "pkg-install "))) { vga_write(package_install(arg) ? "installed\n" : "package not found\n"); }
    else if (!strcmp(cmd, "health")) health_render();
    else if (!strcmp(cmd, "metrics")) metrics_render();
    else if (!strcmp(cmd, "proc")) procfs_render();
    else if (!strcmp(cmd, "services")) service_render();
    else if (!strcmp(cmd, "restart")) { service_restart("shell"); vga_write("shell service restart recorded\n"); }
    else if (!strcmp(cmd, "security")) { security_render(); vga_write("sample shell trigger panic => "); vga_write(security_check("shell", "trigger", "panic") ? "allow" : "deny"); vga_putc('\n'); }
    else if (!strcmp(cmd, "clock")) clock_render();
    else if (!strcmp(cmd, "consoles")) console_render();
    else if (!strcmp(cmd, "switchtty")) { console_switch("tty1"); console_render(); }
    else if (!strcmp(cmd, "manifest")) manifest_render();
    else if (!strcmp(cmd, "snapshot")) { snapshot_take(); snapshot_render(); }
    else if (!strcmp(cmd, "power")) power_render();
    else if (!strcmp(cmd, "reboot")) { power_request("reboot"); vga_write("reboot request recorded, not executed in prototype\n"); }
    else if (!strcmp(cmd, "shutdown")) { power_request("shutdown"); vga_write("shutdown request recorded, not executed in prototype\n"); }
    else if (!strcmp(cmd, "user")) user_render();
    else if (!strcmp(cmd, "exec")) exec_render();
    else if ((arg = arg_after(cmd, "load "))) { vga_write(exec_load(arg) ? "loaded\n" : "load failed\n"); }
    else if (!strcmp(cmd, "run")) user_run_demo();
    else if ((arg = arg_after(cmd, "run "))) { if (!exec_run(arg)) vga_write("program not found or not executable\n"); }
    else if (!strcmp(cmd, "rings")) gdt_render();
    else if (!strcmp(cmd, "abi")) syscall_render();
    else if (!strcmp(cmd, "install")) { installer_render(); installer_plan(); }
    else if (!strcmp(cmd, "install-run")) { installer_run(); storage_render(); }
    else if (!strcmp(cmd, "sys")) { uint32_t t = syscall_dispatch(2,0,0,0); syscall_dispatch(1,0,0,0); vga_write("SYS_TICKS returned "); vga_write_dec(t); vga_putc('\n'); }
    else if (!strcmp(cmd, "tasks")) task_render();
    else if (!strcmp(cmd, "spawn")) { uint32_t id = task_spawn("demo.worker", "ready"); vga_write("spawned task id="); vga_write_dec(id); vga_putc('\n'); }
    else if ((arg = arg_after(cmd, "kill "))) { uint32_t id = parse_u32(arg, 0); vga_write(task_kill(id) ? "killed\n" : "not found\n"); }
    else if (!strcmp(cmd, "stats")) stats();
    else if (!strcmp(cmd, "clear")) { shell_render_home(); return; }
    else if (!strcmp(cmd, "about")) { vga_write("RavenOS is a tiny 32-bit research OS built around a native audit timeline.\n"); vga_write("It is not POSIX-with-a-new-logo. It is an inspectable machine first.\n"); }
    else if (!strcmp(cmd, "panic-test")) {
        if (!cap_check("panic.trigger", "shell")) { wyrd_append("capability", "deny", "panic.trigger", "blocked", "shell lacks grant"); vga_write("Denied: shell does not hold panic.trigger. Good.\n"); }
        else vga_write("Capability granted unexpectedly. Refusing anyway.\n");
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
                vga_backspace();
            }
        } else if (c >= 32 && c <= 126 && line_len + 1 < sizeof(line)) {
            line[line_len++] = c;
            line[line_len] = '\0';
            vga_putc(c);
        }
    }
}
