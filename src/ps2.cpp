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

#include <Arduino.h>
#include "ps2.h"

namespace ps2 {
// Sample code for interacting with a PS2 mouse from a mega32u4.
// Writing is synchronous. Async write is hard for the client to handle.
namespace {  // anonymous namespace to hide code from the client.
// On atmel mega32u4, clock_pin needs to be one of these: 0, 1, 2, 3, 7,
// otherwise, we need to use pin change interrupt.
int clock_pin_;
int data_pin_;
void (*byte_received_)(uint8_t);

void pull_low(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void pull_high(uint8_t pin) { pinMode(pin, INPUT_PULLUP); }

uint8_t read_bit() {
  pinMode(data_pin_, INPUT);
  return digitalRead(data_pin_);
}

void write_bit(uint8_t data) {
  pinMode(data_pin_, OUTPUT);
  digitalWrite(data_pin_, data);
}

void wait_clock(uint8_t value) {
  if (value == LOW) {
    pinMode(clock_pin_, INPUT_PULLUP);
  } else {
    pinMode(clock_pin_, INPUT);
  }

  unsigned long millis_start = millis();
  while (digitalRead(clock_pin_) != value) {
    if (millis() - millis_start > 25) {
      Serial.println("wait_clock timed out.");
      return;
    }
  }
}

volatile int receive_index = 0;
volatile uint8_t receive_buffer = 0;
volatile uint8_t parity = 0;

void disable_interrupt() {
  cli();
  // We need to abandon the ongoing read from the device.
  // Otherwise it'll go haywire next time we enable interrupt.
  receive_index = 0;
  receive_buffer = 0;
  parity = 0;
}

void enable_interrupt() { sei(); }

void bit_received() {
  int clock = digitalRead(clock_pin_);
  if (clock != LOW) {
    return;
  }

  int bit = read_bit();
  if (receive_index == 0) {
    // Start bit
    if (bit != LOW) {
      Serial.println("Start bit error.");
    }
  } else if (receive_index >= 1 && receive_index <= 8) {
    // Payload bit
    receive_buffer |= bit << (receive_index - 1);
    parity ^= bit;
  } else if (receive_index == 9) {
    // Parity bit
    parity ^= bit;
    if (parity != 1) {
      Serial.println("Parity bit error.");
    }

  } else if (receive_index == 10) {
    // Stop bit
    if (bit != HIGH) {
      Serial.println("Stop bit error.");
    }
    byte_received_(receive_buffer);
    receive_buffer = 0;
    receive_index = 0;
    parity = 0;
    return;
  }

  receive_index++;
}

// Reads a byte synchronously. This should only be called from write_byte() to
// read the response and shouldn't be called by the client.
uint8_t read_byte() {
  // Start bit
  wait_clock(LOW);
  uint8_t start_bit = read_bit();
  if (start_bit != LOW) {
    Serial.println("Start bit error.");
  }
  wait_clock(HIGH);

  uint8_t data = 00;
  uint8_t parity = 0;
  for (int i = 0; i < 8; i++) {
    wait_clock(LOW);
    uint8_t bit = read_bit();
    wait_clock(HIGH);

    data |= bit << i;
    parity ^= bit;
  }

  wait_clock(LOW);
  parity ^= read_bit();
  wait_clock(HIGH);
  if (parity != 1) {
    Serial.println("Parity error.");
  }

  wait_clock(LOW);
  uint8_t stop = read_bit();
  if (stop != 1) {
    Serial.println("Stop bit error.");
  }
  pull_low(clock_pin_);
  delayMicroseconds(50);
  pull_high(clock_pin_);

  return data;
}
}  // namespace

bool write_byte(uint8_t data) {
  // Bring CLK low for 100 us.
  pull_low(clock_pin_);
  delayMicroseconds(100);
  // Bring DATA low.
  pull_low(data_pin_);
  // Release CLK
  pull_high(clock_pin_);
  // Now we are in the request-to-send state.

  uint8_t const stop = 1;
  uint8_t parity = 1;

  // bits 0 - 7: payload
  for (int i = 0; i < 8; i++) {
    // Get the LSB
    uint8_t bit = data & 0x01;
    // Update parity
    parity ^= bit;
    // Shift data for next iteration
    data >>= 1;

    // Device samples when CLK is low.
    wait_clock(LOW);
    write_bit(bit);
    wait_clock(HIGH);
  }

  // bit 8: parity
  wait_clock(LOW);
  write_bit(parity);
  wait_clock(HIGH);

  // bit 9: stop
  wait_clock(LOW);
  write_bit(stop);
  wait_clock(HIGH);

  // bit 10: line control
  wait_clock(LOW);
  uint8_t line_control = read_bit();
  wait_clock(HIGH);
  if (line_control != LOW) {
    Serial.println("Line control error.");
  }

  uint8_t ack = read_byte();
  if (ack != 0xFA) {
    Serial.println("Error: did not receive ACK");
    return false;
  }
}

void begin(uint8_t clock_pin, uint8_t data_pin,
           void (*byte_received)(uint8_t)) {
  clock_pin_ = clock_pin;
  data_pin_ = data_pin;
  byte_received_ = byte_received;

  pull_high(clock_pin);
  pull_high(data_pin);

  attachInterrupt(digitalPinToInterrupt(clock_pin_), bit_received, FALLING);
}

bool ps2_command(uint16_t command, uint8_t* args, uint8_t* result) {
  disable_interrupt();

  unsigned int send = (command >> 12) & 0x0F;
  unsigned int receive = (command >> 8) & 0x0F;
  write_byte(command & 0xFF);

  for (int i = 0; i < send; i++) {
    write_byte(args[i]);
  }

  for (int i = 0; i < receive; i++) {
    uint8_t response = read_byte();
    if (result != nullptr) {
      result[i] = response;
    }
  }

  enable_interrupt();
  return true;
}

void reset() { ps2_command(PSMOUSE_CMD_RESET_BAT, nullptr, nullptr); }

void enable() { ps2_command(PSMOUSE_CMD_ENABLE, nullptr, nullptr); }

void disable() { ps2_command(PSMOUSE_CMD_DISABLE, nullptr, nullptr); }

};  // namespace ps2