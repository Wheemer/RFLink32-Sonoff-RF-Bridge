#pragma once
#include <stdint.h>

// Main initializes this before attaching the callback and reads it only after
// detaching. The callback owns all progress; done is the publication flag.
struct RfPulseTrain {
  const uint16_t *pulses = nullptr;
  uint16_t count = 0, index = 0, repeats = 0, repeated = 0;
  uint32_t multiply = 1, gapUs = 0, cyclesPerUs = 80;
  uint32_t deadline = 0, remainingUs = 0, maxLateCycles = 0;
  bool waiting = false;
  volatile bool done = false;

  template<class Write>
  uint32_t IRAM_ATTR step(uint32_t now, Write write) {
    const uint32_t chunkUs = 10000;
    if(done)
      return cyclesPerUs * chunkUs;
    if(waiting) {
      const int32_t left = static_cast<int32_t>(deadline - now);
      if(left > 0)
        return static_cast<uint32_t>(left);
      const uint32_t late = now - deadline;
      if(late > maxLateCycles)
        maxLateCycles = late;
    }
    if(remainingUs) {
      const uint32_t chunk = remainingUs > chunkUs ? chunkUs : remainingUs;
      remainingUs -= chunk;
      // Continue a long pulse without accumulating callback-entry lateness.
      deadline += chunk * cyclesPerUs;
      const int32_t left = static_cast<int32_t>(deadline - now);
      return left > 0 ? static_cast<uint32_t>(left) : 1;
    }
    while(true) {
      uint32_t duration;
      if(index < count) {
        write((index & 1U) == 0);
        duration = static_cast<uint32_t>(pulses[++index]) * multiply;
      } else if(repeated < repeats) {
        ++repeated;
        index = 0;
        duration = gapUs;
      } else {
        done = true;
        return cyclesPerUs * chunkUs;
      }
      if(duration == 0)
        continue;
      const uint32_t chunk = duration > chunkUs ? chunkUs : duration;
      remainingUs = duration - chunk;
      // Never compress the next symbol to catch up a late previous edge.
      deadline = now + chunk * cyclesPerUs;
      waiting = true;
      return chunk * cyclesPerUs;
    }
  }
};
