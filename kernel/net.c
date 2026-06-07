#include "kernel.h"

static uint8_t mac[6] = {0x52,0x41,0x56,0x45,0x4E,0x00};
static uint32_t tx_packets;
static uint32_t rx_packets;

void net_init(void) {
    tx_packets = rx_packets = 0;
    wyrd_append("network", "init", "loopback0", "ok", "network stack scaffold online");
}

void net_ping_self(void) {
    tx_packets++;
    rx_packets++;
    wyrd_append("network", "ping", "loopback0", "ok", "self-test packet round trip");
}

void net_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("network\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  device: loopback0 scaffold\n");
    vga_write("  mac: ");
    for (uint32_t i = 0; i < 6; i++) { vga_write_hex(mac[i]); if (i < 5) vga_putc(':'); }
    vga_putc('\n');
    vga_write("  tx="); vga_write_dec(tx_packets); vga_write(" rx="); vga_write_dec(rx_packets); vga_putc('\n');
    vga_write("  hardware NIC driver: pending, e1000 target documented\n");
}
