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

namespace ps2 {

// PS2 protocol: https://wiki.osdev.org/PS/2_Mouse

#define PSMOUSE_CMD_ENABLE 0x00f4
#define PSMOUSE_CMD_DISABLE 0x00f5
#define PSMOUSE_CMD_RESET_BAT 0x02ff

void begin(uint8_t clock_pin, uint8_t data_pin, void (*byte_received)(uint8_t));
void ps2_command(u16 command, uint8_t* args, uint8_t* result);

void reset() {
  ps2_command(PSMOUSE_CMD_RESET_BAT, nullptr, nullptr);
}
void enable() {
  ps2_command(PSMOUSE_CMD_ENABLE, nullptr, nullptr);
}
void disable() {
  ps2_command(PSMOUSE_CMD_DISABLE, nullptr, nullptr);
}

}  // namespace ps2