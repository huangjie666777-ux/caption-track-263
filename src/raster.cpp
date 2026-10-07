#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_STROKER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "caption_track263/caption_track263.h"
#include "blit.h"
#include "engine_internal.h"

namespace caption_track263 {

namespace {

constexpr int kMaxPixels = 4000000;
constexpr double kMinFontSize = 8.0;
constexpr double kMaxFontSize = 128.0;

// One grayscale coverage bitmap (8-bit, 255 = full coverage) placed
// on the line's integer pixel grid (x right, y down from the baseline
// origin).
struct CoverageBitmap {
  std::vector<uint8_t> coverage;
  int width = 0;
  int height = 0;
  int x = 0;
  int y = 0;  // Top row, y down.
  bool empty() const { return width == 0 || height == 0; }
};

void MergeCoverageMax(const CoverageBitmap& src,
                      std::vector<uint8_t>* dst, int canvas_width,
                      int canvas_height, int origin_x, int origin_y) {
  for (int row = 0; row < src.height; ++row) {
    int cy = src.y + row - origin_y;
    if (cy < 0 || cy >= canvas_height) continue;
    for (int col = 0; col < src.width; ++col) {
      uint8_t cov = src.coverage[row * src.width + col];
      if (cov == 0) continue;
      int cx = src.x + col - origin_x;
      if (cx < 0 || cx >= canvas_width) continue;
      uint8_t& cell = (*dst)[cy * canvas_width + cx];
      if (cov > cell) cell = cov;
    }
  }
}

}  // namespace

Error Engine::RasterizeLine(const Layout& layout, double font_size,
                            const RasterStyle& style,
                            RasterImage* out) const {
  *out = RasterImage();
  out->line_width = layout.width;
  if (GetError() != Error::kOk) return GetError();
  if (!std::isfinite(font_size) || font_size < kMinFontSize ||
      font_size > kMaxFontSize) {
    return Error::kBadFontSize;
  }
  if (!std::isfinite(style.stroke_radius) || style.stroke_radius < 0.0 ||
      style.stroke_radius > 8.0) {
    return Error::kBadStyle;
  }
  if (style.padding < 0 || style.padding > 32) return Error::kBadStyle;
  if (!impl_->ft_library || !impl_->ft_face) return Error::kBadFont;

  FT_Face face = impl_->ft_face;
  if (FT_Set_Char_Size(face, 0,
                        static_cast<FT_F26Dot6>(std::lround(font_size * 64.0)),
                        72, 72) != 0) {
    return Error::kBadFont;
  }

  FT_Stroker stroker = nullptr;
  bool want_stroke = style.stroke_radius > 0.0 && style.stroke.a != 0;
  if (want_stroke) {
    FT_Stroker_New(impl_->ft_library, &stroker);
    FT_Stroker_Set(stroker,
                   static_cast<FT_Fixed>(std::lround(style.stroke_radius *
                                                    64.0)),
                   FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
  }

  std::vector<CoverageBitmap> fills;
  std::vector<CoverageBitmap> strokes;
  bool have_box = false;
  int box_x0 = 0, box_y0 = 0, box_x1 = 0, box_y1 = 0;

  auto absorb = [&](const CoverageBitmap& bmp) {
    if (bmp.empty()) return;
    if (!have_box) {
      box_x0 = bmp.x;
      box_y0 = bmp.y;
      box_x1 = bmp.x + bmp.width;
      box_y1 = bmp.y + bmp.height;
      have_box = true;
    } else {
      box_x0 = std::min(box_x0, bmp.x);
      box_y0 = std::min(box_y0, bmp.y);
      box_x1 = std::max(box_x1, bmp.x + bmp.width);
      box_y1 = std::max(box_y1, bmp.y + bmp.height);
    }
  };

  const FT_Int32 load_flags =
      FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;

  for (const Glyph& glyph : layout.glyphs) {
    if (glyph.glyph_id == 0) continue;
    if (FT_Load_Glyph(face, glyph.glyph_id, load_flags) != 0) continue;
    FT_GlyphSlot slot = face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) continue;

    // Subpixel pen position. Glyph coordinates use y up; FreeType
    // outlines use y up as well, so the fractional part is applied to
    // the outline and the integer floored origin moves the bitmap.
    double pen_x = glyph.x;
    double pen_y_up = glyph.y;
    int floor_x = static_cast<int>(std::floor(pen_x));
    int floor_y = static_cast<int>(std::floor(pen_y_up));
    double frac_x = pen_x - floor_x;
    double frac_y = pen_y_up - floor_y;
    FT_Outline_Translate(&slot->outline,
                          static_cast<FT_Pos>(std::lround(frac_x * 64.0)),
                          static_cast<FT_Pos>(std::lround(frac_y * 64.0)));

    FT_Glyph stroke_glyph = nullptr;
    if (want_stroke) {
      if (FT_Get_Glyph(slot, &stroke_glyph) == 0 &&
          (stroke_glyph->format != FT_GLYPH_FORMAT_OUTLINE ||
           FT_Glyph_StrokeBorder(&stroke_glyph, stroker, 0, 1) != 0 ||
           FT_Glyph_To_Bitmap(&stroke_glyph, FT_RENDER_MODE_NORMAL, nullptr,
                              1) != 0)) {
        if (stroke_glyph) FT_Done_Glyph(stroke_glyph);
        stroke_glyph = nullptr;
      }
    }

    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) == 0 &&
        slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
      CoverageBitmap fill;
      fill.width = static_cast<int>(slot->bitmap.width);
      fill.height = static_cast<int>(slot->bitmap.rows);
      fill.x = slot->bitmap_left + floor_x;
      // bitmap_top is y up relative to the pen; flip and add the
      // floored upward pen offset.
      fill.y = -(static_cast<int>(slot->bitmap_top) + floor_y);
      fill.coverage.resize(fill.width * fill.height);
      for (int row = 0; row < fill.height; ++row) {
        for (int col = 0; col < fill.width; ++col) {
          fill.coverage[row * fill.width + col] =
              slot->bitmap.buffer[row * slot->bitmap.pitch + col];
        }
      }
      fills.push_back(std::move(fill));
      absorb(fills.back());
    }

