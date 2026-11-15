#include "ata.h"
#include "io.h"
#include "string.h"

#define ATA_IO_BASE   0x1F0
#define ATA_CTRL_BASE 0x3F6

static bool     ata_present = false;
static uint32_t ata_sectors = 0;
static char     ata_model[41];

static bool ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < 200000; i++) {
        uint8_t s = inb(ATA_IO_BASE + 7);
        if (s == 0xFF) return false;
        if (!(s & 0x80)) return true;
    }
    return false;
}

static bool ata_wait_drq(void) {
    for (uint32_t i = 0; i < 200000; i++) {
        uint8_t s = inb(ATA_IO_BASE + 7);
        if (s & 0x01) return false; /* ERR */
        if (s & 0x20) return false; /* DF */
        if (!(s & 0x80) && (s & 0x08)) return true;
    }
    return false;
}

bool ata_init(void) {
    ata_present = false;
    ata_sectors = 0;
    strcpy(ata_model, "None");

    /* Floating bus check */
    if (inb(ATA_IO_BASE + 7) == 0xFF) {
        return false;
    }

    outb(ATA_IO_BASE + 6, 0xA0); /* Select primary master */
    io_wait();
    outb(ATA_IO_BASE + 2, 0);
    outb(ATA_IO_BASE + 3, 0);
    outb(ATA_IO_BASE + 4, 0);
    outb(ATA_IO_BASE + 5, 0);
    outb(ATA_IO_BASE + 7, 0xEC); /* IDENTIFY */
    io_wait();

    uint8_t status = inb(ATA_IO_BASE + 7);
    if (status == 0 || status == 0xFF) return false;
    if (!ata_wait_not_busy()) return false;

    /* Check if ATAPI/SATA non-ATA signature */
    if (inb(ATA_IO_BASE + 4) != 0 || inb(ATA_IO_BASE + 5) != 0) {
        return false;
    }

    if (!ata_wait_drq()) return false;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) {
        id[i] = inw(ATA_IO_BASE + 0);
    }

    ata_sectors = ((uint32_t)id[61] << 16) | id[60];
    for (int i = 0; i < 20; i++) {
        ata_model[i * 2]     = (char)(id[27 + i] >> 8);
        ata_model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    ata_model[40] = '\0';
    for (int i = 39; i >= 0 && ata_model[i] == ' '; i--) ata_model[i] = '\0';

    ata_present = (ata_sectors > 0);
    return ata_present;
}

bool ata_is_present(void) { return ata_present; }
uint32_t ata_sector_count(void) { return ata_sectors; }
const char* ata_model_name(void) { return ata_model; }

bool ata_read_sectors(uint32_t lba, uint8_t count, void* buf) {
    if (!ata_present || count == 0) return false;
    if (!ata_wait_not_busy()) return false;

    outb(ATA_IO_BASE + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_IO_BASE + 2, count);
    outb(ATA_IO_BASE + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_IO_BASE + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_IO_BASE + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_IO_BASE + 7, 0x20); /* READ SECTORS */

    uint16_t* dst = (uint16_t*)buf;
    for (uint8_t s = 0; s < count; s++) {
        if (!ata_wait_drq()) return false;
        for (int i = 0; i < 256; i++) {
            dst[s * 256 + i] = inw(ATA_IO_BASE + 0);
        }
    }
    return true;
}

bool ata_write_sectors(uint32_t lba, uint8_t count, const void* buf) {
    if (!ata_present || count == 0) return false;
    if (!ata_wait_not_busy()) return false;

    outb(ATA_IO_BASE + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_IO_BASE + 2, count);
    outb(ATA_IO_BASE + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_IO_BASE + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_IO_BASE + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_IO_BASE + 7, 0x30); /* WRITE SECTORS */

    const uint16_t* src = (const uint16_t*)buf;
    for (uint8_t s = 0; s < count; s++) {
        if (!ata_wait_drq()) return false;
        for (int i = 0; i < 256; i++) {
            outw(ATA_IO_BASE + 0, src[s * 256 + i]);
        }
        outb(ATA_IO_BASE + 7, 0xE7); /* CACHE FLUSH */
        ata_wait_not_busy();
    }
    return true;
}
