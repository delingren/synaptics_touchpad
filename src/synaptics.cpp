#include <Arduino.h>
#include "synaptics.h"

namespace synaptics {
// 4.2. TouchPad special command sequences
void special_command(uint8_t command) {
  uint8_t resolution;
  for (int i = 6; i >= 0; i -= 2) {
    resolution = (command >> i) & 0x03;
    ps2::ps2_command(PSMOUSE_CMD_SETRES, &resolution, nullptr);
  }
}

// 4.3. Mode byte
void set_mode(uint8_t mode) {
  ps2::disable();

  special_command(mode);

  uint8_t sample_rate = 0x14;
  ps2::ps2_command(PSMOUSE_CMD_SETRATE, &sample_rate, nullptr);

  ps2::enable();
}

// 4.4. Information queries
void status_request(uint8_t arg, uint8_t *result) {
  special_command(arg);
  ps2::ps2_command(PSMOUSE_CMD_GETINFO, nullptr, result);
}
} // namespace synaptics