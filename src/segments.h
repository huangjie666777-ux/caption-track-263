#ifndef CAPTION_TRACK263_SRC_SEGMENTS_H
#define CAPTION_TRACK263_SRC_SEGMENTS_H

#include <hb.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "utf8.h"

namespace caption_track263 {

enum class Error;
enum class BaseDirection;

// A run of code points sharing one embedding level and one script,
// shaped together by HarfBuzz. Indices are code point indices.
struct Segment {
  size_t begin = 0;  // Inclusive code point index.
  size_t end = 0;    // Exclusive code point index.
  uint8_t level = 0;
  hb_script_t script = HB_SCRIPT_LATIN;
  hb_direction_t direction = HB_DIRECTION_LTR;
};

// Computes per-code-point embedding levels with FriBidi. Returns false
// on allocation failure. paragraph_ltr reports the resolved base
// direction (kAuto resolves to the first strong character, LTR if none).
bool AnalyzeLevels(const DecodedText& text, BaseDirection base,
                   std::vector<uint8_t>* levels, bool* paragraph_ltr);

// Splits the text into level+script segments in logical order. Common
// and inherited script characters join the surrounding run.
std::vector<Segment> BuildSegments(const DecodedText& text,
                                   const std::vector<uint8_t>& levels);

// Reorders segments (originally logical) into visual order using the
// standard level-reversal algorithm.
void ReorderSegmentsVisual(std::vector<Segment>* segments);

}  // namespace caption_track263

#endif
