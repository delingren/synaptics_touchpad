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

#include "src/hid.h"
#include "src/ps2.h"

int8_t to_hid_value(bool overflow, bool negative, uint8_t data) {
  // HID uses [-127, 127]. I.e. an 8-bit signed integer, except -128.
  // PS2 uses [-256, 255]. I.e. a 9-bit signed integer.
  int16_t ps2_value = negative ? (overflow ? -256 : ((int16_t)data) - 256)
                               : (overflow ? 255 : data);
  return negative ? (-127 <= ps2_value && ps2_value <= -1 ? ps2_value : -127)
                  : (0 <= ps2_value && ps2_value <= 127 ? ps2_value : 127);
}

uint32_t pending_packet;
uint32_t has_pending_packet = false;

void packet_received(uint32_t data) {
  has_pending_packet = true;
  pending_packet = data;
}

void process_pending_packet() {
  uint32_t data = pending_packet;
  has_pending_packet = false;

  bool y_overflow = (data >> 7) & 0x01;
  bool x_overflow = (data >> 6) & 0x01;
  bool y_negative = (data >> 5) & 0x01;
  bool x_negative = (data >> 4) & 0x01;
  uint8_t y_value = (data >> 16) & 0xFF;
  uint8_t x_value = (data >> 8) & 0xFF;
  bool middle = (data >> 2) & 0x01;
  bool right = (data >> 1) & 0x01;
  bool left = data & 0x01;

  int8_t x = to_hid_value(x_overflow, x_negative, x_value);
  int8_t y = to_hid_value(y_overflow, y_negative, y_value);

  hid::report(left | right << 1 | middle << 2, x, -y, 0);

  Serial.print("L: ");
  Serial.print(left);
  Serial.print(" M: ");
  Serial.print(middle);
  Serial.print(" R: ");
  Serial.print(right);
  Serial.println();
}

void byte_received(uint8_t data) {
  // Relative mode. Each packet consists of 3 bytes. Let's use a 32 bit unsigned
  // int.
  static uint32_t buffer = 0;
  static int index = 0;
  buffer |= ((uint32_t)data) << index;
  index += 8;
  if (index == 24) {
    packet_received(buffer);
    index = 0;
    buffer = 0;
  }
}

void setup() {
  hid::init();

  ps2::begin(0, 1, byte_received);
  ps2::reset();
  ps2::enable();
}

void loop() {
  if (has_pending_packet) {
    process_pending_packet();
  }
}
