#ifndef ANVIL_PCI_H
#define ANVIL_PCI_H

#include "types.h"

#define MAX_PCI_DEVICES 48

typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq_line;
    uint32_t bar[6];
    uint32_t bar0; /* alias for bar[0] */
} pci_device_t;

void     pci_init(void);
int      pci_device_count(void);
const pci_device_t* pci_get_device(int idx);
const char* pci_class_name(uint8_t cls, uint8_t sub);
const char* pci_vendor_name(uint16_t vendor_id);

uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void     pci_enable_busmaster(uint8_t bus, uint8_t slot, uint8_t func);

#endif
