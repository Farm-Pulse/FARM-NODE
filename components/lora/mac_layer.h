#ifndef MAC_LAYER_H
#define MAC_LAYER_H

#include <stdbool.h>
#include "farmpulse_defs.h"

// Initialize the LoRa hardware
void mac_init(void);

// Send a packet immediately (Low level)
bool mac_tx(farm_packet_t *packet);

#endif