#ifndef SYNAPTICS_H
#define SYNAPTICS_H

#include "ps2.h"

namespace synaptics {
void set_mode(uint8_t mode);
void status_request(uint8_t arg, uint8_t *result);
} // namespace synaptics

#endif