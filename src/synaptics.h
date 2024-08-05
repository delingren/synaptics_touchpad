#ifndef SYNAPTICS_H
#define SYNAPTICS_H

#include "ps2.h"

namespace synaptics {
extern int units_per_mm_x;
extern int units_per_mm_y;
extern uint8_t clickpad_type;

void special_command(uint8_t command);
void status_request(uint8_t arg, uint8_t *result);
void init();
}  // namespace synaptics

template <class T, int N>
class SimpleAverage {
 private:
  T m_buffer[N];
  int m_count;
  int m_sum;
  int m_index;

 public:
  inline SimpleAverage() { reset(); }
  T filter(T data) {
    // add new entry to sum
    m_sum += data;
    // if full buffer, then we are overwriting, so subtract old from sum
    if (m_count == N) m_sum -= m_buffer[m_index];
    // new entry into buffer
    m_buffer[m_index] = data;
    // move index to next position with wrap around
    if (++m_index >= N) m_index = 0;
    // keep count moving until buffer is full
    if (m_count < N) ++m_count;
    // return average of current items
    return m_sum / m_count;
  }
  inline void reset() {
    m_count = 0;
    m_sum = 0;
    m_index = 0;
  }
  inline int count() const { return m_count; }
  inline int sum() const { return m_sum; }
  T oldest() const {
    // undefined if nothing in here, return zero
    if (m_count == 0) return 0;
    // if it is not full, oldest is at index 0
    // if full, it is right where the next one goes
    if (m_count < N)
      return m_buffer[0];
    else
      return m_buffer[m_index];
  }
  T newest() const {
    // undefined if nothing in here, return zero
    if (m_count == 0) return 0;
    // newest is index - 1, with wrap
    int index = m_index;
    if (--index < 0) index = m_count - 1;
    return m_buffer[index];
  }
  T average() const {
    if (m_count == 0) return 0;
    return m_sum / m_count;
  }
};
#endif