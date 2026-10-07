// Mixed Arabic/Hebrew/Latin line layout and selection mapping demo.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "caption_track263/caption_track263.h"

using caption_track263::BaseDirection;
using caption_track263::BuildCaptionTrack;
using caption_track263::CaptionCue;
using caption_track263::CaptionTrackOptions;
using caption_track263::Engine;
using caption_track263::Error;
using caption_track263::Layout;
using caption_track263::RasterImage;
using caption_track263::RasterStyle;
using caption_track263::Rgba;
using caption_track263::SelectionRange;
using caption_track263::VideoFrame;
using caption_track263::VisibleCaption;

static void Dump(Engine& engine, const std::string& text,
                 BaseDirection dir) {
  Layout layout;
  Error err = engine.LayoutLine(text, 24.0, dir, &layout);
  if (err != Error::kOk) {
    std::printf("layout failed: %s\n", caption_track263::ErrorMessage(err));
    return;
  }
  std::printf("text: %s\n", text.c_str());
  std::printf("  direction: %s  width: %.2f px  glyphs: %zu\n",
              layout.resolved_direction == BaseDirection::kLtr ? "LTR"
                                                               : "RTL",
              layout.width, layout.glyphs.size());
  for (const auto& g : layout.glyphs) {
    std::printf(
        "  gid=%5u cluster=[%2zu,%2zu) x=%7.2f y=%5.2f adv=(%.2f,%.2f) "
        "off=(%.2f,%.2f)\n",
        g.glyph_id, g.cluster_begin, g.cluster_end, g.x, g.y,
        g.advance_x, g.advance_y, g.offset_x, g.offset_y);
  }
}

static void DumpSelection(const Engine& engine, const Layout& layout,
                          size_t begin, size_t end) {
  std::vector<SelectionRange> ranges;
  Error err = engine.SelectionRanges(layout, begin, end, &ranges);
  if (err != Error::kOk) {
    std::printf("  selection [%zu,%zu): %s\n", begin, end,
                caption_track263::ErrorMessage(err));
    return;
  }
  std::printf("  selection [%zu,%zu):", begin, end);
  for (const auto& r : ranges) std::printf("  [%.2f, %.2f)", r.x0, r.x1);
  std::printf("\n");
}

