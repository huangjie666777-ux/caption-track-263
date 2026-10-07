// Mixed Arabic/Hebrew/Latin line layout and selection mapping demo.
#include <cstdio>
#include <string>
#include <vector>

#include "caption_track263/caption_track263.h"

using caption_track263::BaseDirection;
using caption_track263::Caption;
using caption_track263::CaptionTrack;
using caption_track263::Engine;
using caption_track263::Error;
using caption_track263::Layout;
using caption_track263::RasterImage;
using caption_track263::RasterStyle;
using caption_track263::Rgba;
using caption_track263::SampleResult;
using caption_track263::SelectionRange;
using caption_track263::TrackStyle;

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

  // Timed caption track: overlapping subtitles burned into video
  // frames, sampled at arbitrary (non-monotonic) times.
  std::vector<Caption> captions = {
      {"en", "Hello \u0645\u0631\u062D\u0628\u0627 world", 1000, 3000},
      {"he", "\u05E9\u05DC\u05D5\u05DD subtitle", 2000, 4000},
  };
  TrackStyle track_style;
  track_style.font_size = 28.0;
  track_style.raster.fill = Rgba{255, 255, 255, 255};
  track_style.raster.stroke = Rgba{20, 20, 20, 255};
  track_style.raster.stroke_radius = 2.0;
  track_style.raster.padding = 6;
  track_style.bottom_margin = 24;
  track_style.line_spacing = 8;
  CaptionTrack track;
  Error track_err = CaptionTrack::Build(engine, captions, track_style,
                                        &track);
  if (track_err != Error::kOk) {
    std::fprintf(stderr, "track build failed: %s\n",
                 ErrorMessage(track_err));
    return 1;
  }

  // Synthetic 640x360 "video" frame: a vertical blue gradient.
  const int fw = 640, fh = 360;
  std::vector<uint8_t> video(fw * fh * 4);
  for (int y = 0; y < fh; ++y) {
    for (int x = 0; x < fw; ++x) {
      uint8_t* px = &video[(y * fw + x) * 4];
      px[0] = static_cast<uint8_t>(30 + x * 60 / fw);
      px[1] = static_cast<uint8_t>(40 + y * 80 / fh);
      px[2] = static_cast<uint8_t>(120 + y * 100 / fh);
      px[3] = 255;
    }
  }

  // Seek around: only "en", then both overlapping, back before any,
  // then only "he" -- sampling is stateless.
  const uint64_t times[] = {1500, 2500, 500, 3500};
  for (uint64_t t : times) {
    SampleResult sample;
    track_err = track.Sample(t, video.data(), video.size(), fw, fh,
                             &sample);
    if (track_err != Error::kOk) {
      std::fprintf(stderr, "sample at %llu ms failed: %s\n",
                   static_cast<unsigned long long>(t),
                   ErrorMessage(track_err));
      return 1;
    }
    char path[128];
    std::snprintf(path, sizeof(path), "build/frame_%04llums.png",
                  static_cast<unsigned long long>(t));
    track_err = Engine::SavePng(sample.frame, path);
    if (track_err != Error::kOk) {
      std::fprintf(stderr, "png save failed: %s\n",
                   ErrorMessage(track_err));
      return 1;
    }
    std::printf("frame t=%llu ms -> %s\n",
                static_cast<unsigned long long>(t), path);
    for (const auto& placed : sample.captions) {
      std::printf("  caption %s at (%d, %d) %dx%d px\n",
                  placed.id.c_str(), placed.x, placed.y, placed.width,
                  placed.height);
    }
    if (sample.captions.empty()) std::printf("  (no active captions)\n");
  }
  return 0;
}
