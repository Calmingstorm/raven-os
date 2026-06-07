#include "kernel.h"

void metrics_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("metrics\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  raven_ticks "); vga_write_dec(timer_ticks()); vga_putc('\n');
    vga_write("  raven_wyrd_events "); vga_write_dec(wyrd_count()); vga_putc('\n');
    vga_write("  raven_pmm_total_frames "); vga_write_dec(pmm_total_frames()); vga_putc('\n');
    vga_write("  raven_pmm_free_frames "); vga_write_dec(pmm_free_frames()); vga_putc('\n');
    vga_write("  raven_ramfs_files "); vga_write_dec(ramfs_count()); vga_putc('\n');
    vga_write("  raven_ramfs_bytes "); vga_write_dec(ramfs_bytes_used()); vga_putc('\n');
    vga_write("  raven_storage_reads "); vga_write_dec(storage_reads()); vga_putc('\n');
    vga_write("  raven_storage_writes "); vga_write_dec(storage_writes()); vga_putc('\n');
    vga_write("  raven_task_switches "); vga_write_dec(task_switches()); vga_putc('\n');
    vga_write("  raven_heap_bytes tracked via heap command\n");
}
