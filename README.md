# caption_track263

C++17 library for laying out and rasterizing a single line of mixed
Arabic / Hebrew / Latin text for subtitle rendering, for mapping
logical text selections to visual on-screen intervals, and for burning
timed subtitle tracks into video frames.

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
- Timed caption tracks (`CaptionTrack`):
  - Built from up to 100 captions (unique non-empty id, UTF-8
    single-line text, 64-bit millisecond times with
    `0 <= start < end`); captions may be unordered and
    overlapping. Every caption is validated, laid out, and rasterized
    once at build time with the shared font size, base direction, and
    raster style, so content and style are snapshotted; any failure
    returns no track.
  - `Sample` takes a non-negative millisecond time and a
    top-left, row-major, straight (non-premultiplied) RGBA8 frame
    (positive dimensions, matching byte count, at most 4,000,000
    pixels) and returns a new frame plus the ids and pixel rectangles
    of the visible captions. The input frame is never modified.
  - Captions with `start <= t < end` are stacked in input
    order from the bottom up (first caption lowest), each centered
    horizontally by its image bounding box, above the configured
    non-negative bottom margin and line spacing. Captions with no ink
    occupy no space. Sampling is stateless: arbitrary seeks are
    unaffected by previous calls.
  - With no active caption the frame is returned unchanged. If a
    caption is wider than the frame or the stack exceeds the available
    height, the whole sample fails with `kNoFit`; nothing is
    scaled, cropped, or partially drawn.
  - Compositing is source-over with straight alpha; background
    transparency and color are preserved exactly.

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

// Timed track burned into frames:
std::vector<caption_track263::Caption> captions = {
    {"en", "Hello world", 1000, 3000},  // id, text, start_ms, end_ms
    {"he", "second line", 2000, 4000},  // overlaps and stacks above
};
caption_track263::TrackStyle track_style;
track_style.font_size = 28.0;
track_style.raster = style;        // reuse the RasterStyle above
track_style.bottom_margin = 24;    // px kept clear below the stack
track_style.line_spacing = 8;      // px between stacked captions
caption_track263::CaptionTrack track;
caption_track263::CaptionTrack::Build(engine, captions, track_style,
                                      &track);
caption_track263::SampleResult sample;
track.Sample(2500, frame_rgba.data(), frame_rgba.size(), 640, 360,
             &sample);
// sample.frame: new RGBA8 frame with captions composited
// sample.captions: ids and pixel rects of the visible captions
```

## Source layout

- `src/utf8.cpp` — strict UTF-8 decoding and bidi-control table.
- `src/segments.cpp` — FriBidi level analysis, script-run
  segmentation, visual reordering.
- `src/engine.cpp` — HarfBuzz shaping and glyph positioning.
- `src/selection.cpp` — logical-to-visual selection mapping.
- `src/raster.cpp` — FreeType grayscale raster, rounded outline,
  per-line coverage merge and source-over RGBA compositing.
- `src/blit.cpp` — shared source-over RGBA8 compositing
  (rounding and clamping keep semi-transparent layers from wrapping).
- `src/track.cpp` — timed caption track: build-time validation
  and rasterization, time-based selection, bottom-up placement, and
  frame compositing.
- `src/png_save.cpp` — 8-bit RGBA PNG output via libpng.

## Build, test, demo

```sh
make            # builds build/libcaption_track263.a, bin/demo, bin/selftest
make test       # runs the selftest suite
./bin/demo      # mixed-line layout + selection + transparent PNG example
                # plus timed-track frames under build/frame_*.png
```

Executables use `-Wl,-rpath,'$ORIGIN/../third_party/lib'`, so no
`LD_LIBRARY_PATH` setup is needed.
