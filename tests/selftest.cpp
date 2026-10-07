#include <cmath>
#include <cstdio>
#include <memory>
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

  // ---- Timed caption track -----------------------------------------
  {
    std::vector<CaptionCue> cues = {
        {"a", "First", 0, 1000},
        {"b", "Second \u0645\u0631\u062D\u0628\u0627", 500, 1500},
        {"spaces", "   ", 0, 5000},  // No ink: never occupies space.
        {"c", "Late", 9000, 10000},
    };
    CaptionTrackOptions opts;
    opts.font_size = 20.0;
    opts.style = style;
    opts.bottom_margin = 6;
    opts.line_spacing = 4;

    std::unique_ptr<CaptionTrack> track;
    CHECK(BuildCaptionTrack(cues, "fonts/DejaVuSans.ttf", opts,
                            &track) == Error::kOk);

    // Build failures leave no track.
    std::unique_ptr<CaptionTrack> bad_track;
    std::vector<CaptionCue> empty_id = cues;
    empty_id[0].id.clear();
    CHECK(BuildCaptionTrack(empty_id, "fonts/DejaVuSans.ttf", opts,
                            &bad_track) == Error::kBadCue);
    CHECK(!bad_track);
    std::vector<CaptionCue> dup = cues;
    dup[1].id = "a";
    CHECK(BuildCaptionTrack(dup, "fonts/DejaVuSans.ttf", opts,
                            &bad_track) == Error::kDuplicateId);
    CHECK(!bad_track);
    std::vector<CaptionCue> bad_time = cues;
    bad_time[0].start_ms = -1;
    CHECK(BuildCaptionTrack(bad_time, "fonts/DejaVuSans.ttf", opts,
                            &bad_track) == Error::kBadCue);
    bad_time = cues;
    bad_time[0].end_ms = bad_time[0].start_ms;
    CHECK(BuildCaptionTrack(bad_time, "fonts/DejaVuSans.ttf", opts,
                            &bad_track) == Error::kBadCue);
    CaptionTrackOptions bad_margin = opts;
    bad_margin.bottom_margin = -1;
    CHECK(BuildCaptionTrack(cues, "fonts/DejaVuSans.ttf", bad_margin,
                            &bad_track) == Error::kBadMargin);
    std::vector<CaptionCue> invalid_text = cues;
    invalid_text[0].text = "\xff";
    CHECK(BuildCaptionTrack(invalid_text, "fonts/DejaVuSans.ttf", opts,
                            &bad_track) == Error::kInvalidUtf8);
    CHECK(BuildCaptionTrack(cues, "/no/such/font.ttf", opts,
                            &bad_track) == Error::kBadFont);
    std::vector<CaptionCue> too_many;
    for (int i = 0; i < 101; ++i) {
      too_many.push_back({"id" + std::to_string(i), "x",
                          static_cast<int64_t>(i),
                          static_cast<int64_t>(i + 1)});
    }
    CHECK(BuildCaptionTrack(too_many, "fonts/DejaVuSans.ttf", opts,
                            &bad_track) == Error::kTooManyCues);

    VideoFrame frame;
    frame.width = 200;
    frame.height = 120;
    frame.pixels.assign(frame.width * frame.height * 4, 0);
    for (int y = 0; y < frame.height; ++y) {
      for (int x = 0; x < frame.width; ++x) {
        uint8_t* p = &frame.pixels[(y * frame.width + x) * 4];
        p[0] = 30; p[1] = 60; p[2] = 90; p[3] = 128;
      }
    }

    // Invalid frames and negative times.
    VideoFrame out;
    std::vector<VisibleCaption> visible;
    CHECK(track->Sample(0, frame, &out, &visible) == Error::kOk);
    VideoFrame zero_w = frame; zero_w.width = 0;
    CHECK(track->Sample(0, zero_w, &out, &visible) == Error::kBadFrame);
    VideoFrame bad_bytes = frame; bad_bytes.pixels.pop_back();
    CHECK(track->Sample(0, bad_bytes, &out, &visible) == Error::kBadFrame);
    CHECK(track->Sample(-1, frame, &out, &visible) == Error::kBadFrame);
    VideoFrame huge; huge.width = 4000; huge.height = 1001;
    huge.pixels.assign(10, 0);
    CHECK(track->Sample(0, huge, &out, &visible) == Error::kBadFrame);

    // No active caption: exact copy and empty visible list.
    CHECK(track->Sample(8000, frame, &out, &visible) == Error::kOk);
    CHECK(visible.empty());
    CHECK(out.width == frame.width && out.height == frame.height);
    CHECK(out.pixels == frame.pixels);

    // Boundaries: [start, end). At 0 only "a" is active; at 999
    // both "a" and "b" (which starts at 500) are visible; at 1000
    // "a" has expired and only "b" remains.
    CHECK(track->Sample(0, frame, &out, &visible) == Error::kOk);
    CHECK(visible.size() == 1 && visible[0].id == "a");
    CHECK(track->Sample(999, frame, &out, &visible) == Error::kOk);
    CHECK(visible.size() == 2 && visible[0].id == "a" &&
          visible[1].id == "b");
    CHECK(track->Sample(1000, frame, &out, &visible) == Error::kOk);
    CHECK(visible.size() == 1 && visible[0].id == "b");

    // Overlap: input order bottom-to-top; "a" below "b";
    // no-ink "spaces" is never reported nor placed.
    CHECK(track->Sample(700, frame, &out, &visible) == Error::kOk);
    CHECK(visible.size() == 2);
    CHECK(visible[0].id == "a" && visible[1].id == "b");
    CHECK(visible[0].rect.y > visible[1].rect.y);
    for (const auto& v : visible) {
      CHECK(v.rect.x >= 0);
      CHECK(v.rect.x + v.rect.width <= frame.width);
      CHECK(v.rect.y >= 0);
      CHECK(v.rect.y + v.rect.height <= frame.height);
    }
    // Horizontal centering.
    int ax0 = visible[0].rect.x;
    CHECK(ax0 == (frame.width - visible[0].rect.width) / 2);

    // Some pixels changed, untouched background kept color and alpha.
    bool changed = false;
    bool bg_kept = true;
    for (size_t i = 0; i < frame.pixels.size(); i += 4) {
      if (out.pixels[i] != frame.pixels[i] ||
          out.pixels[i + 3] != frame.pixels[i + 3]) {
        changed = true;
      } else if (out.pixels[i] != 30 || out.pixels[i + 1] != 60 ||
                 out.pixels[i + 2] != 90 || out.pixels[i + 3] != 128) {
        bg_kept = false;
      }
    }
    CHECK(changed && bg_kept);

    // Random seek backwards and forwards is independent of history.
    CHECK(track->Sample(9500, frame, &out, &visible) == Error::kOk);
    CHECK(visible.size() == 1 && visible[0].id == "c");
    CHECK(track->Sample(500, frame, &out, &visible) == Error::kOk);
    CHECK(visible.size() == 2 && visible[0].id == "a");

    // Input frame is never modified.
    VideoFrame before = frame;
    CHECK(track->Sample(700, frame, &out, &visible) == Error::kOk);
    CHECK(frame.pixels == before.pixels);

    // Snapshot: mutating the cue list after build has no effect.
    cues.clear();
    CHECK(track->Sample(700, frame, &out, &visible) == Error::kOk);
    CHECK(visible.size() == 2);

    // Too wide: whole call fails, no half image.
    VideoFrame narrow;
    narrow.width = 4; narrow.height = 200;
    narrow.pixels.assign(4 * 200 * 4, 0);
    CHECK(track->Sample(0, narrow, &out, &visible) == Error::kDoesNotFit);

    // Too tall: margins plus stacked captions exceed the frame.
    CaptionTrackOptions tight_opts = opts;
    tight_opts.bottom_margin = 100;
    std::unique_ptr<CaptionTrack> tight_track;
    std::vector<CaptionCue> single = {{"x", "Hi", 0, 1000}};
    CHECK(BuildCaptionTrack(single, "fonts/DejaVuSans.ttf", tight_opts,
                            &tight_track) == Error::kOk);
    CHECK(tight_track->Sample(0, frame, &out, &visible) ==
          Error::kDoesNotFit);
  }

  // CompositeMask regression: white fill and white stroke with alpha
  // 128 must stay white, not wrap to black.
  {
    RasterStyle half_white;
    half_white.fill = Rgba{255, 255, 255, 128};
    half_white.stroke = Rgba{255, 255, 255, 128};
    half_white.stroke_radius = 2.0;
    half_white.padding = 2;
    Layout wlay;
    RasterImage wimg;
    CHECK(LaysOut(&engine, "OO", BaseDirection::kLtr, &wlay));
    CHECK(engine.RasterizeLine(wlay, 40.0, half_white, &wimg) ==
          Error::kOk);
    for (size_t i = 0; i < wimg.pixels.size(); i += 4) {
      if (wimg.pixels[i + 3] != 0) {
        CHECK(wimg.pixels[i] >= wimg.pixels[i + 3]);
        CHECK(wimg.pixels[i] != 0);
      }
    }
  }

  if (failures == 0) {
    std::printf("all selftests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
