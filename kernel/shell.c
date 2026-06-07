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
    vga_write("\nCommands: help, timeline, log, caps, objects, boot, mem, paging, heap, fs, cat, write, disk, net, ping, env, sethost, pkgs, health, metrics, proc, services, restart, security, clock, consoles, switchtty, manifest, snapshot, power, reboot, shutdown, user, run, abi, sys, install, alloc, tasks, spawn, stats, clear, about, panic-test\n\n");
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
        vga_write("  log         render structured diagnostic log\n");
        vga_write("  caps        inspect capability grants/denials\n");
        vga_write("  objects     inspect kernel object namespace\n");
        vga_write("  boot        show multiboot memory information\n");
        vga_write("  mem         show physical memory manager state\n");
        vga_write("  alloc       allocate one physical frame for test/proof\n");
        vga_write("  paging      show paging state\n");
        vga_write("  heap        show kernel heap state\n");
        vga_write("  fs          list RAM filesystem\n");
        vga_write("  cat         read /etc/motd\n");
        vga_write("  write       write /notes/live\n");
        vga_write("  disk        show volatile block storage\n");
        vga_write("  net         show network scaffold\n");
        vga_write("  ping        loopback network self-test\n");
        vga_write("  env         show runtime environment\n");
        vga_write("  sethost     set hostname to raven-lab\n");
        vga_write("  pkgs        show package catalog\n");
        vga_write("  health      run operator health report\n");
        vga_write("  metrics     show text metrics export\n");
        vga_write("  proc        show procfs pseudo-files\n");
        vga_write("  services    show service manager state\n");
        vga_write("  restart     restart shell service in supervisor table\n");
        vga_write("  security    show security policy and sample denial\n");
        vga_write("  clock       show monotonic time state\n");
        vga_write("  consoles    show console/session table\n");
        vga_write("  switchtty   switch active console marker to tty1\n");
        vga_write("  manifest    show boot/component manifest\n");
        vga_write("  snapshot    take and show volatile checkpoint\n");
        vga_write("  power       show power-control state\n");
        vga_write("  reboot      record simulated reboot request\n");
        vga_write("  shutdown    record simulated shutdown request\n");
        vga_write("  user        show userspace program table\n");
        vga_write("  run         run demo user programs through ABI\n");
        vga_write("  abi         show syscall ABI\n");
        vga_write("  install     show installer dry-run plan\n");
        vga_write("  sys         exercise syscall dispatcher\n");
        vga_write("  tasks       show task table\n");
        vga_write("  spawn       register a demo task\n");
        vga_write("  stats       show timer and kernel counters\n");
        vga_write("  clear       redraw the home screen\n");
        vga_write("  about       describe RavenOS\n");
        vga_write("  panic-test  verify denied dangerous capability\n");
    } else if (!strcmp(cmd, "timeline")) {
        wyrd_render(18);
    } else if (!strcmp(cmd, "log")) {
        log_render(20);
    } else if (!strcmp(cmd, "caps")) {
        caps_render();
    } else if (!strcmp(cmd, "objects")) {
        objects_render();
    } else if (!strcmp(cmd, "boot")) {
        bootinfo_render();
    } else if (!strcmp(cmd, "mem")) {
        pmm_render();
    } else if (!strcmp(cmd, "alloc")) {
        uint32_t frame = pmm_alloc_frame();
        vga_write("allocated frame: "); vga_write_hex(frame); vga_putc('\n');
    } else if (!strcmp(cmd, "paging")) {
        paging_render();
    } else if (!strcmp(cmd, "heap")) {
        void *p = kmalloc(32);
        vga_write("kmalloc(32): "); vga_write_hex((uint32_t)p); vga_putc('\n');
        heap_render();
    } else if (!strcmp(cmd, "fs")) {
        ramfs_render();
    } else if (!strcmp(cmd, "cat")) {
        const char *body = ramfs_read("/etc/motd");
        vga_write(body ? body : "missing"); vga_putc('\n');
    } else if (!strcmp(cmd, "write")) {
        ramfs_write("/notes/live", "created from command palette");
        vga_write("wrote /notes/live\n");
    } else if (!strcmp(cmd, "disk")) {
        storage_render();
    } else if (!strcmp(cmd, "net")) {
        net_render();
    } else if (!strcmp(cmd, "ping")) {
        net_ping_self();
        vga_write("loopback ping ok\n");
    } else if (!strcmp(cmd, "env")) {
        env_render();
    } else if (!strcmp(cmd, "sethost")) {
        env_set("hostname", "raven-lab");
        vga_write("hostname=raven-lab\n");
    } else if (!strcmp(cmd, "pkgs")) {
        package_render();
    } else if (!strcmp(cmd, "health")) {
        health_render();
    } else if (!strcmp(cmd, "metrics")) {
        metrics_render();
    } else if (!strcmp(cmd, "proc")) {
        procfs_render();
    } else if (!strcmp(cmd, "services")) {
        service_render();
    } else if (!strcmp(cmd, "restart")) {
        service_restart("shell");
        vga_write("shell service restart recorded\n");
    } else if (!strcmp(cmd, "security")) {
        security_render();
        vga_write("sample shell trigger panic => ");
        vga_write(security_check("shell", "trigger", "panic") ? "allow" : "deny");
        vga_putc('\n');
    } else if (!strcmp(cmd, "clock")) {
        clock_render();
    } else if (!strcmp(cmd, "consoles")) {
        console_render();
    } else if (!strcmp(cmd, "switchtty")) {
        console_switch("tty1");
        console_render();
    } else if (!strcmp(cmd, "manifest")) {
        manifest_render();
    } else if (!strcmp(cmd, "snapshot")) {
        snapshot_take();
        snapshot_render();
    } else if (!strcmp(cmd, "power")) {
        power_render();
    } else if (!strcmp(cmd, "reboot")) {
        power_request("reboot");
        vga_write("reboot request recorded, not executed in prototype\n");
    } else if (!strcmp(cmd, "shutdown")) {
        power_request("shutdown");
        vga_write("shutdown request recorded, not executed in prototype\n");
    } else if (!strcmp(cmd, "user")) {
        user_render();
    } else if (!strcmp(cmd, "run")) {
        user_run_demo();
    } else if (!strcmp(cmd, "abi")) {
        syscall_render();
    } else if (!strcmp(cmd, "install")) {
        installer_render();
        installer_plan();
    } else if (!strcmp(cmd, "sys")) {
        uint32_t t = syscall_dispatch(2,0,0,0);
        syscall_dispatch(1,0,0,0);
        vga_write("SYS_TICKS returned "); vga_write_dec(t); vga_putc('\n');
    } else if (!strcmp(cmd, "tasks")) {
        task_render();
    } else if (!strcmp(cmd, "spawn")) {
        uint32_t id = task_spawn("demo.worker", "ready");
        vga_write("spawned task id="); vga_write_dec(id); vga_putc('\n');
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
