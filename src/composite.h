#ifndef CAPTION_TRACK263_SRC_COMPOSITE_H
#define CAPTION_TRACK263_SRC_COMPOSITE_H

#include <cstdint>

#include "caption_track263/caption_track263.h"

namespace caption_track263 {

// Composites one straight RGBA source pixel source-over onto a
// straight RGBA destination pixel (r,g,b,a, four consecutive bytes).
inline void CompositeSourceOver(uint8_t sr, uint8_t sg, uint8_t sb,
                                uint8_t sa8, uint8_t* dst) {
  unsigned sa = sa8;
  unsigned da = dst[3];
  unsigned out_a = sa + da * (255u - sa) / 255u;
  if (out_a == 0) return;
  unsigned inv = 255u - sa;
  // Un-premultiply with an explicit clamp: intermediate integer
  // truncation can otherwise round the numerator just above
  // out_a * 255, wrapping a white result to black.
  auto blend = [&](unsigned sc, unsigned dc) {
    unsigned num = sc * sa * 255u + dc * da * inv;
    unsigned v = num / (out_a * 255u);
    return static_cast<uint8_t>(v > 255u ? 255u : v);
  };
  dst[0] = blend(sr, dst[0]);
  dst[1] = blend(sg, dst[1]);
  dst[2] = blend(sb, dst[2]);
  dst[3] = static_cast<uint8_t>(out_a);
}

}  // namespace caption_track263

#endif
