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

// All the following consts are empirical and may vary from person to person and
// from device to device. These values are a good place to start. I may keep
// fine turning them.

// When finger is held *still*, the maximum flucation from frame to frame in mm.
const float noise_threshold_tracking_mm = 0.15;

const float noise_threshold_scrolling_mm = 0.09;

// HID units per mm, when tracking.
const float scale_tracking_mm = 18.0;
// HID units per mm, when scrolling.
const float scale_scroll_mm = 1.6;
// Cutoff speed between slow and fast scrolling, in mm per frame.
const float slow_scroll_threshold_mm = 2.0;
// Max distance between two frames.
const float max_delta_mm = 3;
// The delta in either direction within which is considered normal movements
// between frames while scrolling at a moderate speed.
const int proximity_threshold_mm = 15;

// HID units per raw unit, when tracking.
float scale_tracking_x, scale_tracking_y;
// UID units per raw unit, when scrolling.
float scale_scroll;
// Max fluctuation from frame to frame in raw units.
float noise_threshold_tracking_x, noise_threshold_tracking_y;

float noise_threshold_scrolling_y;

// Max distance from frame to frame in raw units.
float max_delta_x, max_delta_y;
// Cutoff speed between slow and fast scrolling, in raw units per frame.
float slow_scroll_threshold;
// The number of frames after which we send a scroll to the host in slow
// scrolling mode. Technically the frame number is twice this number since
// we're receiving alternating primary and secondary packets.
const int slow_scroll_frames_per_detent = 7;
// The delta within which is considered normal movements between frames while
// scrolling at a moderate speed.
float proximity_threshold_x, proximity_threshold_y;

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef sign
#define sign(x) ((x) > 0 ? (1) : ((x) < 0 ? (-1) : (0)))
#endif

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
      parse_primary_packet(data, w);
      return;
  }
}

int8_t to_hid_value(float value, float threshold, float scale_factor) {
  const int8_t hid_max = 127;
  if (abs(value) < threshold) {
    return 0;
  }
  return sign(value) * min(max(abs(value) * scale_factor, 1), hid_max);
}

// State of primary and secondary fingers. We only keep track of two since this
// touchpad doesn't report the position of the 3rd and doesn't register the 4th.
static finger_state finger_states[2];
static short finger_count = 0;
static uint8_t button_state = 0;  // bit 0: L, bit 1: R
static uint8_t slow_scroll_frame_count = 0;
const uint8_t LEFT_BUTTON = 0x01;
const uint8_t RIGHT_BUTTON = 0x02;

