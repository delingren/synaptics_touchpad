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

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef sign
#define sign(x) ((x) > 0 ? (1) : ((x) < 0 ? (-1) : (0)))
#endif

// All the following consts are empirical and may vary from person to person and
// from device to device. These values are a good place to start. I may keep
// fine turning them.

// When finger is held *still*, the maximum flucation from frame to frame in mm.
const float noise_threshold_tracking_mm = 0.08;
const float noise_threshold_scrolling_mm = 0.09;

// In order to retrospectively change the frames in the past, we delay reporting
// for a few frames. This needs to be short enough that it's not perceptible.
const int frames_delay = 6;
// After the button is released, we freeze for a few frames since the finger is
// likely going to be very unstable. Since it's very hard to release the button
// and start moving immediately, it's OK to keep this frozen period relatively
// long.
const int frames_stablization = 15;

// HID units per mm, when tracking.
const float scale_tracking_mm = 12.0;
// HID units per mm, when scrolling.
const float scale_scroll_mm = 1.6;
// Cutoff speed between slow and fast scrolling, in mm/frame.
const float slow_scroll_threshold_mm = 2.0;
// Max distance between two frames.
const float max_delta_mm = 3;
// The delta in either direction within which is considered normal movements
// between frames while scrolling at a moderate speed.
const int proximity_threshold_mm = 15;
// The amount of scroll per detent, in HID units
const float slow_scroll_amount = 0.20F;

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
// The delta within which is considered normal movements between frames while
// scrolling at a moderate speed.
float proximity_threshold_x, proximity_threshold_y;

struct finger_state {
  SimpleAverage<int, 5> x;
  SimpleAverage<int, 5> y;
  short z;
};

struct report {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t scroll;
};

// In reality we don't really need a ring buffer for packets. A 16MHz ATMega32U4
// can easily handle 80 frames per second without skipping frames.
RingBuffer<uint64_t, 4> packets;
RingBuffer<report, 32> reports;

static unsigned long global_tick = 0;
static unsigned long session_started_tick = 0;
static unsigned long button_released_tick = 0;

// State of primary and secondary fingers. We only keep track of two since this
// touchpad doesn't report the position of the 3rd and doesn't register the 4th.
static finger_state finger_states[2];
static short finger_count = 0;
static uint8_t button_state = 0;  // bit 0: L, bit 1: R

const uint8_t LEFT_BUTTON = 0x01;
const uint8_t RIGHT_BUTTON = 0x02;

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
    packets.push_back(buffer);
    index = 0;
    buffer = 0;
  }
}

void process_pending_packet(uint64_t packet) {
  global_tick++;
  uint8_t w =
      (packet >> 26) & 0x01 | (packet >> 1) & 0x2 | (packet >> 2) & 0x0C;

  // When parsing packets, we queue the reports instead of send them directly.
  // Then we delay sending them for a few frames, to give us an opportunity to
  // retrospectively change the reports.
  // We produce at most one report per frame. So we only need to send one
  // pending report per frame. Once all activities ceased, the touchpad keeps
  // sending packets with x, y, and z all set to 0 for one second. And we only
  // report the first one. That means, we have plenty of time to clear up the
  // report queue, which we need to do. Otherwise the queue will get clogged up
  // soon, and reports leak to the next session, causing weird behaviors.
  if (global_tick - session_started_tick >= frames_delay) {
    if (!reports.empty()) {
      report item = reports.pop_front();
      hid::report(item.buttons, item.x, item.y, item.scroll);
    }
  }

  switch (w) {
    case 3:  // pass through
      return;
    case 2:  // extended w mode
      parse_extended_packet(packet);
      return;
    default:
      parse_primary_packet(packet, w);
      return;
  }
}

float to_hid_value(float value, float threshold, float scale_factor) {
  const float hid_max = 127.0F;
  if (abs(value) < threshold) {
    return 0;
  }
  return sign(value) * min(max(abs(value) * scale_factor, 1.0F), hid_max);
}

void queue_report(uint8_t buttons, int8_t x, int8_t y, float scroll) {
  static float scroll_amount_rollover = 0;
  report item = {.buttons = buttons};
  if (button_released_tick != 0 &&
      global_tick - button_released_tick < frames_stablization) {
    // When a button is released, we freeze the next few frames.
    item.x = 0;
    item.y = 0;
    item.scroll = 0;
  } else {
    if (scroll > -1.0F && scroll < 1.0F) {
      scroll_amount_rollover += scroll;
      if (scroll_amount_rollover >= 1.0F) {
        scroll = 1.0F;
        scroll_amount_rollover -= 1.0F;
      } else if (scroll_amount_rollover <= -1.0F) {
        scroll = -1.0F;
        scroll_amount_rollover += 1.0F;
      } else {
        scroll = 0;
      }
    }
    item.x = x;
    item.y = y;
    item.scroll = scroll;
  }
  reports.push_back(item);
}

