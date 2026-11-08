#include "../kernel/kernel.h"
#include <assert.h>
#include <stdio.h>

static char output[200000];
static size_t output_len;
void vga_set_color(uint8_t a, uint8_t b) { (void)a; (void)b; }
void vga_putc(char c) {
    assert(output_len + 1 < sizeof(output));
    output[output_len++] = c; output[output_len] = 0;
}
void vga_write(const char *s) { while (*s) vga_putc(*s++); }
void vga_write_dec(uint32_t n) {
    char buf[12]; snprintf(buf, sizeof(buf), "%u", n); vga_write(buf);
}
static int contains(const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= output_len; i++)
        if (!strncmp(output + i, needle, n)) return 1;
    return 0;
}
static void clear_output(void) { output_len = 0; output[0] = 0; }

int main(void) {
    WyrdEvent e, snapshot;
    wyrd_init();
    assert(wyrd_count() == 0 && !wyrd_get(0, &e) && !wyrd_get(0, 0));
    char actor[] = "mutable actor", verb[] = "mutable verb";
    char object[] = "mutable command", result[] = "mutable result", note[] = "mutable note";
    wyrd_append(actor, verb, object, result, note);
    memset(actor, 'x', sizeof(actor)); memset(verb, 'x', sizeof(verb));
    memset(object, 'x', sizeof(object)); memset(result, 'x', sizeof(result));
    memset(note, 'x', sizeof(note));
    assert(wyrd_get(0, &e));
    assert(!strcmp(e.actor, "mutable actor") && !strcmp(e.verb, "mutable verb"));
    assert(!strcmp(e.object, "mutable command") && !strcmp(e.result, "mutable result"));
    assert(!strcmp(e.note, "mutable note") && !e.truncated);
    e.object[0] = 'X'; assert(wyrd_get(0, &e) && e.object[0] == 'm');
    assert(!wyrd_get(1, &e) && !wyrd_get(UINT32_MAX, &e));

    char exact_actor[WYRD_ACTOR_CAP], exact_verb[WYRD_VERB_CAP];
    char exact_object[WYRD_OBJECT_CAP], exact_result[WYRD_RESULT_CAP], exact_note[WYRD_NOTE_CAP];
    memset(exact_actor, 'a', sizeof(exact_actor)); exact_actor[sizeof(exact_actor)-1] = 0;
    memset(exact_verb, 'v', sizeof(exact_verb)); exact_verb[sizeof(exact_verb)-1] = 0;
    memset(exact_object, 'o', sizeof(exact_object)); exact_object[sizeof(exact_object)-1] = 0;
    memset(exact_result, 'r', sizeof(exact_result)); exact_result[sizeof(exact_result)-1] = 0;
    memset(exact_note, 'n', sizeof(exact_note)); exact_note[sizeof(exact_note)-1] = 0;
    wyrd_append(exact_actor, exact_verb, exact_object, exact_result, exact_note);
    assert(wyrd_get(1, &e) && !e.truncated);
    assert(!strcmp(e.actor, exact_actor) && !strcmp(e.verb, exact_verb));
    assert(!strcmp(e.object, exact_object) && strlen(e.object) == 159);
    assert(!strcmp(e.result, exact_result) && !strcmp(e.note, exact_note));

    char long_value[400]; memset(long_value, 'z', sizeof(long_value)); long_value[399] = 0;
    wyrd_append(long_value, long_value, long_value, long_value, long_value);
    assert(wyrd_get(2, &e) && e.truncated == 31);
    assert(strlen(e.actor) == WYRD_ACTOR_CAP-1 && strlen(e.verb) == WYRD_VERB_CAP-1);
    assert(strlen(e.object) == WYRD_OBJECT_CAP-1 && strlen(e.result) == WYRD_RESULT_CAP-1);
    assert(strlen(e.note) == WYRD_NOTE_CAP-1);
    clear_output(); wyrd_render(1);
    assert(contains(" [truncated:actor,verb,object,result,note]"));
    assert(!contains("mutable actor"));
    wyrd_append(long_value, "ok", long_value, "ok", "ok");
    clear_output(); wyrd_render(1); assert(contains(" [truncated:actor,object]"));
    wyrd_append(0, 0, 0, 0, 0); assert(wyrd_get(4, &e));
    assert(!e.actor[0] && !e.verb[0] && !e.object[0] && !e.result[0] && !e.note[0] && !e.truncated);

    /* First non-fitting byte, separately for every flag. Exact-fit was tested
     * above; do not infer boundary behavior from a much longer source. */
    const size_t capacities[] = {WYRD_ACTOR_CAP, WYRD_VERB_CAP, WYRD_OBJECT_CAP,
                                 WYRD_RESULT_CAP, WYRD_NOTE_CAP};
    for (uint32_t field = 0; field < 5; field++) {
        const char *values[] = {"a", "v", "o", "r", "n"};
        memset(long_value, 'b', sizeof(long_value)); long_value[capacities[field]] = 0;
        values[field] = long_value;
        wyrd_append(values[0], values[1], values[2], values[3], values[4]);
        assert(wyrd_get(5+field, &e) && e.truncated == (1u << field));
        const char *stored[] = {e.actor, e.verb, e.object, e.result, e.note};
        assert(strlen(stored[field]) == capacities[field]-1);
    }

    wyrd_init();
    char reusable[32];
    for (uint32_t i = 0; i < WYRD_CAP*4+17; i++) {
        snprintf(reusable, sizeof(reusable), "command-%u", i);
        wyrd_append("shell", "exec", reusable, "ok", "snapshot");
        if (i == WYRD_CAP-1) assert(wyrd_get(0, &snapshot));
    }
    memset(reusable, 'x', sizeof(reusable));
    assert(wyrd_count() == WYRD_CAP);
    assert(!strcmp(snapshot.object, "command-0"));
    for (uint32_t i = 0; i < WYRD_CAP; i++) {
        assert(wyrd_get(i, &e));
        uint32_t expected = WYRD_CAP*3+17+i;
        snprintf(reusable, sizeof(reusable), "command-%u", expected);
        assert(!strcmp(e.object, reusable) && e.tick == expected+1 && !e.truncated);
    }
    clear_output(); wyrd_render(0);
    assert(contains("command-785") && contains("command-1040") && !contains("command-784"));
    clear_output(); wyrd_render(2);
    assert(contains("command-1039") && contains("command-1040") && !contains("command-1038"));
    wyrd_init(); assert(wyrd_count() == 0 && !wyrd_get(0, &e));
    puts("wyrd tests: owned five-field snapshots, boundaries, truncation, render and 4x wrap passed");
    return 0;
}
