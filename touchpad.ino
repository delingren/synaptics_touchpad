// The MIT License (MIT)

// Copyright (c) 2024 Deling Ren

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject tsubstano the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or tial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "src/hid.h"
#include "src/ps2.h"
#include "src/synaptics.h"

const float scale_x = 0.3;
const float scale_y = 0.3;
const float scale_scroll = 0.02;
const int movement_threshold = 4;
const int closeness_threshold = 15;
const int smoothness_threshold = 200;
const int slow_scroll_tipover = 8;
const int slow_scroll_threshold = 125;

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define sign(x) ((x) > 0 ? (1) : ((x) < 0 ? (-1) : (0)))

struct finger_state {
  SimpleAverage<int, 5> x;
  SimpleAverage<int, 5> y;
  short z;
};

volatile uint64_t pending_packet;
volatile bool has_pending_packet = false;

void packet_received(uint64_t data) {
  has_pending_packet = true;
  pending_packet = data;
}

void byte_received(uint8_t data) {
  static uint64_t buffer = 0;
  static int index = 0;

  if (index == 0 && (data & 0xc8) != 0x80) {
    Serial.print("Unexpected byte0 data ");
    Serial.println(data, HEX);

    index = 0;
    buffer = 0;
    return;
  }

  if (index == 24 && (data & 0xc8) != 0xc0) {
    Serial.print("Unexpected byte3 data ");
    Serial.println(data, HEX);

    index = 0;
    buffer = 0;
    return;
  }

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

  switch (w) {
    case 3:  // pass through
      return;
    case 2:  // extended w mode
      parse_extended_packet(data);
      return;
    default:
      parse_normal_packet(data, w);
      return;
  }
}

int8_t to_hid_value(float value, float scale_factor) {
  const int8_t hid_max = 127;
  if (abs(value) < movement_threshold) {
    return 0;
  }
  return sign(value) * min(max(abs(value) * scale_factor, 1), hid_max);
}

// State of primary and secondary fingers. We only keep track of two since this
// touchpad doesn't report the position of the 3rd and doesn't register the 4th.
static finger_state finger_states[2];
static short finger_count = 0;
static uint8_t button_state = 0;  // bit 0: L, bit 1: R
static uint8_t slow_scroll_count = 0;
const uint8_t LEFT_BUTTON = 0x01;
const uint8_t RIGHT_BUTTON = 0x02;

void parse_normal_packet(uint64_t packet, int w) {
  // Reference: Section 3.2.1, Figure 3-4
  int x = (packet >> 32) & 0x00FF | (packet >> 0) & 0x0F00 |
          (packet >> 16) & 0x1000;
  int y = (packet >> 40) & 0x00FF | (packet >> 4) & 0x0F00 |
          (packet >> 17) & 0x1000;
  short z = (packet >> 16) & 0xFF;

  // A clickpad reprots its button as a middle/up button. This logic needs to
  // change completely if the touchpad is not a clickpad (i.e. it has physical
  // buttons).
  bool button = (packet >> 24) & 0x01;
  int fingers = 0;
  if (z == 0) {
    fingers = 0;
  } else if (w >= 4) {
    fingers = 1;
  } else if (w == 0) {
    fingers = 2;
  } else if (w == 1) {
    fingers = 3;
  }

  if (fingers == 0 && z == 0 && !button) {
    // After all fingers and the button are released, the touchpad keep
    // reporting for 1 second. But we only need to report it once and ignore
    // the rest of the packets.
    if (button_state != 0 || finger_count != 0) {
      button_state = 0;
      finger_count = 0;
      hid::report(0, 0, 0, 0);
    }
    return;
  }

  if (fingers > finger_count) {
    // A finger has been added. Reset the secondary finger state.
    finger_states[1].x.reset();
    finger_states[1].y.reset();
    if (finger_count == 0) {
      finger_states[0].x.reset();
      finger_states[0].y.reset();
    }
  }

  if (fingers < finger_count) {
    // A finger has been released.
    if (fingers == 1) {
      // 2 fingers -> 1 finger
      // When a primary finger is released, the secondary becomes the primary.
      // We determine if that is happening be checking if the finger location is
      // close to the previous secondary finger. closeness_threshold is an
      // emperical number and not always reliable. We err on the conservative
      // side. If we can't be sure, just reset the state. This could result in a
      // slightly jerky cursor movement.
      if (abs(x - finger_states[1].x.average()) < closeness_threshold &&
          abs(y - finger_states[1].y.average()) < closeness_threshold) {
        finger_states[0] = finger_states[1];
      } else if (abs(x - finger_states[0].x.average()) >= closeness_threshold ||
                 abs(y - finger_states[0].y.average()) >= closeness_threshold) {
        finger_states[0].x.reset();
        finger_states[0].y.reset();
      }
    } else {
      // 3 fingers -> 2 fingers or 1 finger.
      // Let's not bother. Just reset both fingers.
      finger_states[0].x.reset();
      finger_states[0].y.reset();
      finger_states[1].x.reset();
      finger_states[1].y.reset();
    }
  }

  if (finger_count > 1 && fingers == 1) {
    // When a primary finger is released, the secondary becomes the primary.
    // We determine if that is happening be checking if the finger location is
    // close to the previous secondary finger. closeness_threshold is an
    // emperical number and not always reliable. We err on the conservative
    // side. If we can't be sure, just reset the state. This could result in a
    // slightly jerky cursor movement.
    if (abs(x - finger_states[1].x.average()) < closeness_threshold &&
        abs(y - finger_states[1].y.average()) < closeness_threshold) {
      finger_states[0] = finger_states[1];
    } else if (abs(x - finger_states[0].x.average()) >= closeness_threshold ||
               abs(y - finger_states[0].y.average()) >= closeness_threshold) {
      finger_states[0].x.reset();
      finger_states[0].y.reset();
    }
  }

  int delta_x = 0, delta_y = 0;

  // Update state variables.
  if (fingers > 0) {
    finger_states[0].z = z;

    int prev_x = finger_states[0].x.average();
    int new_x = finger_states[0].x.filter(x);
    if (prev_x > 0 && fingers == finger_count) {
      delta_x = new_x - prev_x;
    }

    int prev_y = finger_states[0].y.average();
    int new_y = finger_states[0].y.filter(y);
    if (prev_y > 0 && fingers == finger_count) {
      delta_y = new_y - prev_y;
    }
  }

  finger_count = fingers;

  // Determine state.
  if (finger_count == 0 && button_state == 0) {
    // idle
    if (button) {
      button_state = fingers < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
    }
    hid::report(button_state, 0, 0, 0);
    return;
  }

  if (finger_count >= 2 && button_state == 0) {
    // scrolling
    if (button) {
      // It's OK to change between left and right while scrolling.
      button_state = fingers > 1 ? RIGHT_BUTTON : LEFT_BUTTON;
    } else {
      button_state = 0;
    }

    int scroll_amount = to_hid_value(delta_y, scale_scroll);
    if (abs(delta_y) <= slow_scroll_threshold) {
      if (++slow_scroll_count == slow_scroll_tipover) {
        slow_scroll_count = 0;
        scroll_amount = sign(scroll_amount);
      } else {
        scroll_amount = 0;
      }
    } else {
      slow_scroll_count = 0;
    }

    hid::report(button_state, 0, 0, scroll_amount);

    if (fingers < 2 || button != 0) {
      // We are going to leave scrolling state in the next frame. Reset slow
      // scroll count.
      slow_scroll_count = 0;
    }

    return;
  }

  if (finger_count == 1 || finger_count >= 2 && button_state != 0) {
    // 1-finger tracking or 2-finger tracking
    if (button) {
      // If the button is already pressed, we don't change between left and
      // right while dragging.
      if (button_state == 0) {
        button_state = fingers > 1 ? RIGHT_BUTTON : LEFT_BUTTON;
      }
    } else {
      button_state = 0;
    }
    hid::report(button_state, to_hid_value(delta_x, scale_x),
                -to_hid_value(delta_y, scale_y), 0);
    return;
  }
}

