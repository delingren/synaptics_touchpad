// The MIT License (MIT)

// Copyright (c) 2024 Deling Ren

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <math.h>

#include "src/hid.h"
#include "src/ps2.h"
#include "src/synaptics.h"

volatile uint64_t pending_packet;
volatile bool has_pending_packet = false;

void packet_received(uint64_t data) {
  has_pending_packet = true;
  pending_packet = data;
}

void byte_received(uint8_t data) {
  static uint64_t buffer = 0;
  static int index = 0;
  buffer |= ((uint64_t)data) << index;
  index += 8;
  if (index == 48) {
    packet_received(buffer);
    index = 0;
    buffer = 0;
  }
}

void process_pending_packet() {
  uint64_t data = pending_packet;
  has_pending_packet = false;

  uint8_t w = (data >> 26) & 0x01 | (data >> 1) & 0x2 | (data >> 2) & 0x0C;

  if (w != 2) {
    // Packet format: 3.2.1, figure 3-4
    uint16_t y =
        (data >> 40) & 0x00FF | (data >> 4) & 0x0F00 | (data >> 17) & 0x1000;
    uint16_t x =
        (data >> 32) & 0x00FF | (data >> 0) & 0x0F00 | (data >> 16) & 0x1000;
    uint8_t z = (data >> 16) & 0xFF;

    // A clickpad reprots its button as a middle/up button. This pad doesn't
    // have left or right buttons.
    bool button = (data >> 24) & 0x01;
    // 3.2.6 W=0: 2 fingers, W=1: 3 or more fingers, W>=4: finger/palm width
    int fingers = z == 0 ? 0 : (w == 0 ? 2 : (w == 1 ? 3 : 1));

    process_report(true, x, y, z, fingers, button);

    // static char output[256];
    // sprintf(output, "P. x: %u, y: %u, z: %u, w: %u, fingers: %d, button: %d",
    // x,
    //         y, z, w, fingers, button);

    // Serial.println(output);
  } else {
    uint8_t packet_code = (data >> 44) & 0x0F;
    if (packet_code == 1) {
      // 3.2.9.2. Secondary finger information
      uint16_t y = (data >> 15) & 0x01FE | (data >> 27) & 0x1E00;
      uint16_t x = (data >> 7) & 0x01FE | (data >> 23) & 0x1E00;
      uint8_t z = (data >> 39) & 0x1E | (data >> 31) & 0x60;

      process_report(false, x, y, z, 2, 0);

      // static char output[256];
      // sprintf(output, "S. x: %u, y: %u, z: %u, w: %u", x, y, z, w);
      // Serial.println(output);
    }
  }
}

enum state { IDLE, TRACKING, SCROLLING };
uint8_t to_hid_value(int16_t value, float scale_factor = 0.25,
                     int16_t min = -127, int16_t max = 127) {
  const uint16_t threshold = 8;
  if (abs(value) < threshold) {
    return 0;
  }
  float float_value = ((float)value) * scale_factor;
  return min(max(float_value, min), max);
}

