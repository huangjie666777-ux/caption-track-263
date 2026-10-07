#include <cstdint>
#include <set>
#include <vector>

#include "caption_track263/caption_track263.h"
#include "blit.h"

namespace caption_track263 {

namespace {

constexpr size_t kMaxCaptions = 100;
constexpr int64_t kMaxFramePixels = 4000000;

}  // namespace

Error CaptionTrack::Build(Engine& engine,
                          const std::vector<Caption>& captions,
                          const TrackStyle& style, CaptionTrack* out) {
  if (engine.GetError() != Error::kOk) return engine.GetError();
  if (captions.size() > kMaxCaptions) return Error::kBadCaption;

  CaptionTrack track;
  track.style_bottom_margin_ = style.bottom_margin;
  track.style_line_spacing_ = style.line_spacing;
  std::set<std::string> ids;
  for (const Caption& caption : captions) {
    if (caption.id.empty() || !ids.insert(caption.id).second) {
      return Error::kBadCaption;
    }
    if (caption.start_ms < 0 || caption.start_ms >= caption.end_ms) {
      return Error::kBadCaption;
    }
    Layout layout;
    Error err = engine.LayoutLine(caption.text, style.font_size,
                                  style.base_direction, &layout);
    if (err != Error::kOk) return err;
    Prepared prepared;
    prepared.id = caption.id;
    prepared.start_ms = caption.start_ms;
    prepared.end_ms = caption.end_ms;
    err = engine.RasterizeLine(layout, style.font_size, style.raster,
                               &prepared.image);
    if (err == Error::kNoInk) {
      prepared.has_ink = false;  // Valid, but occupies no space.
    } else if (err != Error::kOk) {
      return err;
    } else {
      prepared.has_ink = true;
    }
    track.captions_.push_back(std::move(prepared));
  }

  *out = std::move(track);
  return Error::kOk;
}

Error CaptionTrack::Sample(uint64_t time_ms, const uint8_t* rgba,
                           size_t byte_count, int width, int height,
                           SampleResult* out) const {
  *out = SampleResult();
  if (rgba == nullptr || width <= 0 || height <= 0) {
    return Error::kBadFrame;
  }
  if (static_cast<int64_t>(width) * height > kMaxFramePixels) {
    return Error::kImageTooLarge;
  }
  if (byte_count != static_cast<size_t>(width) * height * 4) {
    return Error::kBadFrame;
  }

  out->frame.width = width;
  out->frame.height = height;
  out->frame.pixels.assign(rgba, rgba + byte_count);

  // Active captions in input order; no-ink captions occupy no space.
  std::vector<const Prepared*> active;
  for (const Prepared& caption : captions_) {
    if (!caption.has_ink) continue;
    if (static_cast<uint64_t>(caption.start_ms) <= time_ms &&
        time_ms < static_cast<uint64_t>(caption.end_ms)) {
      active.push_back(&caption);
    }
  }
  if (active.empty()) return Error::kOk;

  int64_t total_height = 0;
  for (size_t i = 0; i < active.size(); ++i) {
    if (active[i]->image.width > width) return Error::kNoFit;
    total_height += active[i]->image.height;
    if (i > 0) total_height += style_line_spacing_;
  }
  total_height += style_bottom_margin_;
  if (total_height > height) return Error::kNoFit;

  // Stack bottom-up: the first active caption sits lowest, directly
  // above the bottom margin.
  int64_t cursor = height - style_bottom_margin_;
  for (const Prepared* caption : active) {
    const RasterImage& image = caption->image;
    cursor -= image.height;
    PlacedCaption placed;
    placed.id = caption->id;
    placed.x = (width - image.width) / 2;
    placed.y = static_cast<int>(cursor);
    placed.width = image.width;
    placed.height = image.height;
    BlitRgbaSourceOver(image.pixels.data(), image.width, image.height,
                       placed.x, placed.y, out->frame.pixels.data(), width,
                       height);
    out->captions.push_back(std::move(placed));
    cursor -= style_line_spacing_;
  }
  return Error::kOk;
}

}  // namespace caption_track263
