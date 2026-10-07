# caption_track263

C++17 library for laying out and rasterizing a single line of mixed
Arabic / Hebrew / Latin text for subtitle rendering, and for mapping
logical text selections to visual on-screen intervals.

Built on FriBidi 1.0.8 (bidi analysis), HarfBuzz 2.7.4 (shaping),
FreeType 2.11.1 (grayscale glyph rasterization and rounded outlines),
and libpng 1.6.37 (PNG output), all vendored under `third_party/`.
The font is `fonts/DejaVuSans.ttf`.

## Features

- UTF-8 single-line input; base direction `kAuto` (first strong
  character, LTR when none), `kLtr`, or `kRtl`.
- Digits, punctuation, combining marks, and bidi isolates
  (LRI/RLI/FSI/PDI and friends) are supported; directional controls
  produce no visible glyphs.
- FriBidi computes embedding levels; the text is split into
  level + script segments; HarfBuzz shapes each segment in its own
  direction; segments are then reordered visually. Strings are never
  reversed before shaping, so joining, ligatures, marks, and bracket
  mirroring (via the OpenType `rtlm` feature) come out right.
- Input validation: empty text succeeds; invalid UTF-8, line breaks,
  missing glyphs, and inputs over 4096 code points are rejected.
- Output per glyph, in visual order: glyph id, half-open UTF-8 byte
  cluster range (clusters cover merged combining marks), baseline
  position, advance, and offset, plus total line width. Origin is the
  left end of the line baseline, x grows right, y grows up, unit is
  pixel. Spaces keep their real advance.
- Logical selections (half-open byte ranges on code point boundaries)
  map to visual horizontal intervals: partially covered clusters are
  selected whole, intervals are positioned by cluster advance spans,
  adjacent intervals merge, and mixed-direction selections can yield
  several disjoint ranges. Out-of-range, inverted, or
  non-code-point-boundary selections are rejected; empty selections
  return empty.
- Single-line rasterization to a transparent overlay image:
  - Reuses the glyph ids and positions produced by `LayoutLine`; text
    is never shaped or positioned again, so joins, ligatures, and
    combining-mark placement are identical to the layout.
  - FreeType grayscale antialiasing honors negative bearings, subpixel
    pen positions, and the y-up/y-down flip. Rounded outer outlines use
    the FreeType stroker (radius 0..8 finite pixels).
  - The fill and the outline are each merged across the whole line with
    maximum coverage first, then composited source-over outline-first,
    fill-second, so overlapping glyphs never double-darken and an
    outline never covers an adjacent glyph's fill.
  - Output is top-to-bottom straight (non-premultiplied) RGBA8; fully
    transparent pixels have RGB 0. The canvas is the tight bitmap
    bounding box (marks and outlines included) plus integer padding
    (0..32). Up to 4,000,000 pixels; larger canvases are rejected with
    `kImageTooLarge`. No-ink lines (empty/whitespace text or zero-alpha
    colors) return a 0x0 image carrying the original line width and no
    file is written.
  - Font sizes are 8..128 pixels. Fill and outline take 8-bit RGBA
    colors.
- PNG saving writes the in-memory pixels byte-for-byte as 8-bit RGBA.
- Timed caption tracks (`CaptionTrack` / `BuildCaptionTrack`) burn
  pre-rendered captions into video frames at millisecond timestamps:
  - Up to 100 cues with unique non-empty ids, single-line UTF-8 text,
    and half-open 64-bit millisecond ranges (`start <= t < end`).
    Cues may arrive out of order and may overlap. Building reuses the
    original `LayoutLine`/`RasterizeLine` validation, rasterizes every
    cue once, and snapshots text and style; any failure returns no
    track. No-ink cues (empty/whitespace text or zero-alpha colors)
    are retained but never occupy stack space.
  - `Sample` takes any non-negative time and a top-to-bottom straight
    RGBA8 frame (positive size, exact byte count, at most 4,000,000
    pixels). Selection and placement are stateless, so seeking forward
    or backward always gives the same result, independent of previous
    samples and of caller mutations after the track was built.
  - Active captions stack bottom-to-top in cue input order (the first
    active cue is lowest), each image centered horizontally by its ink
    bounding box, separated by an integer line spacing and lifted off
    the bottom by an integer margin. If any image is wider than the
    frame or the whole stack is too tall, the call fails without
    scaling, cropping, or returning a partially drawn frame. With no
    active caption the frame is returned as an exact copy.
  - Images composite source-over onto a copy of the input frame,
    preserving background straight-alpha color and transparency, and
    the returned list reports each visible caption id with its pixel
    rectangle.

