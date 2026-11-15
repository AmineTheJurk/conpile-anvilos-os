#include "acpi.h"
#include "io.h"
#include "mem.h"
#include "serial.h"
#include "string.h"

typedef struct __attribute__((packed)) {
    char     sig[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
} rsdp_t;

typedef struct __attribute__((packed)) {
    char     sig[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_rev;
    uint32_t creator_id;
    uint32_t creator_rev;
} acpi_sdt_hdr_t;

typedef struct __attribute__((packed)) {
    acpi_sdt_hdr_t hdr;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
} acpi_fadt_t;

static rsdp_t* find_rsdp(void) {
    /* Search standard BIOS ROM area 0xE0000 .. 0xFFFFF */
    for (uint32_t addr = 0x000E0000; addr < 0x00100000; addr += 16) {
        const char* p = (const char*)addr;
        if (memcmp(p, "RSD PTR ", 8) == 0) {
            uint8_t sum = 0;
            for (int i = 0; i < 20; i++) sum += ((const uint8_t*)p)[i];
            if (sum == 0) return (rsdp_t*)addr;
        }
    }
    return NULL;
}

static void try_acpi_fadt_poweroff(void) {
    rsdp_t* rsdp = find_rsdp();
    if (!rsdp || !rsdp->rsdt_addr) return;

    vmm_map_region(rsdp->rsdt_addr, rsdp->rsdt_addr, 65536, 0x03);
    acpi_sdt_hdr_t* rsdt = (acpi_sdt_hdr_t*)rsdp->rsdt_addr;
    if (memcmp(rsdt->sig, "RSDT", 4) != 0 || rsdt->length < sizeof(*rsdt)) return;

    uint32_t entries = (rsdt->length - sizeof(*rsdt)) / 4;
    uint32_t* ptrs = (uint32_t*)((uint8_t*)rsdt + sizeof(*rsdt));

    for (uint32_t i = 0; i < entries && i < 32; i++) {
        uint32_t taddr = ptrs[i];
        if (!taddr) continue;
        vmm_map_region(taddr, taddr, 65536, 0x03);
        acpi_sdt_hdr_t* tbl = (acpi_sdt_hdr_t*)taddr;
        if (memcmp(tbl->sig, "FACP", 4) == 0 && tbl->length >= sizeof(acpi_fadt_t)) {
            acpi_fadt_t* fadt = (acpi_fadt_t*)tbl;
            if (fadt->smi_cmd && fadt->acpi_enable) {
                outb((uint16_t)fadt->smi_cmd, fadt->acpi_enable);
                for (volatile int k = 0; k < 100000; k++) {}
            }
            if (fadt->dsdt) {
                vmm_map_region(fadt->dsdt, fadt->dsdt, 131072, 0x03);
                acpi_sdt_hdr_t* dsdt = (acpi_sdt_hdr_t*)fadt->dsdt;
                if (memcmp(dsdt->sig, "DSDT", 4) == 0 && dsdt->length > sizeof(*dsdt)) {
                    uint8_t* aml = (uint8_t*)dsdt + sizeof(*dsdt);
                    uint32_t len = dsdt->length - sizeof(*dsdt);
                    for (uint32_t j = 0; j + 8 < len; j++) {
                        if (memcmp(aml + j, "_S5_", 4) == 0) {
                            uint8_t* p = aml + j + 4;
                            uint8_t slp_typa = 0, slp_typb = 0;
                            if (p[0] == 0x12) { /* PackageOp */
                                p += 2; /* skip pkg length byte */
                                if ((p[-1] & 0xC0) != 0) p++;
                                p++;    /* element count */
                                if (p[0] == 0x0A) { slp_typa = p[1]; p += 2; }
                                else if (p[0] == 0x00 || p[0] == 0x01) { slp_typa = p[0]; p += 1; }
                                if (p[0] == 0x0A) { slp_typb = p[1]; }
                                else if (p[0] == 0x00 || p[0] == 0x01) { slp_typb = p[0]; }

                                if (fadt->pm1a_cnt_blk) {
                                    outw((uint16_t)fadt->pm1a_cnt_blk,
                                         (uint16_t)(((slp_typa & 7) << 10) | (1 << 13)));
                                }
                                if (fadt->pm1b_cnt_blk) {
                                    outw((uint16_t)fadt->pm1b_cnt_blk,
                                         (uint16_t)(((slp_typb & 7) << 10) | (1 << 13)));
                                }
                            }
                            break;
                        }
                    }
                }
            }
            break;
        }
    }
}

void acpi_shutdown(void) {
    serial_puts("[ACPI] Shutting down system...\n");
    cli();

    /* 1. Try native motherboard ACPI FADT/DSDT _S5_ poweroff */
    try_acpi_fadt_poweroff();

    /* 2. Hypervisor / standard PIIX4 / ICH / VirtualBox ACPI poweroff ports */
    outw(0x604,  0x2000); /* QEMU PIIX4 / i440FX / Q35 */
    outw(0xB004, 0x2000); /* Bochs / older QEMU */
    outw(0x4004, 0x3400); /* VirtualBox */
    outw(0x0600, 0x0034); /* Cloud Hypervisor / KVM */

    /* Halt permanently if hardware requires manual power-off */
    serial_puts("[ACPI] Safe to power off.\n");
    for (;;) {
        cli();
        hlt();
    }
}

void acpi_reboot(void) {
    serial_puts("[ACPI] Rebooting system...\n");
    cli();
    outb(0x64, 0xFE);  /* PS/2 8042 reset */
    outb(0xCF9, 0x06); /* PCI reset */
    for (;;) hlt();
}
