#include "kernel.h"

#define PCI_ADDRESS 0xCF8
#define PCI_DATA 0xCFC
#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY 0x2
#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0 0x10

static uint32_t address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return 0x80000000u | (uint32_t)bus << 16 | (uint32_t)(slot & 31) << 11 | (uint32_t)(func & 7) << 8 | (offset & 0xFC);
}

static uint32_t read_at(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_ADDRESS, address(bus, slot, func, offset));
    return inl(PCI_DATA);
}

uint32_t pci_read32(const PciDevice *dev, uint8_t offset) {
    return read_at(dev->bus, dev->slot, dev->func, offset);
}

void pci_write32(const PciDevice *dev, uint8_t offset, uint32_t value) {
    outl(PCI_ADDRESS, address(dev->bus, dev->slot, dev->func, offset));
    outl(PCI_DATA, value);
}

/* Brute-force scan of every bus and slot; functions 1-7 only on multifunction devices. */
int pci_find_class(uint8_t class_code, uint8_t subclass, uint32_t nth, PciDevice *out) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = read_at((uint8_t)bus, slot, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            uint8_t functions = (read_at((uint8_t)bus, slot, 0, PCI_HEADER_TYPE & 0xFC) >> 16 & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < functions; func++) {
                id = read_at((uint8_t)bus, slot, func, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                uint32_t class_reg = read_at((uint8_t)bus, slot, func, 0x08);
                if ((uint8_t)(class_reg >> 24) != class_code || (uint8_t)(class_reg >> 16) != subclass) continue;
                if (nth--) continue;
                out->bus = (uint8_t)bus;
                out->slot = slot;
                out->func = func;
                out->vendor = (uint16_t)id;
                out->device = (uint16_t)(id >> 16);
                out->class_code = class_code;
                out->subclass = subclass;
                return 1;
            }
        }
    }
    return 0;
}

/* Size a memory BAR the standard way, with memory decoding off while all-ones is written. A 64-bit BAR is
 * accepted only if its upper half is zero: this kernel has a 32-bit physical address space. */
int pci_memory_bar(const PciDevice *dev, uint8_t index, uint32_t *base, uint32_t *size) {
    if (index > 5) return 0;
    uint8_t offset = (uint8_t)(PCI_BAR0 + index * 4);
    uint32_t original = pci_read32(dev, offset);
    if (original & 1) return 0;                       /* I/O BAR */
    uint32_t type = (original >> 1) & 3;
    if (type == 1 || type == 3) return 0;             /* reserved */
    if (type == 2) {
        if (index == 5 || pci_read32(dev, (uint8_t)(offset + 4))) return 0;
    }
    uint32_t command = pci_read32(dev, PCI_COMMAND);
    /* The upper half is the status register: writing its ones back would clear them. */
    pci_write32(dev, PCI_COMMAND, (command & 0xFFFF) & ~(uint32_t)PCI_COMMAND_MEMORY);
    pci_write32(dev, offset, 0xFFFFFFFFu);
    uint32_t mask = pci_read32(dev, offset) & ~0xFu;
    pci_write32(dev, offset, original);
    /* Sizing is observational: the command register goes back exactly as it was. */
    pci_write32(dev, PCI_COMMAND, command & 0xFFFF);
    if (!mask) return 0;
    uint32_t bytes = ~mask + 1;
    uint32_t start = original & ~0xFu;
    if (!start || !bytes || start > 0xFFFFFFFFu - (bytes - 1)) return 0;
    *base = start;
    *size = bytes;
    return 1;
}

/* A device has to decode memory accesses before its BAR answers; firmware normally leaves this on. */
void pci_enable_memory(const PciDevice *dev) {
    uint32_t command = pci_read32(dev, PCI_COMMAND) & 0xFFFF;
    if (!(command & PCI_COMMAND_MEMORY)) pci_write32(dev, PCI_COMMAND, command | PCI_COMMAND_MEMORY);
}
