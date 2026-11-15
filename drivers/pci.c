#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static pci_device_t pci_devs[MAX_PCI_DEVICES];
static int pci_count = 0;

uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

void pci_enable_busmaster(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = pci_cfg_read32(bus, slot, func, 0x04);
    cmd |= 0x0007; /* I/O Space + Memory Space + Bus Master */
    pci_cfg_write32(bus, slot, func, 0x04, cmd);
}

void pci_init(void) {
    pci_count = 0;
    for (uint8_t bus = 0; bus < 8 && pci_count < MAX_PCI_DEVICES; bus++) {
        for (uint8_t slot = 0; slot < 32 && pci_count < MAX_PCI_DEVICES; slot++) {
            for (uint8_t func = 0; func < 8 && pci_count < MAX_PCI_DEVICES; func++) {
                uint32_t id = pci_cfg_read32(bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)(id & 0xFFFF);
                if (vendor == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }
                uint16_t device = (uint16_t)(id >> 16);
                uint32_t cls_reg = pci_cfg_read32(bus, slot, func, 0x08);
                uint32_t hdr_reg = pci_cfg_read32(bus, slot, func, 0x0C);
                uint32_t irq_reg = pci_cfg_read32(bus, slot, func, 0x3C);

                pci_device_t* d = &pci_devs[pci_count++];
                d->bus        = bus;
                d->slot       = slot;
                d->func       = func;
                d->vendor_id  = vendor;
                d->device_id  = device;
                d->class_code = (uint8_t)(cls_reg >> 24);
                d->subclass   = (uint8_t)((cls_reg >> 16) & 0xFF);
                d->prog_if    = (uint8_t)((cls_reg >> 8) & 0xFF);
                for (int b = 0; b < 6; b++) {
                    d->bar[b] = pci_cfg_read32(bus, slot, func, (uint8_t)(0x10 + b * 4));
                }
                d->bar0     = d->bar[0];
                d->irq_line = (uint8_t)(irq_reg & 0xFF);

                if (func == 0 && !((hdr_reg >> 16) & 0x80)) break;
            }
        }
    }
}

int pci_device_count(void) { return pci_count; }

const pci_device_t* pci_get_device(int idx) {
    if (idx < 0 || idx >= pci_count) return NULL;
    return &pci_devs[idx];
}

const char* pci_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
        case 0x8086: return "Intel";
        case 0x1002: return "AMD/ATI";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x10EC: return "Realtek";
        case 0x14E4: return "Broadcom";
        case 0x168C: return "Qualcomm Atheros";
        case 0x17CB: return "Qualcomm";
        case 0x1969: return "Qualcomm Atheros (Attansic)";
        case 0x14C3: return "MediaTek";
        case 0x1814: return "Ralink";
        case 0x11AB: return "Marvell";
        case 0x1D6A: return "Aquantia";
        case 0x15B3: return "Mellanox";
        case 0x1AF4: return "Red Hat (VirtIO)";
        case 0x1234: return "QEMU/Bochs";
        case 0x1B36: return "Red Hat (QEMU)";
        case 0x80EE: return "Oracle VirtualBox";
        case 0x15AD: return "VMware";
        case 0x10B7: return "3Com";
        case 0x1106: return "VIA";
        case 0x1039: return "SiS";
        case 0x1013: return "Cirrus Logic";
        case 0x1274: return "Ensoniq";
        default:     return "Unknown Vendor";
    }
}

const char* pci_class_name(uint8_t cls, uint8_t sub) {
    switch (cls) {
        case 0x01:
            if (sub == 0x01) return "IDE Storage Controller";
            if (sub == 0x06) return "SATA AHCI Controller";
            if (sub == 0x08) return "NVMe Storage Controller";
            return "Mass Storage Controller";
        case 0x02:
            if (sub == 0x00) return "Ethernet Controller";
            if (sub == 0x80) return "Wireless Network Controller";
            return "Network Controller";
        case 0x03:
            if (sub == 0x00) return "VGA Display Adapter";
            if (sub == 0x02) return "3D GPU Controller";
            return "Display Controller";
        case 0x04:
            if (sub == 0x01 || sub == 0x03) return "Audio / HDA Controller";
            return "Multimedia Device";
        case 0x06:
            if (sub == 0x00) return "Host/PCI Bridge";
            if (sub == 0x01) return "ISA/LPC Bridge";
            if (sub == 0x04) return "PCI-to-PCI Bridge";
            if (sub == 0x80) return "ACPI / PM Bridge";
            return "PCI Bridge";
        case 0x0C:
            if (sub == 0x03) return "USB Host Controller";
            if (sub == 0x05) return "SMBus Controller";
            return "Serial Bus Controller";
        case 0x0D: return "Wireless RF Controller";
        default:   return "PCI Peripheral";
    }
}
