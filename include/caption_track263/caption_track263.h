#ifndef CAPTION_TRACK263_CAPTION_TRACK263_H
#define CAPTION_TRACK263_CAPTION_TRACK263_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace caption_track263 {

enum class BaseDirection {
  kAuto,  // First strong directional character wins; LTR if none.
  kLtr,
  kRtl,
};

enum class Error {
  kOk = 0,
  kInvalidUtf8,    // Malformed UTF-8 byte sequence.
  kNewline,        // Input contains a line break character.
  kTooLong,        // More than 4096 code points.
  kMissingGlyph,   // Font has no glyph for a printable character.
  kBadFont,        // Font file could not be opened or parsed.
  kBadFontSize,    // Font size is not a positive finite number.
  kBadSelection,   // Selection range is inverted, out of bounds, or
                   // does not fall on code point boundaries.
  kBadStyle,       // Style parameter is out of range.
  kImageTooLarge,  // Rasterized canvas would exceed 4,000,000 pixels.
  kNoInk,          // Line has no visible pixels; nothing was rendered.
  kFileIo,         // PNG file could not be written.
  kBadCue,         // Cue id/text/time range is invalid.
  kTooManyCues,    // More than 100 cues were supplied.
  kDuplicateId,    // Two cues share the same non-empty id.
  kBadMargin,      // Bottom margin or line spacing is invalid.
  kBadFrame,       // Frame dimensions or pixel buffer are invalid.
  kDoesNotFit,     // Caption stack does not fit the frame.
};

const char* ErrorMessage(Error error);

// One shaped glyph in visual (left-to-right display) order.
struct Glyph {
  uint32_t glyph_id = 0;
  // Half-open UTF-8 byte range of the source cluster. A cluster covers
  // every code point that merged into it (ligatures, combining marks).
  size_t cluster_begin = 0;
  size_t cluster_end = 0;
  // Pen position of this glyph on the line baseline. Origin is the left
  // end of the line baseline; x grows right, y grows up; unit is pixel.
  double x = 0.0;
  double y = 0.0;
  double advance_x = 0.0;
  double advance_y = 0.0;
  double offset_x = 0.0;
  double offset_y = 0.0;
};

struct Layout {
  std::vector<Glyph> glyphs;  // Visual order.
  double width = 0.0;         // Total advance of the line in pixels.
  double ascender = 0.0;
  double descender = 0.0;
  BaseDirection resolved_direction = BaseDirection::kLtr;
  size_t text_size = 0;  // UTF-8 byte length of the source text.
  // Byte offset of every code point start, plus text_size as sentinel.
  std::vector<size_t> codepoint_offsets;
};

// Half-open horizontal pixel interval of a logical selection.
struct SelectionRange {
  double x0 = 0.0;
  double x1 = 0.0;
};

// 8-bit straight (non-premultiplied) RGBA color.
struct Rgba {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

struct RasterStyle {
  Rgba fill = {255, 255, 255, 255};
  Rgba stroke = {0, 0, 0, 255};
  // Outline radius in finite pixels, 0..8. 0 disables the outline.
  double stroke_radius = 0.0;
  // Integer padding added on every side of the ink bounding box,
  // 0..32 pixels.
  int padding = 0;
};

struct RasterImage {
  // Top-to-bottom, left-to-right straight RGBA8. Fully transparent
  // pixels always have r=g=b=0. Empty for a no-ink line.
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  // Original line width returned unchanged from Layout::width.
  double line_width = 0.0;
  // Canvas top-left corner relative to the line baseline origin used
  // by Layout: x grows right, y grows down. Both are <= 0 (the canvas
  // starts at or above/left of the baseline origin).
  int origin_x = 0;
  int origin_y = 0;
};

struct VideoFrame;

class Engine {
 public:
  // Loads the font file. Check GetError() after construction.
  explicit Engine(const std::string& font_path);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  Error GetError() const;

  // Shapes a single line of UTF-8 text. font_size must be a positive
  // finite pixel size. Empty text succeeds with an empty layout.
  Error LayoutLine(const std::string& utf8_text, double font_size,
                   BaseDirection base_direction, Layout* out);

