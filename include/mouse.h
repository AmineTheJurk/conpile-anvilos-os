#ifndef ANVIL_MOUSE_H
#define ANVIL_MOUSE_H

#include "types.h"

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t buttons; /* bit 0 = left, bit 1 = right, bit 2 = middle */
    uint8_t clicked; /* latched rising-edge clicks since last poll */
    bool    moved;
} mouse_state_t;

void mouse_init(int32_t max_w, int32_t max_h);
void mouse_get_state(mouse_state_t* out);

#endif