int main() {
  Engine engine("fonts/DejaVuSans.ttf");
  if (engine.GetError() != Error::kOk) {
    std::fprintf(stderr, "font error: %s\n",
                 caption_track263::ErrorMessage(engine.GetError()));
    return 1;
  }

  // Hebrew + Latin + digits.
  const std::string mixed_he =
      "abc \u05E9\u05DC\u05D5\u05DD 123 xyz";
  Layout layout;
  Dump(engine, mixed_he, BaseDirection::kAuto);
  engine.LayoutLine(mixed_he, 24.0, BaseDirection::kAuto, &layout);
  // Select the Hebrew word plus one trailing Latin letter.
  size_t heb_begin = 4;
  size_t heb_end = 4 + 8 + 1;  // 4 Hebrew letters (2 bytes each) + space.
  DumpSelection(engine, layout, heb_begin, heb_end);
  DumpSelection(engine, layout, 0, 3);
  DumpSelection(engine, layout, 0, mixed_he.size());

  // Arabic with combining marks, Latin, and a bidi isolate.
  const std::string mixed_ar =
      "\u0645\u0631\u062D\u0628\u0627 file\u2066"
      "\u05E0\u05D9\u05E1\u05D9\u05D5\u05DF\u2069 (v2)!";
  Dump(engine, mixed_ar, BaseDirection::kAuto);

  // Combining mark cluster: "e" + U+0301.
  Dump(engine, "e\u0301clair", BaseDirection::kLtr);

  // Rasterize a mixed Arabic/Latin subtitle line to a transparent
  // RGBA PNG that can be composited straight onto video frames.
  const std::string subtitle_text =
      "Hello \u0645\u0631\u062D\u0628\u0627 \u05E9\u05DC\u05D5\u05DD!";
  Layout raster_layout;
  Error raster_err =
      engine.LayoutLine(subtitle_text, 40.0, BaseDirection::kAuto,
                        &raster_layout);
  if (raster_err != Error::kOk) {
    std::fprintf(stderr, "subtitle layout failed: %s\n",
                 ErrorMessage(raster_err));
    return 1;
  }
  RasterStyle style;
  style.fill = Rgba{255, 255, 255, 255};
  style.stroke = Rgba{20, 20, 20, 255};
  style.stroke_radius = 2.5;
  style.padding = 10;
  RasterImage image;
  raster_err = engine.RasterizeLine(raster_layout, 40.0, style, &image);
  if (raster_err != Error::kOk) {
    std::fprintf(stderr, "raster failed: %s\n",
                 ErrorMessage(raster_err));
    return 1;
  }
  const char* out_path = "build/demo_subtitle.png";
  raster_err = Engine::SavePng(image, out_path);
  if (raster_err != Error::kOk) {
    std::fprintf(stderr, "png save failed: %s\n",
                 ErrorMessage(raster_err));
    return 1;
  }
  std::printf("\nsubtitle raster: %s\n", out_path);
  std::printf("  canvas: %dx%d px, line width: %.2f px\n", image.width,
              image.height, image.line_width);
  std::printf("  canvas top-left relative to baseline origin: (%d, %d)\n",
              image.origin_x, image.origin_y);

  // Timed caption track: overlapping cues rendered at seeked times.
  std::vector<CaptionCue> cues = {
      {"bottom", "Hello world", 0, 2000},
      {"top", "\u0645\u0631\u062D\u0628\u0627", 1000, 3000},
      {"late", "seek target", 5000, 6000},
  };
  CaptionTrackOptions track_options;
  track_options.font_size = 26.0;
  track_options.style.fill = Rgba{255, 255, 255, 255};
  track_options.style.stroke = Rgba{0, 0, 0, 220};
  track_options.style.stroke_radius = 1.5;
  track_options.style.padding = 4;
  track_options.bottom_margin = 12;
  track_options.line_spacing = 8;

  std::unique_ptr<caption_track263::CaptionTrack> track;
  Error track_err =
      BuildCaptionTrack(cues, "fonts/DejaVuSans.ttf", track_options,
                        &track);
  if (track_err != Error::kOk) {
    std::fprintf(stderr, "track build failed: %s\n",
                 ErrorMessage(track_err));
    return 1;
  }

  VideoFrame source;
  source.width = 320;
  source.height = 180;
  source.pixels.assign(source.width * source.height * 4, 0);
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      uint8_t* p = &source.pixels[(y * source.width + x) * 4];
      p[0] = static_cast<uint8_t>(20 + x / 3);
      p[1] = static_cast<uint8_t>(40 + y / 3);
      p[2] = 140;
      p[3] = 180;  // Semi-transparent background is preserved.
    }
  }

  // Seek arbitrarily: overlap, back before overlap, then forward.
  const int64_t sample_times[] = {1500, 500, 5500};
  for (int64_t t : sample_times) {
    VideoFrame rendered;
    std::vector<VisibleCaption> visible;
    track_err = track->Sample(t, source, &rendered, &visible);
    if (track_err != Error::kOk) {
      std::fprintf(stderr, "sample at %lld ms failed: %s\n",
                   static_cast<long long>(t), ErrorMessage(track_err));
      return 1;
    }
    char out[256];
    std::snprintf(out, sizeof(out), "build/track_%04lldms.png",
                  static_cast<long long>(t));
    Engine::SaveFramePng(rendered, out);
    std::printf("\ntrack sample t=%lld ms: %s\n",
                static_cast<long long>(t), out);
    for (const auto& v : visible) {
      std::printf("  id=%s rect=(%d,%d %dx%d)\n", v.id.c_str(),
                  v.rect.x, v.rect.y, v.rect.width, v.rect.height);
    }
  }
  return 0;
}
