#include "kernel.h"

static char keybuf[KEYBUF_CAP];
static uint32_t key_head, key_tail;
static uint8_t shift_down;

static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'', '`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};
static const char shifted_map[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};

static void push_key(char c) {
    uint32_t next = (key_head + 1) % KEYBUF_CAP;
    if (next == key_tail) return;
    keybuf[key_head] = c;
    key_head = next;
}

void keyboard_init(void) {
    key_head = key_tail = 0;
    shift_down = 0;
    wyrd_append("driver.keyboard", "init", "ps2", "ok", "irq1 line enabled");
}

int keyboard_pop(char *out) {
    if (key_tail == key_head) return 0;
    *out = keybuf[key_tail];
    key_tail = (key_tail + 1) % KEYBUF_CAP;
    return 1;
}

void keyboard_on_irq(void) {
    uint8_t sc = inb(0x60);
    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_down = 0; return; }
    if (sc & 0x80) return;
    char c = shift_down ? shifted_map[sc] : scancode_map[sc];
    if (c) push_key(c);
}
