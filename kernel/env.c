#include "kernel.h"

typedef struct {
    const char *key;
    char value[64];
    uint8_t used;
} EnvVar;

#define ENV_CAP 16
static EnvVar env[ENV_CAP];
static uint32_t env_len;

static EnvVar *find_env(const char *key) {
    for (uint32_t i = 0; i < env_len; i++) {
        if (env[i].used && !strcmp(env[i].key, key)) return &env[i];
    }
    return 0;
}

static void add_env(const char *key, const char *value) {
    if (env_len >= ENV_CAP) return;
    env[env_len].key = key;
    strcopy(env[env_len].value, value, sizeof(env[env_len].value));
    env[env_len].used = 1;
    env_len++;
}

void env_init(void) {
    env_len = 0;
    add_env("hostname", "raven");
    add_env("user", "root");
    add_env("path", "/bin:/sys:/etc");
    add_env("mode", "prototype");
    wyrd_append("env", "init", "table", "ok", "runtime settings loaded");
}

const char *env_get(const char *key) {
    EnvVar *v = find_env(key);
    return v ? v->value : 0;
}

int env_set(const char *key, const char *value) {
    EnvVar *v = find_env(key);
    if (v) {
        strcopy(v->value, value, sizeof(v->value));
        wyrd_append("env", "set", key, "ok", "runtime setting updated");
        return 1;
    }
    if (env_len >= ENV_CAP) {
        wyrd_append("env", "set", key, "fail", "environment table full");
        return 0;
    }
    env[env_len].key = key;
    strcopy(env[env_len].value, value, sizeof(env[env_len].value));
    env[env_len].used = 1;
    env_len++;
    wyrd_append("env", "create", key, "ok", "runtime setting created");
    return 1;
}

void env_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("environment\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < env_len; i++) {
        if (!env[i].used) continue;
        vga_write("  "); vga_write(env[i].key); vga_write("="); vga_write(env[i].value); vga_putc('\n');
    }
}