    if (stroke_glyph) {
      FT_BitmapGlyph bmp_glyph = reinterpret_cast<FT_BitmapGlyph>(stroke_glyph);
      FT_Bitmap& bmp = bmp_glyph->bitmap;
      if (bmp.width > 0 && bmp.rows > 0) {
        CoverageBitmap stroke;
        stroke.width = static_cast<int>(bmp.width);
        stroke.height = static_cast<int>(bmp.rows);
        // The glyph's left/top already include slot bearings and the
        // fractional outline translation; add the floored pen only.
        stroke.x = static_cast<int>(bmp_glyph->left) + floor_x;
        stroke.y = -(static_cast<int>(bmp_glyph->top) + floor_y);
        stroke.coverage.resize(stroke.width * stroke.height);
        for (int row = 0; row < stroke.height; ++row) {
          for (int col = 0; col < stroke.width; ++col) {
            stroke.coverage[row * stroke.width + col] =
                bmp.buffer[row * bmp.pitch + col];
          }
        }
        strokes.push_back(std::move(stroke));
        absorb(strokes.back());
      }
      FT_Done_Glyph(stroke_glyph);
    }
  }

  if (stroker) FT_Stroker_Done(stroker);

  if (!have_box) return Error::kNoInk;

  const int pad = style.padding;
  int origin_x = box_x0 - pad;
  int origin_y = box_y0 - pad;
  int width = (box_x1 - box_x0) + 2 * pad;
  int height = (box_y1 - box_y0) + 2 * pad;
  if (width <= 0 || height <= 0 ||
      static_cast<int64_t>(width) * height > kMaxPixels) {
    return Error::kImageTooLarge;
  }

  // Merge every glyph into one fill mask and one stroke mask using
  // maximum coverage, so overlapping glyphs never darken each other.
  std::vector<uint8_t> fill_mask(width * height, 0);
  std::vector<uint8_t> stroke_mask(width * height, 0);
  for (const CoverageBitmap& bmp : strokes) {
    MergeCoverageMax(bmp, &stroke_mask, width, height, origin_x, origin_y);
  }
  for (const CoverageBitmap& bmp : fills) {
    MergeCoverageMax(bmp, &fill_mask, width, height, origin_x, origin_y);
  }

  std::vector<uint8_t> pixels(width * height * 4, 0);
  // Stroke first, fill on top: the fill never gets covered by the
  // outline of an adjacent glyph.
  CompositeMaskSourceOver(stroke_mask, width, height, style.stroke,
                          &pixels);
  CompositeMaskSourceOver(fill_mask, width, height, style.fill, &pixels);

  bool has_ink = false;
  for (size_t i = 3; i < pixels.size(); i += 4) {
    if (pixels[i] != 0) {
      has_ink = true;
      break;
    }
  }
  if (!has_ink) return Error::kNoInk;

  out->pixels = std::move(pixels);
  out->width = width;
  out->height = height;
  out->origin_x = origin_x;
  out->origin_y = origin_y;
  return Error::kOk;
}

}  // namespace caption_track263
