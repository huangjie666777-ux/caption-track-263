#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "caption_track263/caption_track263.h"
#include "composite.h"

namespace caption_track263 {

namespace {

constexpr size_t kMaxCues = 100;
constexpr int64_t kMaxPixels = 4000000;

}  // namespace

CaptionTrack::~CaptionTrack() = default;

Error BuildCaptionTrack(const std::vector<CaptionCue>& cues,
                        const std::string& font_path,
                        const CaptionTrackOptions& options,
                        std::unique_ptr<CaptionTrack>* out_track) {
  out_track->reset();

  if (cues.size() > kMaxCues) return Error::kTooManyCues;
  if (options.bottom_margin < 0 || options.line_spacing < 0) {
    return Error::kBadMargin;
  }

  std::set<std::string> seen_ids;
  for (const CaptionCue& cue : cues) {
    if (cue.id.empty()) return Error::kBadCue;
    if (!seen_ids.insert(cue.id).second) return Error::kDuplicateId;
    if (cue.start_ms < 0 || cue.end_ms <= cue.start_ms) {
      return Error::kBadCue;
    }
  }

  // The Engine constructor performs the original font validation;
  // LayoutLine/RasterizeLine enforce the original text, size, and
  // style rules. Everything is rasterized up front so the resulting
  // track is a snapshot independent of later caller mutations.
  Engine engine(font_path);
  if (engine.GetError() != Error::kOk) return engine.GetError();

  auto track = std::unique_ptr<CaptionTrack>(new CaptionTrack());
  track->options_ = options;
  track->cues_.reserve(cues.size());

  for (const CaptionCue& cue : cues) {
    Layout layout;
    Error err = engine.LayoutLine(cue.text, options.font_size,
                                  options.base_direction, &layout);
    if (err != Error::kOk) return err;

    CaptionTrack::PreparedCue prepared;
    prepared.cue = cue;
    err = engine.RasterizeLine(layout, options.font_size,
                               options.style, &prepared.image);
    // A line with no ink is a legal caption; it simply never occupies
    // stack space and is never reported as visible.
    if (err != Error::kOk && err != Error::kNoInk) return err;
    track->cues_.push_back(std::move(prepared));
  }

  *out_track = std::move(track);
  return Error::kOk;
}

Error CaptionTrack::Sample(int64_t time_ms, const VideoFrame& frame,
                           VideoFrame* out_frame,
                           std::vector<VisibleCaption>* out_visible)
    const {
  out_frame->pixels.clear();
  out_frame->width = frame.width;
  out_frame->height = frame.height;
  if (out_visible) out_visible->clear();

  if (time_ms < 0) return Error::kBadFrame;
  if (frame.width <= 0 || frame.height <= 0 ||
      static_cast<int64_t>(frame.width) * frame.height > kMaxPixels) {
    return Error::kBadFrame;
  }
  if (frame.pixels.size() !=
      static_cast<size_t>(frame.width) * frame.height * 4) {
    return Error::kBadFrame;
  }

  // Active, inked captions in input order (first input cue is lowest).
  std::vector<const PreparedCue*> active;
  for (const PreparedCue& prepared : cues_) {
    if (prepared.cue.start_ms <= time_ms &&
        time_ms < prepared.cue.end_ms &&
        prepared.image.width > 0 && prepared.image.height > 0) {
      active.push_back(&prepared);
    }
  }

  // Copy first; the caller's frame must be untouched on failure.
  out_frame->pixels = frame.pixels;
  if (active.empty()) return Error::kOk;

  const int margin = options_.bottom_margin;
  const int spacing = options_.line_spacing;
  const int frame_w = frame.width;
  const int frame_h = frame.height;

  int64_t total_h = margin;
  for (size_t i = 0; i < active.size(); ++i) {
    const RasterImage& image = active[i]->image;
    if (image.width > frame_w) return Error::kDoesNotFit;
    total_h += image.height;
    if (i + 1 < active.size()) total_h += spacing;
  }
  if (total_h > frame_h) return Error::kDoesNotFit;

  std::vector<VisibleCaption> visible;
  visible.reserve(active.size());

  // Lay out bottom-to-top: the first active cue sits nearest the
  // bottom margin, later cues stack above it.
  int cursor_bottom = frame_h - margin;
  for (const PreparedCue* prepared : active) {
    const RasterImage& image = prepared->image;
    int x0 = (frame_w - image.width) / 2;
    int y0 = cursor_bottom - image.height;
    cursor_bottom -= image.height + spacing;

    for (int row = 0; row < image.height; ++row) {
      const uint8_t* src_row =
          &image.pixels[static_cast<size_t>(row) * image.width * 4];
      uint8_t* dst_row =
          &out_frame->pixels[(static_cast<size_t>(y0 + row) * frame_w +
                              x0) * 4];
      for (int col = 0; col < image.width; ++col) {
        const uint8_t* sp = src_row + col * 4;
        CompositeSourceOver(sp[0], sp[1], sp[2], sp[3],
                            dst_row + col * 4);
      }
    }

    VisibleCaption item;
    item.id = prepared->cue.id;
    item.rect = CaptionRect{x0, y0, image.width, image.height};
    visible.push_back(std::move(item));
  }

  if (out_visible) *out_visible = std::move(visible);
  return Error::kOk;
}

}  // namespace caption_track263
