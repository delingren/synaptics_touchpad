#ifndef SYNAPTICS_H
#define SYNAPTICS_H

#include "ps2.h"

namespace synaptics {
void special_command(uint8_t command);
void status_request(uint8_t arg, uint8_t *result);
void set_mode();
} // namespace synaptics

#endif