void parse_primary_packet(uint64_t packet, int w) {
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
  int new_finger_count = 0;
  if (z == 0) {
    new_finger_count = 0;
  } else if (w >= 4) {
    new_finger_count = 1;
  } else if (w == 0) {
    new_finger_count = 2;
  } else if (w == 1) {
    new_finger_count = 3;
  }

  if (new_finger_count > finger_count) {
    // A finger has been added. Reset the secondary finger state.
    finger_states[1].x.reset();
    finger_states[1].y.reset();
    if (finger_count == 0) {
      finger_states[0].x.reset();
      finger_states[0].y.reset();
    }
  }

  int delta_x = 0, delta_y = 0;

  // Update state variables.
  if (new_finger_count > 0) {
    finger_states[0].z = z;

    int prev_x = finger_states[0].x.average();
    int new_x = finger_states[0].x.filter(x);
    if (prev_x > 0 && new_finger_count == finger_count) {
      delta_x = new_x - prev_x;
    }

    int prev_y = finger_states[0].y.average();
    int new_y = finger_states[0].y.filter(y);
    if (prev_y > 0 && new_finger_count == finger_count) {
      delta_y = new_y - prev_y;
    }
  }

  if (new_finger_count < finger_count) {
    // A finger has been released.
    if (finger_count == 2) {
      // 2 fingers -> 1 finger
      // When a primary finger is released, the secondary becomes the primary.
      // We determine if that is happening by checking if the finger location is
      // close to the previous secondary finger. closeness_threshold is an
      // emperical number and not always reliable. We err on the conservative
      // side. If we can't be sure, just reset the state. This could result in a
      // slightly jerky cursor movement.
      if (abs(x - finger_states[0].x.average()) >= proximity_threshold_x ||
          abs(y - finger_states[0].y.average()) >= proximity_threshold_y) {
        if (abs(x - finger_states[1].x.average()) < proximity_threshold_x &&
            abs(y - finger_states[1].y.average()) < proximity_threshold_y) {
          finger_states[0] = finger_states[1];
        } else {
          finger_states[0].x.reset();
          finger_states[0].y.reset();
        }
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

  if (finger_count == 1 && new_finger_count == 1 &&
      (abs(delta_x) >= max_delta_x || abs(delta_y) >= max_delta_y)) {
    // In rare occasions where a finger is released and another is pressed in
    // the same frame, we don't see a finger count change but a big jump in
    // finger position. In this case, reset the position and start over.
    // This solution isn't ideal. A rather big jump could happen when the
    // fingers are moving very fast. A more reliable approach would be based
    // on the recent velocity of the finger movements. But it's complicated
    // and expensive. Both scenarios just described are edge cases and the user
    // is probably just fooling around.
    finger_states[0].x.reset();
    finger_states[0].y.reset();
    delta_x = 0;
    delta_y = 0;
  }

  finger_count = new_finger_count;

  // Determine state.
  if (finger_count == 0) {
    // idle
    if (button_state == 0 && button) {
      button_state = new_finger_count < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
    } else if (!button) {
      button_state = 0;
    }
    hid::report(button_state, 0, 0, 0);
    return;
  }

  if (finger_count >= 2 && button_state == 0) {
    // scrolling
    if (button) {
      // It's OK to change between left and right while scrolling.
      button_state = new_finger_count > 1 ? RIGHT_BUTTON : LEFT_BUTTON;
    } else {
      button_state = 0;
    }

    // Since we're scrolling, we are here every other frame. So we should double
    // the noise threshold.
    int scroll_amount =
        to_hid_value(delta_y, noise_threshold_scrolling_y, scale_scroll);
    if (abs(delta_y) <= slow_scroll_threshold) {
      if (++slow_scroll_frame_count == slow_scroll_frames_per_detent) {
        slow_scroll_frame_count = 0;
        scroll_amount = sign(scroll_amount);
      } else {
        scroll_amount = 0;
      }
    } else {
      slow_scroll_frame_count = 0;
    }

    hid::report(button_state, 0, 0, scroll_amount);

    if (new_finger_count < 2 || button != 0) {
      // We are going to leave scrolling state in the next frame. Reset slow
      // scroll count.
      slow_scroll_frame_count = 0;
    }
    return;
  }

  if (finger_count == 1 || finger_count >= 2 && button_state != 0) {
    // 1-finger tracking or 2-finger tracking
    if (button) {
      // If the button is already pressed, we don't change between left and
      // right while dragging.
      if (button_state == 0) {
        button_state = new_finger_count > 1 ? RIGHT_BUTTON : LEFT_BUTTON;
      }
    } else {
      button_state = 0;
    }
    // If there are multiple fingers pressed, normal packets and secondary
    // packets are alternated. So we should double the threshold.
    hid::report(
        button_state,
        to_hid_value(delta_x,
                     noise_threshold_tracking_x * finger_count == 1 ? 1 : 2,
                     scale_tracking_x),
        -to_hid_value(delta_y,
                      noise_threshold_tracking_y * finger_count == 1 ? 1 : 2,
                      scale_tracking_y),
        0);
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

    if (abs(delta_x) >= max_delta_x || abs(delta_y) >= max_delta_y) {
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
      // Since we are parsing secondary packets, we are here every other frame,
      // so we should double the noise threshold.
      int scroll_amount =
          to_hid_value(delta_y, noise_threshold_scrolling_y, scale_scroll);
      if (abs(delta_y) <= slow_scroll_threshold) {
        if (++slow_scroll_frame_count == slow_scroll_frames_per_detent) {
          slow_scroll_frame_count = 0;
          scroll_amount = sign(scroll_amount);
        } else {
          scroll_amount = 0;
        }
      } else {
        slow_scroll_frame_count = 0;
      }
      hid::report(button_state, 0, 0, scroll_amount);
    } else {
      hid::report(button_state,
                  to_hid_value(delta_x, noise_threshold_tracking_x * 2,
                               scale_tracking_x),
                  -to_hid_value(delta_y, noise_threshold_tracking_y * 2,
                                scale_tracking_y),
                  0);
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
  synaptics::init();

  scale_tracking_x = scale_tracking_mm / synaptics::units_per_mm_x;
  scale_tracking_y = scale_tracking_mm / synaptics::units_per_mm_y;
  scale_scroll = scale_scroll_mm / synaptics::units_per_mm_y;
  noise_threshold_tracking_x =
      noise_threshold_tracking_mm * synaptics::units_per_mm_x;
  noise_threshold_tracking_y =
      noise_threshold_tracking_mm * synaptics::units_per_mm_y;
  noise_threshold_scrolling_y =
      noise_threshold_scrolling_mm * synaptics::units_per_mm_y;
  max_delta_x = max_delta_mm * synaptics::units_per_mm_x;
  max_delta_y = max_delta_mm * synaptics::units_per_mm_y;
  slow_scroll_threshold = slow_scroll_threshold_mm * synaptics::units_per_mm_y;
  proximity_threshold_x = proximity_threshold_mm * synaptics::units_per_mm_x;
  proximity_threshold_y = proximity_threshold_mm * synaptics::units_per_mm_y;
}

void loop() {
  if (has_pending_packet) {
    process_pending_packet();
  }
}