void parse_primary_packet(uint64_t packet, int w) {
  // Reference: Section 3.2.1, Figure 3-4
  int x = (packet >> 32) & 0x00FF | (packet >> 0) & 0x0F00 |
          (packet >> 16) & 0x1000;
  int y = (packet >> 40) & 0x00FF | (packet >> 4) & 0x0F00 |
          (packet >> 17) & 0x1000;
  short z = (packet >> 16) & 0xFF;
  // w is width only if it >= 4. otherwise it encodes finger count
  short width = max(w, 4);

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

  if (finger_count == 0 && new_finger_count > 0) {
    session_started_tick = global_tick;
  }

  /* Mechanisms to smooth the movements. */

  // When a button is pressed, we retrospectively freeze the previous frames,
  // since the movements tend to be jerky when releasing a button.
  if (button && button_state == 0) {
    int size = reports.size();
    for (int i = 0; i < size; i++) {
      reports[i].x = 0;
      reports[i].y = 0;
      reports[i].scroll = 0;
    }
  }

  // When a button is released, we freeze the next few frames, since the
  // movements tend to be jerky when pressing a button.
  if (!button && button_state != 0) {
    button_released_tick = global_tick;
  }

  // When a finger is lifted, we restrospectively freeze the previous
  // frames, since the movements tend to be jerky when lifting a finger.
  if (new_finger_count < finger_count) {
    for (int i = 0; i < reports.size(); i++) {
      reports[i].x = 0;
      reports[i].y = 0;
      reports[i].scroll = 0;
    }
  }

  /* Update state variables. */
  if (new_finger_count > finger_count) {
    // A finger has been added. Reset state for that finger.
    finger_states[1].x.reset();
    finger_states[1].y.reset();
    if (finger_count == 0) {
      finger_states[0].x.reset();
      finger_states[0].y.reset();
    }
  }

  int delta_x = 0, delta_y = 0;

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
      // close to the previous secondary finger. proximity_threshold is an
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
      // 3 fingers -> 2 fingers or 1 finger, or all fingers have been lifted.
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

  /* State machine logic */
  if (finger_count == 0) {
    // idle
    if (button_state == 0 && button) {
      button_state = new_finger_count < 2 ? LEFT_BUTTON : RIGHT_BUTTON;
      queue_report(button_state, 0, 0, 0);
    } else if (button_state != 0 && !button) {
      button_state = 0;
      queue_report(0, 0, 0, 0);
    }
  } else if (finger_count >= 2 && button_state == 0) {
    // scrolling
    if (button) {
      // It's OK to change between left and right while scrolling.
      button_state = new_finger_count > 1 ? RIGHT_BUTTON : LEFT_BUTTON;
    } else {
      button_state = 0;
    }

    // Since we're scrolling, we are here every other frame. So we should double
    // the noise threshold.
    float scroll_amount =
        to_hid_value(delta_y, noise_threshold_scrolling_y, scale_scroll);
    if (abs(delta_y) <= slow_scroll_threshold) {
      scroll_amount = sign(scroll_amount) * slow_scroll_amount;
    }
    queue_report(button_state, 0, 0, scroll_amount);
  } else if (finger_count == 1 || finger_count >= 2 && button_state != 0) {
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
    float threshold_multiplier = finger_count == 1 ? 1.0 : 2.0;
    // Serial.print("Width: ");
    // Serial.print(width);
    // Serial.print(" Pressure: ");
    // Serial.println(z);

    if (width > 4) {
      // Fat finger
      threshold_multiplier *= 1.0F + (width - 4.0F) / 4.0F;
    }
    if (z >= 60) {
      // Heavy finger
      threshold_multiplier *= 1.0F + (z - 60.0F) / 40.0F;
    }

    float delta_x_mm = ((float)delta_x) / ((float)synaptics::units_per_mm_x);
    float delta_y_mm = ((float)delta_y) / ((float)synaptics::units_per_mm_y);
    // Precision for low speed and range for high speed. If there are more than
    // one finger the speed needs to be doubled.
    float velocity = sqrt(delta_x_mm * delta_x_mm + delta_y_mm * delta_y_mm);
    if (finger_count > 1) {
      velocity *= 2;
    }
    float scale_multiplier = 1.0F + velocity * 0.5F;  // Emperical constant

    int8_t delta_x_hid =
        to_hid_value(delta_x, noise_threshold_tracking_x * threshold_multiplier,
                     scale_tracking_x * scale_multiplier);
    int8_t delta_y_hid = -to_hid_value(
        delta_y, noise_threshold_tracking_y * threshold_multiplier,
        scale_tracking_y * scale_multiplier);
    queue_report(button_state, delta_x_hid, delta_y_hid, 0);
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

    // TODO: use velocity and z value to adjst the multiplier here too, just
    // like the primary frames. We don't have width info though.
    if (finger_count >= 2 && button_state == 0) {
      // Since we are parsing secondary packets, we are here every other frame,
      // so we should double the noise threshold.
      float scroll_amount =
          to_hid_value(delta_y, noise_threshold_scrolling_y, scale_scroll);
      if (abs(delta_y) <= slow_scroll_threshold) {
        scroll_amount = sign(scroll_amount) * slow_scroll_amount;
      }
      queue_report(button_state, 0, 0, scroll_amount);
    } else {
      int8_t delta_x_hid = to_hid_value(
          delta_x, noise_threshold_tracking_x * 2.0F, scale_tracking_x);
      int8_t delta_y_hid = -to_hid_value(
          delta_y, noise_threshold_tracking_y * 2.0F, scale_tracking_y);
      queue_report(button_state, delta_x_hid, delta_y_hid, 0);
    }
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
  if (!packets.empty()) {
    uint64_t packet = packets.pop_front();
    process_pending_packet(packet);
  }
}
