#include "blit.h"

namespace caption_track263 {

namespace {

// Source-over for straight (non-premultiplied) RGBA8. All divisions
// round to nearest and results are clamped, so the 8-bit channels can
// never wrap (e.g. white over white at alpha 128 stays white).
void SourceOverPixel(unsigned sr, unsigned sg, unsigned sb, unsigned sa,
                     uint8_t* dst) {
  const unsigned da = dst[3];
  const unsigned inv = 255u - sa;
  const unsigned out_a = sa + (da * inv + 127u) / 255u;
  if (out_a == 0u) {
    dst[0] = dst[1] = dst[2] = dst[3] = 0;
    return;
  }
  const unsigned denom = out_a * 255u;
  const unsigned half = denom / 2u;
  unsigned out_r = (sr * sa * 255u + dst[0] * da * inv + half) / denom;
  unsigned out_g = (sg * sa * 255u + dst[1] * da * inv + half) / denom;
  unsigned out_b = (sb * sa * 255u + dst[2] * da * inv + half) / denom;
  if (out_r > 255u) out_r = 255u;
  if (out_g > 255u) out_g = 255u;
  if (out_b > 255u) out_b = 255u;
  dst[0] = static_cast<uint8_t>(out_r);
  dst[1] = static_cast<uint8_t>(out_g);
  dst[2] = static_cast<uint8_t>(out_b);
  dst[3] = static_cast<uint8_t>(out_a);
}

}  // namespace

void CompositeMaskSourceOver(const std::vector<uint8_t>& mask, int width,
                             int height, const Rgba& color,
                             std::vector<uint8_t>* pixels) {
  for (int i = 0; i < width * height; ++i) {
    const unsigned sa =
        (static_cast<unsigned>(mask[i]) * color.a + 127u) / 255u;
    if (sa == 0u) continue;
    SourceOverPixel(color.r, color.g, color.b, sa, &(*pixels)[i * 4]);
  }
}

void BlitRgbaSourceOver(const uint8_t* src, int src_w, int src_h, int dx,
                        int dy, uint8_t* dst, int dst_w, int dst_h) {
  for (int y = 0; y < src_h; ++y) {
    const int fy = dy + y;
    if (fy < 0 || fy >= dst_h) continue;
    for (int x = 0; x < src_w; ++x) {
      const int fx = dx + x;
      if (fx < 0 || fx >= dst_w) continue;
      const uint8_t* sp = src + (static_cast<size_t>(y) * src_w + x) * 4;
      const unsigned sa = sp[3];
      if (sa == 0u) continue;
      SourceOverPixel(sp[0], sp[1], sp[2], sa,
                      dst + (static_cast<size_t>(fy) * dst_w + fx) * 4);
    }
  }
}

}  // namespace caption_track263
