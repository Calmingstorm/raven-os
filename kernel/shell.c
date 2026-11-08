#include "kernel.h"

static char line[160];
static uint32_t line_len;
static uint32_t line_cursor;
#define HISTORY_CAP 32
static char history[HISTORY_CAP][sizeof(line)];
static uint32_t history_head, history_count, history_offset;
static char draft[sizeof(line)];
static uint32_t draft_cursor;
/* Logical cell offset from the serial prompt's origin. Keep the terminal out
 * of delayed-autowrap state, so offsets also describe its physical cursor. */
static uint32_t serial_cursor;

/* Keep completion names explicit: arguments are deliberately not completed. */
static const char *const commands[] = {
    "help", "clear", "about", "stats", "serial", "timeline", "log", "caps",
    "objects", "manifest", "boot", "mem", "alloc", "paging", "heap", "clock",
    "consoles", "switchtty", "fs", "ls", "cat", "edit", "append", "write", "rm",
    "disk", "fsck", "ata", "ata-read", "ata-write", "save", "load", "appenddisk",
    "sync", "deldisk", "format", "install", "install-run", "net", "ping", "sock",
    "send", "env", "sethost", "pkgs", "pkg-install", "pkg-remove", "health",
    "metrics", "proc", "services", "restart", "security", "power", "reboot",
    "shutdown", "user", "exec", "run", "abi", "rings", "procps", "ps-spawn",
    "sys", "tasks", "spawn", "kill", "snapshot", "panic-test", "debug-stage-journal", "gfx"
};

static void prompt(void) {
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_write("raven> ");
    serial_cursor = 7;
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_input_begin();
}

static const char *arg_after(const char *cmd, const char *prefix) {
    if (!strprefix(cmd, prefix)) return 0;
    const char *p = cmd + strlen(prefix);
    while (*p == ' ') p++;
    return p;
}

