#include <Arduino.h>
#include "synaptics.h"

namespace synaptics {

int units_per_mm_x;
int units_per_mm_y;
uint8_t clickpad_type;

void special_command(uint8_t command) {
  // Reference: 4.2. TouchPad special command sequences
  uint8_t resolution;
  for (int i = 6; i >= 0; i -= 2) {
    resolution = (command >> i) & 0x03;
    ps2::ps2_command(PSMOUSE_CMD_SETRES, &resolution, nullptr);
  }
}

void status_request(uint8_t arg, uint8_t* result) {
  // Reference: 4.4. Information queries
  special_command(arg);
  ps2::ps2_command(PSMOUSE_CMD_GETINFO, nullptr, result);
}

void init() {
  uint8_t result[3];
  char buffer[256];

  Serial.println("TouchPad info:");

  synaptics::status_request(0x00, result);
  uint8_t infoMajor = result[2] & 0x0F;
  uint8_t infoMinor = result[0];
  sprintf(buffer, "  Version: %u.%u", infoMajor, infoMinor);
  Serial.println(buffer);

  synaptics::status_request(0x02, result);
  bool capExtended = result[0] & 0x80;
  if (capExtended) {
    int nExtendedQueries = (result[0] >> 4) & 0x07;
    if (nExtendedQueries >= 1) {
      nExtendedQueries += 8;
    }
    bool middleButton = result[0] & 0x04;
    bool fourButtons = result[2] & 0x08;
    bool multiFinger = result[2] & 0x02;
    bool palmDetect = result[2] & 0x01;

    sprintf(buffer,
            "  Ext Queries: %d\n  Middle Button: %u\n  Four Buttons: %u\n"
            "  Multi-Finger: %u\n  Palm Detection: %u",
            nExtendedQueries, middleButton, fourButtons, multiFinger,
            palmDetect);
    Serial.println(buffer);
  }

  synaptics::status_request(0x08, result);
  units_per_mm_x = result[0];
  units_per_mm_y = result[2];
  sprintf(buffer, "  X units per mm: %d\n  Y units per mm: %d", units_per_mm_x,
          units_per_mm_y);
  Serial.println(buffer);

  synaptics::status_request(0x0C, result);
  bool coveredPadGest = result[0] & 0x80;
  clickpad_type = (result[0] >> 4) & 0x01 | (result[1] << 1) & 0x02;
  char* clickPadInfo[4] = {"Not a ClickPad", "1-button ClickPad",
                           "2-button ClickPad", "Reserved"};
  bool advGest = result[0] & 0x08;
  bool clearPad = result[0] & 0x04;
  sprintf(buffer,
          "  Covered Pad Gesture: %u\n  ClickPad type: %s\n  Adv Gesture: %u",
          coveredPadGest, clickPadInfo[clickpad_type], advGest);
  Serial.println(buffer);

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