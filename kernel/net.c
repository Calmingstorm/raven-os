#include "kernel.h"

typedef struct {
    uint8_t used;
    char name[24];
    char peer[32];
    uint32_t sent;
    uint32_t received;
    char last_payload[64];
} Socket;

#define SOCKET_CAP 8
static uint32_t packets_sent;
static uint32_t packets_recv;
static uint32_t loopback_up;
static Socket sockets[SOCKET_CAP];

void net_init(void) {
    packets_sent = packets_recv = 0;
    loopback_up = 1;
    memset(sockets, 0, sizeof(sockets));
    net_open_socket("shell.loop", "127.0.0.1");
    wyrd_append("net", "init", "loopback", "ok", "packet path scaffold online");
}

int net_open_socket(const char *name, const char *peer) {
    for (uint32_t i = 0; i < SOCKET_CAP; i++) {
        if (!sockets[i].used) {
            sockets[i].used = 1;
            strcopy(sockets[i].name, name ? name : "socket", sizeof(sockets[i].name));
            strcopy(sockets[i].peer, peer ? peer : "127.0.0.1", sizeof(sockets[i].peer));
            sockets[i].sent = sockets[i].received = 0;
            sockets[i].last_payload[0] = '\0';
            wyrd_append("net", "open", sockets[i].name, "ok", sockets[i].peer);
            return (int)(i + 1);
        }
    }
    wyrd_append("net", "open", name ? name : "socket", "fail", "socket table full");
    return 0;
}

int net_send(uint32_t id, const char *payload) {
    if (!id || id > SOCKET_CAP || !sockets[id - 1].used || !loopback_up) {
        wyrd_append("net", "send", "socket", "fail", "invalid socket or link down");
        return 0;
    }
    Socket *s = &sockets[id - 1];
    strcopy(s->last_payload, payload ? payload : "", sizeof(s->last_payload));
    s->sent++;
    s->received++;
    packets_sent++;
    packets_recv++;
    wyrd_append("net", "send", s->name, "ok", s->last_payload);
    return 1;
}

void net_ping_self(void) {
    int id = 1;
    if (!sockets[0].used) id = net_open_socket("ping.loop", "127.0.0.1");
    net_send((uint32_t)id, "icmp-ish echo");
    vga_write("loopback ping ok: tx="); vga_write_dec(packets_sent); vga_write(" rx="); vga_write_dec(packets_recv); vga_putc('\n');
}

void net_sockets_render(void) {
    vga_write("sockets\n");
    for (uint32_t i = 0; i < SOCKET_CAP; i++) {
        if (sockets[i].used) {
            vga_write("  #"); vga_write_dec(i + 1); vga_write(" "); vga_write(sockets[i].name);
            vga_write(" peer="); vga_write(sockets[i].peer);
            vga_write(" sent="); vga_write_dec(sockets[i].sent);
            vga_write(" recv="); vga_write_dec(sockets[i].received);
            vga_write(" last="); vga_write(sockets[i].last_payload); vga_putc('\n');
        }
    }
}

void net_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("network\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  loopback: "); vga_write(loopback_up ? "up" : "down"); vga_putc('\n');
    vga_write("  packets tx/rx: "); vga_write_dec(packets_sent); vga_write("/"); vga_write_dec(packets_recv); vga_putc('\n');
    net_sockets_render();
}
