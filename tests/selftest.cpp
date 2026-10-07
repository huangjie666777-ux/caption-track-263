#include <cmath>
#include <cstdio>
#include <png.h>
#include <string>
#include <vector>

#include "caption_track263/caption_track263.h"

using namespace caption_track263;

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

static bool LaysOut(Engine* e, const std::string& text,
                    BaseDirection dir, Layout* out) {
  return e->LayoutLine(text, 20.0, dir, out) == Error::kOk;
}

int main() {
  Engine engine("fonts/DejaVuSans.ttf");
  CHECK(engine.GetError() == Error::kOk);

  Layout lay;

  // Empty text succeeds.
  CHECK(LaysOut(&engine, "", BaseDirection::kAuto, &lay));
  CHECK(lay.glyphs.empty() && lay.width == 0.0);

  // Invalid inputs.
  CHECK(engine.LayoutLine("\xff\xfe", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kInvalidUtf8);
  CHECK(engine.LayoutLine("a\xC0\xAF", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kInvalidUtf8);  // Overlong.
  CHECK(engine.LayoutLine("a\nb", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kNewline);
  CHECK(engine.LayoutLine("a", 0.0, BaseDirection::kAuto, &lay) ==
        Error::kBadFontSize);
  CHECK(engine.LayoutLine("a", -3.0, BaseDirection::kAuto, &lay) ==
        Error::kBadFontSize);
  {
    std::string big;
    for (int i = 0; i < 4097; ++i) big += 'a';
    CHECK(engine.LayoutLine(big, 20.0, BaseDirection::kAuto, &lay) ==
          Error::kTooLong);
  }
  // Missing glyph: CJK is not in DejaVuSans.
  CHECK(engine.LayoutLine("\u4E2D", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kMissingGlyph);

  // Pure Latin LTR.
  CHECK(LaysOut(&engine, "Hello", BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kLtr);
  CHECK(lay.glyphs.size() == 5);
  CHECK(lay.width > 0.0);
  for (size_t i = 0; i < 5; ++i) {
    CHECK(lay.glyphs[i].cluster_begin == i);
    CHECK(lay.glyphs[i].cluster_end == i + 1);
  }

  // Pure Hebrew, auto direction resolves RTL.
  const std::string hebrew = "\u05E9\u05DC\u05D5\u05DD";
  CHECK(LaysOut(&engine, hebrew, BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kRtl);
  CHECK(lay.glyphs.size() == 4);
  // Visual order is reversed: first glyph is the last character.
  CHECK(lay.glyphs[0].cluster_begin == 6);
  CHECK(lay.glyphs[3].cluster_begin == 0);

  // Mixed Latin + Hebrew + digits.
  const std::string mixed = "ab " + hebrew + " 12";
  CHECK(LaysOut(&engine, mixed, BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kLtr);
  CHECK(lay.width > 0.0);
  // First three glyphs are "ab " in logical order.
  CHECK(lay.glyphs[0].cluster_begin == 0);
  CHECK(lay.glyphs[1].cluster_begin == 1);
  CHECK(lay.glyphs[2].cluster_begin == 2);
  // Per UBA, the trailing European digits stay LTR and land visually
  // before the Hebrew word: a b sp 1 2 sp <reversed Hebrew> sp.
  CHECK(lay.glyphs[3].cluster_begin == 12);  // '1'
  CHECK(lay.glyphs[4].cluster_begin == 13);  // '2'
  size_t last = lay.glyphs.size() - 1;
  CHECK(lay.glyphs[last].cluster_begin == 3);  // First Hebrew letter.

  // Arabic shaping: 4 letters join, glyph count stays 4, advances > 0.
  const std::string arabic = "\u0645\u0631\u062D\u0628\u0627";
  CHECK(LaysOut(&engine, arabic, BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kRtl);
  CHECK(lay.glyphs.size() == 5);
  for (const auto& g : lay.glyphs) CHECK(g.advance_x > 0.0);

  // Combining mark merges into one cluster covering both code points.
  const std::string accent = "e\u0301";
  CHECK(LaysOut(&engine, accent, BaseDirection::kLtr, &lay));
  CHECK(lay.glyphs.size() >= 1);
  CHECK(lay.glyphs[0].cluster_begin == 0);
  CHECK(lay.glyphs[0].cluster_end == accent.size());

  // Mirroring: "(" in RTL context shapes as its mirror glyph.
  Layout ltr_paren, rtl_paren;
  CHECK(LaysOut(&engine, "(", BaseDirection::kLtr, &ltr_paren));
  CHECK(LaysOut(&engine, "(", BaseDirection::kRtl, &rtl_paren));
  CHECK(ltr_paren.glyphs[0].glyph_id != rtl_paren.glyphs[0].glyph_id);

  // Bidi isolate controls produce no visible glyphs.
  const std::string iso = "a\u2066\u05D1\u2069b";
  CHECK(LaysOut(&engine, iso, BaseDirection::kLtr, &lay));
  for (const auto& g : lay.glyphs) {
    if (g.cluster_begin == 1 || g.cluster_begin == 6) {
      CHECK(g.glyph_id == 0 && g.advance_x == 0.0);
    }
  }

  // Space keeps a real advance.
  CHECK(LaysOut(&engine, "a b", BaseDirection::kLtr, &lay));
  CHECK(lay.glyphs[1].advance_x > 0.0);

  // Selection: full range covers the whole line width.
  std::vector<SelectionRange> ranges;
  CHECK(LaysOut(&engine, mixed, BaseDirection::kAuto, &lay));
  CHECK(engine.SelectionRanges(lay, 0, mixed.size(), &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(std::fabs(ranges[0].x0 - 0.0) < 1e-6);
  CHECK(std::fabs(ranges[0].x1 - lay.width) < 1e-6);

  // Empty selection returns empty.
  CHECK(engine.SelectionRanges(lay, 2, 2, &ranges) == Error::kOk);
  CHECK(ranges.empty());

  // Latin-only prefix selection is a single range at the left edge.
  CHECK(engine.SelectionRanges(lay, 0, 2, &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(ranges[0].x0 == 0.0);
  CHECK(ranges[0].x1 > 0.0 && ranges[0].x1 < lay.width);

  // Hebrew word selection lands in the middle of the line.
  CHECK(engine.SelectionRanges(lay, 3, 3 + 8, &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(ranges[0].x0 > 0.0);

  // Selection crossing the boundary yields two ranges (gap unselected).
  CHECK(engine.SelectionRanges(lay, 0, 3 + 8, &ranges) == Error::kOk);
  CHECK(ranges.size() == 2);
  CHECK(ranges[1].x0 > ranges[0].x1);

  // Partial cluster coverage selects the whole cluster.
  CHECK(LaysOut(&engine, accent, BaseDirection::kLtr, &lay));
  CHECK(engine.SelectionRanges(lay, 0, 1, &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(std::fabs(ranges[0].x1 - lay.width) < 1e-6);

  // Bad selections are rejected.
  CHECK(engine.SelectionRanges(lay, 2, 1, &ranges) == Error::kBadSelection);
  CHECK(engine.SelectionRanges(lay, 0, lay.text_size + 1, &ranges) ==
        Error::kBadSelection);
  CHECK(engine.SelectionRanges(lay, 1, 2, &ranges) ==
        Error::kBadSelection);  // Mid-code-point (U+0301 is 2 bytes).

  // ---- Rasterization ------------------------------------------------
  RasterStyle style;
  style.fill = Rgba{255, 255, 255, 255};
  style.stroke = Rgba{0, 0, 0, 255};
  style.stroke_radius = 2.0;
  style.padding = 4;

  // Basic Latin raster.
  RasterImage img;
  CHECK(LaysOut(&engine, "Hello", BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 20.0, style, &img) == Error::kOk);
  CHECK(img.width > 0 && img.height > 0);
  CHECK(img.pixels.size() ==
        static_cast<size_t>(img.width) * img.height * 4);
  CHECK(std::fabs(img.line_width - lay.width) < 1e-9);
  // Canvas left/top edge is up-left of the baseline origin.
  CHECK(img.origin_x <= 0 && img.origin_y < 0);
  // Padding: glyph ink never touches the canvas border.
  bool found_white = false, found_black = false, border_clean = true;
  for (int y = 0; y < img.height; ++y) {
    for (int x = 0; x < img.width; ++x) {
      const uint8_t* p = &img.pixels[(y * img.width + x) * 4];
      if (p[3] == 0) {
        if (!(p[0] == 0 && p[1] == 0 && p[2] == 0)) border_clean = false;
      } else if (p[0] > 200 && p[1] > 200 && p[2] > 200) {
        found_white = true;
      } else if (p[0] < 60 && p[1] < 60 && p[2] < 60) {
        found_black = true;
      }
      bool border = x == 0 || y == 0 || x == img.width - 1 ||
                    y == img.height - 1;
      if (border && p[3] != 0) border_clean = false;
    }
  }
  CHECK(found_white && found_black);  // Fill and outline colors.
  CHECK(border_clean);

  // No outline: no near-black pixels.
  RasterStyle fill_only = style;
  fill_only.stroke_radius = 0.0;
  fill_only.padding = 0;
  CHECK(engine.RasterizeLine(lay, 20.0, fill_only, &img) == Error::kOk);
  found_black = false;
  for (size_t i = 0; i < img.pixels.size(); i += 4) {
    if (img.pixels[i + 3] != 0 && img.pixels[i] < 100 &&
        img.pixels[i + 1] < 100 && img.pixels[i + 2] < 100) {
      found_black = true;
    }
  }
  CHECK(!found_black);

  // Empty and whitespace-only lines produce no ink, keep line width.
  CHECK(LaysOut(&engine, "", BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 20.0, style, &img) == Error::kNoInk);
  CHECK(img.width == 0 && img.height == 0 && img.pixels.empty());
  CHECK(std::fabs(img.line_width - lay.width) < 1e-9);
  CHECK(LaysOut(&engine, "   ", BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 20.0, style, &img) == Error::kNoInk);
  CHECK(img.width == 0 && std::fabs(img.line_width - lay.width) < 1e-9);

  // Fully transparent colors produce no ink.
  RasterStyle invisible = style;
  invisible.fill.a = 0;
  invisible.stroke.a = 0;
  CHECK(LaysOut(&engine, "Hi", BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 20.0, invisible, &img) == Error::kNoInk);

  // Parameter validation.
  CHECK(engine.RasterizeLine(lay, 7.0, style, &img) == Error::kBadFontSize);
  CHECK(engine.RasterizeLine(lay, 129.0, style, &img) ==
        Error::kBadFontSize);
  RasterStyle bad = style;
  bad.stroke_radius = 9.0;
  CHECK(engine.RasterizeLine(lay, 20.0, bad, &img) == Error::kBadStyle);
  bad = style;
  bad.stroke_radius = std::nan("");
  CHECK(engine.RasterizeLine(lay, 20.0, bad, &img) == Error::kBadStyle);
  bad = style;
  bad.padding = 33;
  CHECK(engine.RasterizeLine(lay, 20.0, bad, &img) == Error::kBadStyle);
  bad = style;
  bad.padding = -1;
  CHECK(engine.RasterizeLine(lay, 20.0, bad, &img) == Error::kBadStyle);

  // Size limits: 8..128 pixels and padding 0..32.
  CHECK(engine.RasterizeLine(lay, 8.0, style, &img) == Error::kOk);
  CHECK(engine.RasterizeLine(lay, 128.0, style, &img) == Error::kOk);

  // 4,000,000 pixel cap: 4096 letters at 128 px vastly exceeds it.
  std::string huge;
  for (int i = 0; i < 4096; ++i) huge += 'm';
  CHECK(engine.LayoutLine(huge, 128.0, BaseDirection::kLtr, &lay) ==
        Error::kOk);
  CHECK(engine.RasterizeLine(lay, 128.0, style, &img) ==
        Error::kImageTooLarge);

  // Mixed-direction line rasterizes with the shaped glyphs.
  CHECK(LaysOut(&engine, mixed, BaseDirection::kAuto, &lay));
  CHECK(engine.RasterizeLine(lay, 20.0, style, &img) == Error::kOk);
  CHECK(img.width > 0);

  // Combining mark is included: raster height with the mark exceeds
  // that of the base letter alone.
  RasterStyle no_pad = style;
  no_pad.padding = 0;
  no_pad.stroke_radius = 0.0;
  RasterImage base_img, mark_img;
  CHECK(LaysOut(&engine, "e", BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 32.0, no_pad, &base_img) == Error::kOk);
  CHECK(LaysOut(&engine, accent, BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 32.0, no_pad, &mark_img) == Error::kOk);
  CHECK(mark_img.height > base_img.height ||
        mark_img.width > base_img.width);

  // Subpixel placement: a 20.5 px size renders differently than 20 px.
  RasterImage sp1, sp2;
  CHECK(LaysOut(&engine, "ag", BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 20.0, fill_only, &sp1) == Error::kOk);
  CHECK(engine.RasterizeLine(lay, 20.5, fill_only, &sp2) == Error::kOk);
  bool pixels_differ = sp1.width != sp2.width || sp1.height != sp2.height;
  if (!pixels_differ) {
    for (size_t i = 0; i < sp1.pixels.size(); ++i) {
      if (sp1.pixels[i] != sp2.pixels[i]) {
        pixels_differ = true;
        break;
      }
    }
  }
  CHECK(pixels_differ);

  // PNG round trip preserves RGBA content byte-for-byte.
  CHECK(LaysOut(&engine, "Hi", BaseDirection::kLtr, &lay));
  CHECK(engine.RasterizeLine(lay, 24.0, style, &img) == Error::kOk);
  const char* png_path = "build/selftest_raster.png";
  CHECK(Engine::SavePng(img, png_path) == Error::kOk);
  {
    FILE* fp = std::fopen(png_path, "rb");
    CHECK(fp != nullptr);
    png_structp rp = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                            nullptr, nullptr, nullptr);
    png_infop ri = png_create_info_struct(rp);
    png_init_io(rp, fp);
    png_read_png(rp, ri, PNG_TRANSFORM_IDENTITY, nullptr);
    CHECK(png_get_image_width(rp, ri) ==
          static_cast<png_uint_32>(img.width));
    CHECK(png_get_image_height(rp, ri) ==
          static_cast<png_uint_32>(img.height));
    CHECK(png_get_bit_depth(rp, ri) == 8);
    CHECK(png_get_color_type(rp, ri) == PNG_COLOR_TYPE_RGBA);
    png_bytep* rows = png_get_rows(rp, ri);
    bool same = true;
    for (int y = 0; y < img.height && same; ++y) {
      for (int x = 0; x < img.width * 4; ++x) {
        if (rows[y][x] != img.pixels[y * img.width * 4 + x]) {
          same = false;
          break;
        }
      }
    }
    CHECK(same);
    png_destroy_read_struct(&rp, &ri, nullptr);
    std::fclose(fp);
    std::remove(png_path);
  }
  CHECK(Engine::SavePng(img, "/nonexistent-dir/x.png") == Error::kFileIo);

  if (failures == 0) {
    std::printf("all selftests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
