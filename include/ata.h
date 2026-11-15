#ifndef ANVIL_ATA_H
#define ANVIL_ATA_H

#include "types.h"

bool ata_init(void);
bool ata_is_present(void);
uint32_t ata_sector_count(void);
const char* ata_model_name(void);
bool ata_read_sectors(uint32_t lba, uint8_t count, void* buf);
bool ata_write_sectors(uint32_t lba, uint8_t count, const void* buf);

#endif