void process_report(bool primary, uint16_t x, uint16_t y, uint8_t z,
                    int fingers, bool button) {
  const uint8_t LEFT_BUTTON = 0x01;
  const uint8_t RIGHT_BUTTON = 0x02;

  static uint8_t last_fingers = 0;
  static uint16_t last_finger_positions[2][2];  // {primary, secondary} * {x, y}
  static uint8_t last_buttons = 0;              // bit 0: L, bit 1: R
  static state state = IDLE;

  if (primary && fingers == 0 && z == 0 && !button) {
    if (last_buttons != 0 || last_fingers != 0) {
      last_buttons = 0;
      last_fingers = 0;
      hid::report(0, 0, 0, 0);
    }
    return;
  }

  if (last_fingers == 0 && last_buttons == 0) {
    // IDLE
    // Serial.println("IDLE");
    if (primary) {
      // Zero, or one, or two fingers pressed. Zero is possible if you press
      // down the pad at the very edge.
      last_fingers = fingers;
      last_finger_positions[0][0] = x;
      last_finger_positions[0][1] = y;

      if (button) {
        last_buttons = fingers < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
      }
      hid::report(last_buttons, 0, 0, 0);
    } else {
      // Two fingers pressed. Start SCROLLING!
      // It is possible that the button is also pressed. But we cannot tell from
      // this packet. If that is indeed the case, we will get a primary finger
      // packet with that info and transition into tracking.
      last_fingers = 2;
      last_finger_positions[1][0] = x;
      last_finger_positions[1][1] = y;
    }
    return;
  }

  if (last_fingers == 1) {
    // TRACKING
    // Serial.println("TRACKING, 1 finger.");
    if (primary) {
      if (fingers > 1) {
        // Second finger pressed.
        if (button) {
          // The buton press is considered a right press only if no button was
          // pressed previously. Otherwise we would change a left press to a
          // right press in the middle of a drag.
          if (last_buttons == 0) {
            last_buttons = RIGHT_BUTTON;
          }
        } else {
          // Start SCROLLING
          // TODO: do not report
          last_buttons = 0;
        }
      } else {
        if (button) {
          // Similarly, we don't change a right press to a left press during a
          // drag.
          if (last_buttons == 0) {
            last_buttons = LEFT_BUTTON;
          }
        } else {
          last_buttons = 0;
        }
      }

      int8_t delta_x = to_hid_value(x - last_finger_positions[0][0]);
      int8_t delta_y = to_hid_value(y - last_finger_positions[0][1]);
      hid::report(last_buttons, delta_x, -delta_y, 0);

      last_fingers = fingers;
      last_finger_positions[0][0] = x;
      last_finger_positions[0][1] = y;
    } else {
      // Second finger pressed.
      last_fingers = 2;
      last_finger_positions[1][0] = x;
      last_finger_positions[1][1] = y;
    }
    return;
  }

  if (last_fingers == 2 && last_buttons != 0) {
    // TRACKING
    // Serial.println("TRACKING, 2 fingers.");
    if (primary) {
      if (button) {
        if (last_buttons == 0) {
          last_buttons = fingers < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
        }
      } else {
        last_buttons = 0;
        if (fingers > 1) {
          // Start SCROLLING
          // TODO: do not report
        }
      }

      int8_t delta_x = to_hid_value(x - last_finger_positions[0][0]);
      int8_t delta_y = to_hid_value(y - last_finger_positions[0][1]);
      hid::report(last_buttons, delta_x, -delta_y, 0);

      last_fingers = fingers;
      last_finger_positions[0][0] = x;
      last_finger_positions[0][1] = y;
    } else {
      // Second finger position update
      int8_t delta_x = to_hid_value(x - last_finger_positions[1][0]);
      int8_t delta_y = to_hid_value(y - last_finger_positions[1][1]);
      hid::report(last_buttons, delta_x, -delta_y, 0);

      last_finger_positions[1][0] = x;
      last_finger_positions[1][1] = y;
    }
    return;
  }

  if (last_fingers == 2 && last_buttons == 0) {
    // SCROLLING
    // Serial.println("SCROLLING");
    if (primary) {
      if (button) {
        last_buttons = fingers < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
      } else {
        last_buttons = 0;
      }

      int8_t delta_y = to_hid_value(y - last_finger_positions[0][1], 0.01);
      if (last_buttons == 0) {
        hid::report(0, 0, 0, delta_y);
      }

      last_fingers = fingers;
      last_finger_positions[0][0] = x;
      last_finger_positions[0][1] = y;
    } else {
      int8_t delta_y = to_hid_value(y - last_finger_positions[1][1], 0.01);
      hid::report(0, 0, 0, delta_y);

      last_finger_positions[1][0] = x;
      last_finger_positions[1][1] = y;
    }
  }
}

void setup() {
  Serial.begin(115200);
  hid::init();

  ps2::begin(0, 1, byte_received);
  ps2::reset();
  synaptics::set_mode();
}

void loop() {
  if (has_pending_packet) {
    process_pending_packet();
  }
}