## Public API

See `include/caption_track263/caption_track263.h`.

```cpp
caption_track263::Engine engine("fonts/DejaVuSans.ttf");
caption_track263::Layout layout;
engine.LayoutLine("mixed text", 24.0,
                  caption_track263::BaseDirection::kAuto, &layout);
std::vector<caption_track263::SelectionRange> ranges;
engine.SelectionRanges(layout, 4, 12, &ranges);

caption_track263::RasterStyle style;
style.fill   = {255, 255, 255, 255};  // white fill
style.stroke = {0, 0, 0, 255};        // black outline
style.stroke_radius = 2.0;            // px, 0 disables the outline
style.padding = 8;                    // px on every side
caption_track263::RasterImage image;
engine.RasterizeLine(layout, 24.0, style, &image);
// image.pixels: straight RGBA8, rows top-to-bottom
// image.width / image.height: canvas size in pixels
// image.line_width: original layout width
// image.origin_x / origin_y: canvas top-left relative to the baseline
//   origin (x right, y down)
caption_track263::Engine::SavePng(image, "subtitle.png");

// Timed track: build once, sample at arbitrary millisecond times.
std::vector<caption_track263::CaptionCue> cues = {
    {"line1", "Hello", 0, 2000},
    {"line2", "\u0645\u0631\u062D\u0628\u0627", 1000, 3000},
};
caption_track263::CaptionTrackOptions options;
options.font_size = 24.0;
options.style = style;
options.bottom_margin = 8;
options.line_spacing = 4;
std::unique_ptr<caption_track263::CaptionTrack> track;
caption_track263::Error track_err =
    caption_track263::BuildCaptionTrack(
        cues, "fonts/DejaVuSans.ttf", options, &track);

caption_track263::VideoFrame frame;  // straight RGBA8, top to bottom
caption_track263::VideoFrame rendered;
std::vector<caption_track263::VisibleCaption> visible;
track->Sample(1500, frame, &rendered, &visible);  // both cues overlap
caption_track263::Engine::SaveFramePng(rendered, "frame_1500.png");
```

## Source layout

- `src/utf8.cpp` — strict UTF-8 decoding and bidi-control table.
- `src/segments.cpp` — FriBidi level analysis, script-run
  segmentation, visual reordering.
- `src/engine.cpp` — HarfBuzz shaping and glyph positioning.
- `src/selection.cpp` — logical-to-visual selection mapping.
- `src/raster.cpp` — FreeType grayscale raster, rounded outline,
  per-line coverage merge and source-over RGBA compositing.
- `src/png_save.cpp` — 8-bit RGBA PNG output via libpng.
- `src/track.cpp` — cue validation/snapshot, time selection, bottom-up
  placement, and source-over frame compositing for `CaptionTrack`.
- `src/composite.h` — shared straight-alpha source-over blending.

## Build, test, demo

```sh
make            # builds build/libcaption_track263.a, bin/demo, bin/selftest
make test       # runs the selftest suite
./bin/demo      # mixed-line layout + selection, plus timed-track PNG
                # frames showing overlapping cues and arbitrary seeks
```

Executables use `-Wl,-rpath,'$ORIGIN/../third_party/lib'`, so no
`LD_LIBRARY_PATH` setup is needed.

The timed-track demo writes `build/track_0500ms.png`,
`build/track_1500ms.png` (two overlapping captions stacked above each
other), and `build/track_5500ms.png` (a forward seek to a later cue).
