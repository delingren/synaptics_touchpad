#include <Arduino.h>
#include "synaptics.h"

namespace synaptics {
void special_command(uint8_t command) {
  // Reference: 4.2. TouchPad special command sequences
  uint8_t resolution;
  for (int i = 6; i >= 0; i -= 2) {
    resolution = (command >> i) & 0x03;
    ps2::ps2_command(PSMOUSE_CMD_SETRES, &resolution, nullptr);
  }
}

void status_request(uint8_t arg, uint8_t *result) {
  // Reference: 4.4. Information queries
  special_command(arg);
  ps2::ps2_command(PSMOUSE_CMD_GETINFO, nullptr, result);
}

void set_mode() {
  // Reference: 4.3. Mode byte
  // Somehow, I couldn't get the touchpad to report extended W mode packets.
  // After some research, I found the solution in VoodooPS2 driver (Touchpad
  // driver for Hackintosh).
  // This sequence sets absolute mode, high rate, W mode, and EW mode.
  //  F5
  //  E6, E6, E8, 03, E8, 00, E8, 01, E8, 01, F3, 14
  //  E6, E6, E8, 00, E8, 00, E8, 00, E8, 03, F3, C8
  //  F4
  // https://github.com/acidanthera/VoodooPS2/blob/8e05d4f97bd0d3fa9066040c50a7ab99a0c60f65/VoodooPS2Trackpad/VoodooPS2SynapticsTouchPad.cpp#L1655

  uint8_t sample_rate = 0x14;

  ps2::disable();

  ps2::ps2_command(PSMOUSE_CMD_SETSCALE11, nullptr, nullptr);
  ps2::ps2_command(PSMOUSE_CMD_SETSCALE11, nullptr, nullptr);
  synaptics::special_command(0xC5);
  sample_rate = 0x14;
  ps2::ps2_command(PSMOUSE_CMD_SETRATE, &sample_rate, nullptr);

  ps2::ps2_command(PSMOUSE_CMD_SETSCALE11, nullptr, nullptr);
  ps2::ps2_command(PSMOUSE_CMD_SETSCALE11, nullptr, nullptr);
  synaptics::special_command(0x03);
  sample_rate = 0xC8;
  ps2::ps2_command(PSMOUSE_CMD_SETRATE, &sample_rate, nullptr);

  ps2::enable();
}
}  // namespace synaptics