static void write_help(const char *topic) {
    if (!topic || !topic[0]) {
        vga_write("RavenOS commands (help <topic> for usage)\n"
                  "  system   about stats serial timeline log caps objects manifest\n"
                  "           env sethost health metrics proc clock consoles switchtty\n"
                  "           services restart security power reboot shutdown snapshot\n"
                  "  memory   boot mem alloc paging heap\n"
                  "  graphics gfx (640x400 test pattern) | gfx info\n"
                  "  files    fs ls cat edit append write rm\n"
                  "  disk     disk fsck ata ata-read ata-write save load appenddisk\n"
                  "           sync deldisk format install install-run debug-stage-journal\n"
                  "  net      net ping sock send\n"
                  "  runtime  pkgs pkg-install pkg-remove user exec run abi rings\n"
                  "           procps ps-spawn sys tasks spawn kill panic-test\n"
                  "  controls Editing, history, completion and scrollback keys\n"
                  "  help [topic] | clear (home screen)\n");
    } else if (!strcmp(topic, "system")) {
        vga_write("System and inspection\n"
                  "  about / stats / serial       Identity, counters, serial status\n"
                  "  timeline / log               Recent audit and service events\n"
                  "  caps / objects / manifest    Capability and object tables\n"
                  "  env / sethost                Environment / set hostname to raven-lab\n"
                  "  health / metrics / proc      Diagnostics and counters\n"
                  "  clock / consoles / switchtty Console status and switch to tty1\n"
                  "  services / restart           Services / record shell restart\n"
                  "  security / snapshot          Security sample / take snapshot\n"
                  "  power / reboot / shutdown    Power status / record requests only\n");
    } else if (!strcmp(topic, "memory")) {
        vga_write("Memory\n"
                  "  boot    Multiboot flags and validated memory map summary\n"
                  "  mem     Physical frame allocator status\n"
                  "  alloc   Allocate one frame (not automatically freed)\n"
                  "  paging  Identity map, heap and MMIO windows\n"
                  "  heap    Multi-page kernel heap status\n");
    } else if (!strcmp(topic, "graphics")) {
        vga_write("Graphics (Bochs VBE display: QEMU std-vga, v86)\n"
                  "  gfx       Switch to 640x400x32, draw a test pattern; any key returns\n"
                  "  gfx info  Display device, framebuffer and mode availability\n");
    } else if (!strcmp(topic, "files")) {
        vga_write("RAM files (changes are not persistent until saved or synced)\n"
                  "  fs                     Filesystem status\n"
                  "  ls [prefix]            List RAM and disk files\n"
                  "  cat [path]             Read file; default /etc/motd\n"
                  "  edit <path> <text>     Replace a RAM file\n"
                  "  append <path> <text>   Append a new line in RAM\n"
                  "  write                  Write sample /notes/live\n"
                  "  rm <path>              Delete a RAM file\n");
    } else if (!strcmp(topic, "disk")) {
        vga_write("Disk (scratch images only: these commands can destroy data)\n"
                  "  disk / fsck                    Status / integrity check\n"
                  "  ata / ata-read / ata-write     Probe / read or overwrite LBA 20\n"
                  "  save <path> / load <path>      Copy RAM to disk / disk to RAM\n"
                  "  appenddisk <path> <text>       Append a new line on disk\n"
                  "  sync / deldisk <path>          Save all RAM files / delete on disk\n"
                  "  format                         Reformat RavenFS\n"
                  "  install / install-run          Plan / execute prototype install\n"
                  "  debug-stage-journal <p> <text>  Stage recovery test transaction\n");
    } else if (!strcmp(topic, "net")) {
        vga_write("Prototype loopback network\n"
                  "  net                       Status\n"
                  "  ping                      Ping self\n"
                  "  sock                      Open a loopback socket\n"
                  "  send <socket-id> <text>    Send payload\n");
    } else if (!strcmp(topic, "runtime")) {
        vga_write("Prototype runtime\n"
                  "  pkgs / pkg-install <name> / pkg-remove <name>\n"
                  "  user / exec / run [name]    User and executable demos\n"
                  "  abi / rings / sys          ABI, descriptors and syscall demo\n"
                  "  procps / ps-spawn          Processes / spawn sample process\n"
                  "  tasks / spawn / kill <id>  Tasks / spawn worker / kill task\n"
                  "  panic-test                 Exercise denied panic capability\n");
    } else if (!strcmp(topic, "controls")) {
        vga_write("Console controls\n"
                  "  Left/Right, Home/End       Move within the input line\n"
                  "  Backspace / Delete        Remove before / under cursor\n"
                  "  Ctrl+U                    Clear the ENTIRE input line\n"
                  "  Up/Down                   32-command history; Down restores draft\n"
                  "  Tab                       Complete command name / list matches\n"
                  "  Shift+PgUp / Shift+PgDn    Browse 256 retained colored lines\n"
                  "  Typing or editing returns to live output\n"
                  "  Shift+Esc is reserved for the browser; no guest action\n");
    } else {
        /* Do not echo unbounded user input into a screen-bounded help page. */
        vga_write("Unknown help topic. Use help to list topics.\n");
    }
}

