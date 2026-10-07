#ifndef CAPTION_TRACK263_SRC_BLIT_H
#define CAPTION_TRACK263_SRC_BLIT_H

#include <cstdint>
#include <vector>

#include "caption_track263/caption_track263.h"

namespace caption_track263 {

// Composites a grayscale coverage mask (255 = full coverage) filled
// with a straight RGBA color over a straight RGBA8 pixel buffer,
// source-over. Both buffers are width*height, pixels width*height*4.
void CompositeMaskSourceOver(const std::vector<uint8_t>& mask, int width,
                             int height, const Rgba& color,
                             std::vector<uint8_t>* pixels);

// Blits a straight RGBA8 image onto a straight RGBA8 frame at (dx, dy)
// (top-left, y down) using source-over. Fully transparent source
// pixels leave the destination untouched.
void BlitRgbaSourceOver(const uint8_t* src, int src_w, int src_h, int dx,
                        int dy, uint8_t* dst, int dst_w, int dst_h);

}  // namespace caption_track263

#endif  // CAPTION_TRACK263_SRC_BLIT_H