void parse_extended_packet(uint64_t packet) {
  uint8_t packet_code = (packet >> 44) & 0x0F;
  if (packet_code == 1) {
    // Reference: Section 3.2.9.2. Figure 3-14
    int x = (packet >> 7) & 0x01FE | (packet >> 23) & 0x1E00;
    int y = (packet >> 15) & 0x01FE | (packet >> 27) & 0x1E00;
    short z = (packet >> 39) & 0x1D | (packet >> 23) & 0x60;

    if (x == 0 || y == 0 || z == 0) {
      finger_states[1].x.reset();
      finger_states[1].y.reset();
      return;
    }

    int prev_x = finger_states[1].x.average();
    int new_x = finger_states[1].x.filter(x);
    int delta_x = prev_x == 0 ? 0 : new_x - prev_x;

    int prev_y = finger_states[1].y.average();
    int new_y = finger_states[1].y.filter(y);
    int delta_y = prev_y == 0 ? 0 : new_y - prev_y;

    if (abs(delta_x) >= smoothness_threshold ||
        abs(delta_y) >= smoothness_threshold) {
      // Sometimes when a 2nd or 3rd finger is released, we receive a secondary
      // finger position before the finger count change. In this case, the new
      // secondary finger is not necessarily the same physical finger as
      // previous one. Not sure if this is by design or due to a packet loss.
      // In either case, we should not report this position change to avoid
      // jerky movements. Instead, reset the secondary finger state and start
      // over.
      finger_states[1].x.reset();
      finger_states[1].y.reset();
      delta_x = 0;
      delta_y = 0;
    }

    finger_states[1].x.filter(x);
    finger_states[1].y.filter(y);
    finger_states[1].z = z;

    if (finger_count >= 2 && button_state == 0) {
      int scroll_amount = to_hid_value(delta_y, scale_scroll);
      if (abs(delta_y) <= slow_scroll_threshold) {
        if (++slow_scroll_count == slow_scroll_tipover) {
          slow_scroll_count = 0;
          scroll_amount = sign(scroll_amount);
        } else {
          scroll_amount = 0;
        }
      } else {
        slow_scroll_count = 0;
      }
      hid::report(button_state, 0, 0, scroll_amount);
    } else {
      hid::report(button_state, to_hid_value(delta_x, scale_x),
                  -to_hid_value(delta_y, scale_y), 0);
    }
  } else {
    Serial.println("Finger count packet");
  }
}

void setup() {
  Serial.begin(115200);
  hid::init();
  // The pins are quite noisy on startup. Let's wait for a bit before starting
  // the touchpad. 500ms seems to be a good time. It's still not bullet proof
  // but the error handling mechanism seems to be able to recover every time.
  delay(500);
  ps2::begin(7, 8, byte_received);
  ps2::reset();
  synaptics::set_mode();
}

void loop() {
  if (has_pending_packet) {
    process_pending_packet();
  }
}