void shell_render_home(void) {
    vga_clear();
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("RavenOS " RAVEN_VERSION " - wyrd-log native kernel\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("One machine. One timeline. Fewer excuses.\n\n");
    wyrd_render(8);
    vga_write("\nType help or help controls. Shift+PgUp browses console history.\n\n");
    prompt();
}

void shell_init(void) {
    line_len = 0;
    line_cursor = 0;
    history_head = history_count = history_offset = 0;
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
    vga_write("keyboard_drops="); vga_write_dec(keyboard_dropped_events()); vga_putc('\n');
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

static void cmd_append(const char *rest, int disk_backed) {
    if (!rest || !rest[0]) { vga_write(disk_backed ? "usage: appenddisk <path> <text>\n" : "usage: append <path> <text>\n"); return; }
    char path[48];
    uint32_t i = 0;
    while (rest[i] && rest[i] != ' ' && i + 1 < sizeof(path)) { path[i] = rest[i]; i++; }
    path[i] = '\0';
    while (rest[i] == ' ') i++;
    if (!path[0] || !rest[i]) { vga_write(disk_backed ? "usage: appenddisk <path> <text>\n" : "usage: append <path> <text>\n"); return; }
    char suffix[180];
    suffix[0] = '\n';
    strcopy(suffix + 1, rest + i, sizeof(suffix) - 1);
    if (disk_backed) {
        if (storage_append_file(path, suffix)) { vga_write("appended disk "); vga_write(path); vga_putc('\n'); }
    } else {
        if (ramfs_append(path, suffix)) { vga_write("appended "); vga_write(path); vga_putc('\n'); }
    }
}

static void cmd_stage_journal(const char *rest) {
    if (!rest || !rest[0]) { vga_write("usage: debug-stage-journal <path> <text>\n"); return; }
    char path[64];
    uint32_t i = 0;
    while (rest[i] && rest[i] != ' ' && i + 1 < sizeof(path)) { path[i] = rest[i]; i++; }
    path[i] = '\0';
    const char *content = rest + i;
    while (*content == ' ') content++;
    if (!path[0] || !content[0]) { vga_write("usage: debug-stage-journal <path> <text>\n"); return; }
    if (storage_debug_stage_journal_save(path, content)) vga_write("staged ravenfs journal transaction\n");
    else vga_write("failed to stage ravenfs journal transaction\n");
}

static void cmd_save(const char *path) {
    if (!path || !path[0]) { vga_write("usage: save <path>\n"); return; }
    const char *content = ramfs_read(path);
    if (!content) { vga_write("ramfs file not found: "); vga_write(path); vga_putc('\n'); return; }
    if (storage_save_file(path, content)) { vga_write("saved "); vga_write(path); vga_write(" to ravenfs\n"); }
}

static void cmd_ls(const char *prefix) {
    const char *pfx = prefix && prefix[0] ? prefix : "/";
    ramfs_ls(pfx);
    storage_ls_prefix(pfx);
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
    if (!strcmp(cmd, "help")) write_help(0);
    else if ((arg = arg_after(cmd, "help "))) write_help(arg);
    else if (!strcmp(cmd, "serial")) serial_render();
    else if (!strcmp(cmd, "timeline")) wyrd_render(18);
    else if (!strcmp(cmd, "log")) log_render(20);
    else if (!strcmp(cmd, "caps")) caps_render();
    else if (!strcmp(cmd, "objects")) objects_render();
    else if (!strcmp(cmd, "boot")) bootinfo_render();
    else if (!strcmp(cmd, "mem")) pmm_render();
    else if (!strcmp(cmd, "alloc")) { uint32_t frame = pmm_alloc_frame(); vga_write("allocated frame: "); vga_write_hex(frame); vga_putc('\n'); }
    else if (!strcmp(cmd, "paging")) paging_render();
    else if (!strcmp(cmd, "heap")) heap_render();
    else if (!strcmp(cmd, "gfx")) gfx_command();
    else if (!strcmp(cmd, "gfx info")) gfx_render_info();
    else if (!strcmp(cmd, "fs")) ramfs_render();
    else if (!strcmp(cmd, "ls")) cmd_ls("/");
    else if ((arg = arg_after(cmd, "ls "))) cmd_ls(arg);
    else if (!strcmp(cmd, "cat")) cmd_cat("/etc/motd");
    else if ((arg = arg_after(cmd, "cat "))) cmd_cat(arg);
    else if (!strcmp(cmd, "write")) { ramfs_write("/notes/live", "written through shell command"); vga_write("wrote /notes/live\n"); }
    else if ((arg = arg_after(cmd, "edit "))) cmd_edit(arg);
    else if ((arg = arg_after(cmd, "append "))) cmd_append(arg, 0);
    else if ((arg = arg_after(cmd, "rm "))) { vga_write(ramfs_delete(arg) ? "deleted\n" : "not found in ramfs\n"); }
    else if (!strcmp(cmd, "disk")) storage_render();
    else if (!strcmp(cmd, "fsck")) storage_fsck();
    else if ((arg = arg_after(cmd, "debug-stage-journal "))) cmd_stage_journal(arg);
    else if (!strcmp(cmd, "ata")) ata_render();
    else if (!strcmp(cmd, "ata-read")) { uint8_t b[SECTOR_SIZE]; if (ata_read_sector(20, b)) { vga_write("ata lba20: "); for (uint32_t i=0;i<48 && b[i];i++) vga_putc((char)b[i]); vga_putc('\n'); } else vga_write("ata read failed\n"); }
    else if (!strcmp(cmd, "ata-write")) { uint8_t b[SECTOR_SIZE]; memset(b, 0, sizeof(b)); strcopy((char *)b, "RavenOS ATA PIO write proof", sizeof(b)); vga_write(ata_write_sector(20, b) ? "ata lba20 written\n" : "ata write failed\n"); }
    else if ((arg = arg_after(cmd, "save "))) cmd_save(arg);
    else if (!strcmp(cmd, "sync")) { storage_sync_ramfs(); }
    else if ((arg = arg_after(cmd, "load "))) cmd_load(arg);
    else if ((arg = arg_after(cmd, "appenddisk "))) cmd_append(arg, 1);
    else if ((arg = arg_after(cmd, "deldisk "))) { vga_write(storage_delete_file(arg) ? "deleted from ravenfs\n" : "not found on ravenfs\n"); }
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
    else if ((arg = arg_after(cmd, "pkg-remove "))) { vga_write(package_remove(arg) ? "removed\n" : "remove failed\n"); }
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
    else if (!strcmp(cmd, "procps")) process_render();
    else if (!strcmp(cmd, "ps-spawn")) { uint32_t pid = process_spawn("shell.child", 0x00404000); vga_write("spawned pid="); vga_write_dec(pid); vga_putc('\n'); }
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

static void serial_normalize_wrap(uint32_t offset) {
    /* At the right margin most terminals defer wrapping until the next glyph.
     * Resolve that state now, including scrolling at the bottom of the screen. */
    if (offset && offset % VGA_WIDTH == 0) serial_write("\r\n");
}

static void serial_redraw_at(uint32_t cursor) {
    /* CR alone addresses the current wrapped row, not the prompt's first row.
     * Return to the origin, erase the old extent, and redraw all wrapped rows. */
    serial_putc('\r');
    for (uint32_t i = 0; i < serial_cursor / VGA_WIDTH; i++) serial_write("\033[A");
    serial_write("\033[Jraven> ");
    serial_write(line);
    uint32_t end = 7 + line_len, target = 7 + cursor;
    serial_normalize_wrap(end);
    serial_putc('\r');
    for (uint32_t i = target / VGA_WIDTH; i < end / VGA_WIDTH; i++) serial_write("\033[A");
    for (uint32_t i = 0; i < target % VGA_WIDTH; i++) serial_write("\033[C");
    serial_cursor = target;
}

static void serial_redraw(void) { serial_redraw_at(line_cursor); }

static void serial_append(char c) {
    serial_putc(c);
    serial_cursor = 7 + line_len;
    serial_normalize_wrap(serial_cursor);
}

static void redraw(void) {
    vga_input_redraw(line, line_len, line_cursor);
    serial_redraw();
}

static void remember(void) {
    if (!line_len) return;
    if (history_count && !strcmp(history[(history_head + HISTORY_CAP - 1) % HISTORY_CAP], line)) return;
    strcopy(history[history_head], line, sizeof(line));
    history_head = (history_head + 1) % HISTORY_CAP;
    if (history_count < HISTORY_CAP) history_count++;
}

static void recall(int older) {
    if (older) {
        if (history_offset == history_count) return;
        if (!history_offset) { strcopy(draft, line, sizeof(draft)); draft_cursor = line_cursor; }
        history_offset++;
    } else {
        if (!history_offset) return;
        history_offset--;
    }
    if (history_offset) {
        strcopy(line, history[(history_head + HISTORY_CAP - history_offset) % HISTORY_CAP], sizeof(line));
        line_len = line_cursor = (uint32_t)strlen(line);
    } else {
        strcopy(line, draft, sizeof(line));
        line_len = (uint32_t)strlen(line);
        line_cursor = draft_cursor;
    }
    redraw();
}

static void complete(void) {
    /* Completion is only for a command token with the cursor at its end. */
    if (!line_len || line_cursor != line_len) return;
    for (uint32_t i = 0; i < line_len; i++) if (line[i] == ' ') return;
    uint32_t matches = 0, common = 0;
    const char *first = 0;
    for (uint32_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strncmp(commands[i], line, line_len)) continue;
        if (!matches++) { first = commands[i]; common = (uint32_t)strlen(first); }
        else {
            uint32_t j = 0;
            while (j < common && first[j] == commands[i][j]) j++;
            common = j;
        }
    }
    if (!matches) return;
    if (common > line_len || matches == 1) {
        strcopy(line, first, common + 1);
        line_len = line_cursor = common;
        redraw();
        return;
    }
    vga_input_end();
    vga_putc('\n');
    uint32_t width = 0;
    for (uint32_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strncmp(commands[i], line, line_len)) continue;
        uint32_t len = (uint32_t)strlen(commands[i]);
        if (width && width + len + 2 > VGA_WIDTH - 1) { vga_putc('\n'); width = 0; }
        if (width) { vga_write("  "); width += 2; }
        vga_write(commands[i]); width += len;
    }
    vga_putc('\n');
    prompt();
    redraw();
}

static void shell_accept_event(const KeyEvent *key) {
    if (!key->pressed) return;
    if ((key->modifiers & KEY_MOD_SHIFT) &&
        (key->code == KEY_PAGEUP || key->code == KEY_PAGEDOWN)) {
        vga_scrollback(key->code == KEY_PAGEUP ? VGA_HEIGHT - 1 : -(VGA_HEIGHT - 1));
        return;
    }
    /* Shift+Esc has no guest meaning. Neither do bare modifiers or Alt chords. */
    if (key->code == KEY_ESCAPE || key->code >= KEY_SHIFT || key->code == KEY_NONE ||
        (key->modifiers & KEY_MOD_ALT)) return;
    if (key->modifiers & KEY_MOD_CTRL) {
        if (key->code != KEY_CHARACTER || (key->ascii != 'u' && key->ascii != 'U')) return;
        vga_live();
        line_len = line_cursor = history_offset = 0;
        line[0] = '\0';
        redraw();
        return;
    }
    if (key->code == KEY_PAGEUP || key->code == KEY_PAGEDOWN) return;
    vga_live();
    switch (key->code) {
    case KEY_ENTER:
        if (serial_cursor != 7 + line_len) serial_redraw_at(line_len);
        vga_input_end();
        vga_putc('\n');
        remember();
        /* clear renders its own home prompt. */
        int cleared = !strcmp(line, "clear");
        shell_execute(line);
        line_len = line_cursor = history_offset = 0;
        line[0] = '\0';
        if (!cleared) prompt();
        break;
    case KEY_BACKSPACE:
        if (!line_cursor) break;
        for (uint32_t i = line_cursor; i <= line_len; i++) line[i - 1] = line[i];
        line_cursor--; line_len--; redraw();
        break;
    case KEY_DELETE:
        if (line_cursor == line_len) break;
        for (uint32_t i = line_cursor; i < line_len; i++) line[i] = line[i + 1];
        line_len--; redraw();
        break;
    case KEY_LEFT: if (line_cursor) { line_cursor--; redraw(); } break;
    case KEY_RIGHT: if (line_cursor < line_len) { line_cursor++; redraw(); } break;
    case KEY_HOME: line_cursor = 0; redraw(); break;
    case KEY_END: line_cursor = line_len; redraw(); break;
    case KEY_UP: recall(1); break;
    case KEY_DOWN: recall(0); break;
    case KEY_TAB: complete(); break;
    case KEY_CHARACTER:
        if (key->ascii < 32 || key->ascii > 126 || line_len + 1 >= sizeof(line)) break;
        for (uint32_t i = line_len + 1; i > line_cursor; i--) line[i] = line[i - 1];
        line[line_cursor++] = key->ascii;
        line_len++;
        vga_input_redraw(line, line_len, line_cursor);
        if (line_cursor == line_len) serial_append(key->ascii);
        else serial_redraw();
        break;
    default: break;
    }
}

static void shell_accept_char(char c) {
    KeyEvent key = { .code = KEY_CHARACTER, .ascii = c, .pressed = 1 };
    if (c == '\n' || c == '\r') key.code = KEY_ENTER;
    else if (c == '\b' || c == 127) key.code = KEY_BACKSPACE;
    else if (c == '\t') key.code = KEY_TAB;
    else if (c == 21) { key.ascii = 'u'; key.modifiers = KEY_MOD_CTRL; }
    shell_accept_event(&key);
}

void shell_tick(void) {
    KeyEvent key;
    while (keyboard_pop(&key)) shell_accept_event(&key);
    char c;
    while (serial_read_char(&c)) shell_accept_char(c);
}
