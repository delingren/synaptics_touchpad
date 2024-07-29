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
    
    // A clickpad reprots its button as a middle/up button. This pad doesn't have
    // left or right buttons.
    bool button = (data >> 24) & 0x01;
    // 3.2.6 W=0: 2 fingers, W=1: 3 or more fingers, W>=4: finger/palm width
    int fingers = z == 0 ? 0 : (w == 0 ? 2 : (w == 1 ? 3 : 1));

    // process_report(x, y, z, fingers, button);

    static char output[256];
    sprintf(output, "P. x: %u, y: %u, z: %u, w: %u, fingers: %d, button: %d", x, y, z, w, fingers, button);
    if (z > 0) {
      Serial.println(output);
    }
  } else {
    uint8_t packet_code = (data >> 44) & 0x0F;
    if (packet_code == 1) {
      // 3.2.9.2. Secondary finger information
      uint16_t y = (data >> 15) & 0x01FE | (data >> 27) & 0x1E00;
      uint16_t x = (data >> 7) & 0x01FE | (data >> 23) & 0x1E00;
      uint8_t z = (data >> 39) & 0x1E | (data >> 31) & 0x60;

      static char output[256];
      sprintf(output, "S. x: %u, y: %u, z: %u, w: %u", x, y, z, w);
      Serial.println(output);
    } else if (packet_code == 2) {
      uint8_t finger_count = (data >> 8) & 0x0F;
      uint8_t primary_index = (data >> 16) & 0xFF;
      uint8_t secondary_index = (data >> 32) & 0xFF;
      bool button = (data >> 24) & 0x01;

      static char output[256];
      sprintf(output, "F. fingers: %d, primary: %u, secondary: %u, button: %d", finger_count, primary_index, secondary_index, button);
      Serial.println(output);
    } else {
      Serial.print("EW Packet code: ");
      Serial.print(packet_code);
    }
  }
}

enum state { IDLE, TRACKING, SCROLLING };
uint8_t to_hid_value(int16_t value, int16_t min, int16_t max) {
  const uint16_t threshold = 16;
  const float scale_factor = 0.25;
  if (abs(value) < threshold) {
    return 0;
  }
  float float_value = ((float)value) * scale_factor;
  return min(max(float_value, min), max);
}

void process_report(uint16_t x, uint16_t y, uint8_t z, int fingers,
                    bool button) {
  const uint8_t LEFT_BUTTON = 0x01;
  const uint8_t RIGHT_BUTTON = 0x02;
  static uint16_t last_x;
  static uint16_t last_y;
  static uint8_t buttons = 0; // bit 0: L, bit 1: R
  static state state = IDLE;

  if (z == 0) {
    if (button && state == IDLE) {
      // Note: both x and y should be 0
      buttons = fingers < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
      hid::report(buttons, 0, 0, 0);
      Serial.print("Button: ");
      Serial.println(buttons, HEX);
    } else {
      state = IDLE;
    }
    return;
  }

  if (state == IDLE) {
    if (x > 0 && y > 0) {
      // Starting tracking or scrolling.
      last_x = x;
      last_y = y;
      if (fingers == 1) {
        state = TRACKING;
        // Serial.println("Starting tracking...");
      } else {
        state = SCROLLING;
        // Serial.println("Starting scrolling...");
      }
    }
    return;
  }

  int8_t delta_x = to_hid_value(x - last_x, -127, 127);
  int8_t delta_y = to_hid_value(y - last_y, -127, 127);

  last_x = x;
  last_y = y;

  if (button && buttons == 0x00) {
    // We do not change from one button to another while any button is pressed.
    buttons = fingers < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
  } else if (!button) {
    buttons = 0x00;
  }

  if (state == TRACKING) {
    // Serial.print("Moving: ");
    // Serial.print(delta_x);
    // Serial.print(", ");
    // Serial.println(delta_y);
    hid::report(buttons, delta_x, - delta_y, 0);
  } else if (state == SCROLLING) {
    // Serial.print("Scrolling: ");
    // Serial.print(delta_x);
    // Serial.print(", ");
    // Serial.println(delta_y);
    hid::report(buttons, 0, 0, delta_y * 0.25);
  }

  // We don't change from tracking to scrolling while the button is pressed.
  state = fingers < 2 || buttons != 0 ? TRACKING : SCROLLING;
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