  // Maps a logical selection (half-open UTF-8 byte range over the text
  // passed to LayoutLine) to visual horizontal intervals. Any cluster
  // partially covered is selected as a whole; adjacent intervals are
  // merged. Mixed-direction selections may yield several ranges.
  Error SelectionRanges(const Layout& layout, size_t byte_begin,
                        size_t byte_end,
                        std::vector<SelectionRange>* out) const;

  // Rasterizes an already shaped single line. The glyph ids and
  // positions from LayoutLine are reused; the text is not re-shaped.
  // font_size must equal the size used for shaping (8..128 pixels).
  // On success the image is written through out. A line with no ink
  // (empty/whitespace-only text or zero-alpha colors) returns
  // kNoInk with out reset to a 0x0 image carrying the line width.
  Error RasterizeLine(const Layout& layout, double font_size,
                      const RasterStyle& style, RasterImage* out) const;

  // Writes the pixels of a RasterImage to a 8-bit RGBA PNG file,
  // byte-identical to the in-memory content.
  static Error SavePng(const RasterImage& image, const std::string& path);
  // Same byte-for-byte RGBA output for a composited video frame.
  static Error SaveFramePng(const VideoFrame& frame,
                            const std::string& path);

 private:
  struct Impl;
  Impl* impl_;
};

// One timed, single-line subtitle cue. Times are milliseconds; a cue
// is active while start_ms <= t < end_ms.
struct CaptionCue {
  std::string id;
  std::string text;
  int64_t start_ms = 0;
  int64_t end_ms = 0;
};

// Immutable configuration captured when a track is built.
struct CaptionTrackOptions {
  double font_size = 24.0;
  BaseDirection base_direction = BaseDirection::kAuto;
  RasterStyle style;
  // Non-negative integer pixels left between the frame bottom and the
  // lowest caption, and between stacked caption images.
  int bottom_margin = 0;
  int line_spacing = 0;
};

// Straight (non-premultiplied) RGBA8 frame, rows stored top to bottom
// from the top-left pixel.
struct VideoFrame {
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
};

// Axis-aligned pixel rectangle of a composited caption image.
struct CaptionRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// A caption visible in a sampled frame, listed bottom-to-top in cue
// input order.
struct VisibleCaption {
  std::string id;
  CaptionRect rect;
};

// An immutable, time-indexed collection of rasterized captions.
class CaptionTrack {
 public:
  ~CaptionTrack();

  // Selects every cue active at time_ms (start_ms <= time_ms <
  // end_ms), stacks the inked images bottom-to-top in input order,
  // composites them source-over onto a copy of the input frame, and
  // reports each visible id and pixel rectangle. Seeking forwards or
  // backwards is stateless. With no active caption the frame is copied
  // unchanged and the visible list is empty. Fails entirely (without
  // modifying the input) when the stack is too wide or too tall.
  Error Sample(int64_t time_ms, const VideoFrame& frame,
               VideoFrame* out_frame,
               std::vector<VisibleCaption>* out_visible) const;

 private:
  friend Error BuildCaptionTrack(const std::vector<CaptionCue>&,
                                 const std::string&,
                                 const CaptionTrackOptions&,
                                 std::unique_ptr<CaptionTrack>*);
  CaptionTrack() = default;
  CaptionTrack(const CaptionTrack&) = delete;
  CaptionTrack& operator=(const CaptionTrack&) = delete;

  struct PreparedCue {
    CaptionCue cue;
    RasterImage image;  // 0x0 when the line has no ink.
  };
  std::vector<PreparedCue> cues_;
  CaptionTrackOptions options_;
};

// Validates the cues against the same rules as LayoutLine /
// RasterizeLine, shapes and rasterizes every cue immediately, and
// snapshots the text and style inside the returned track. The track
// is not assigned on any failure.
Error BuildCaptionTrack(const std::vector<CaptionCue>& cues,
                        const std::string& font_path,
                        const CaptionTrackOptions& options,
                        std::unique_ptr<CaptionTrack>* out_track);

}  // namespace caption_track263

#endif  // CAPTION_TRACK263_CAPTION_TRACK263_H
