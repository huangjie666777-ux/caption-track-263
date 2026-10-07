#include <algorithm>

#include "caption_track263/caption_track263.h"

namespace caption_track263 {

Error Engine::SelectionRanges(const Layout& layout, size_t byte_begin,
                              size_t byte_end,
                              std::vector<SelectionRange>* out) const {
  out->clear();
  if (byte_begin > byte_end || byte_end > layout.text_size) {
    return Error::kBadSelection;
  }
  const std::vector<size_t>& offsets = layout.codepoint_offsets;
  auto is_boundary = [&offsets](size_t pos) {
    return std::binary_search(offsets.begin(), offsets.end(), pos);
  };
  if (!is_boundary(byte_begin) || !is_boundary(byte_end)) {
    return Error::kBadSelection;
  }
  if (byte_begin == byte_end) return Error::kOk;  // Empty selection.

  // Collect the advance span of every cluster touched by the selection.
  std::vector<SelectionRange> ranges;
  size_t i = 0;
  while (i < layout.glyphs.size()) {
    size_t j = i + 1;
    while (j < layout.glyphs.size() &&
           layout.glyphs[j].cluster_begin ==
               layout.glyphs[i].cluster_begin) {
      ++j;
    }
    size_t cb = layout.glyphs[i].cluster_begin;
    size_t ce = layout.glyphs[i].cluster_end;
    if (cb < byte_end && byte_begin < ce) {
      double x0 = layout.glyphs[i].x;
      double x1 = layout.glyphs[i].x;
      for (size_t g = i; g < j; ++g) {
        double left = layout.glyphs[g].x;
        double right = layout.glyphs[g].x + layout.glyphs[g].advance_x;
        if (g == i) {
          x0 = left;
          x1 = right;
        } else {
          if (left < x0) x0 = left;
          if (right > x1) x1 = right;
        }
      }
      ranges.push_back({x0, x1});
    }
    i = j;
  }

  std::sort(ranges.begin(), ranges.end(),
            [](const SelectionRange& a, const SelectionRange& b) {
              return a.x0 < b.x0;
            });
  for (const SelectionRange& r : ranges) {
    if (!out->empty() && r.x0 <= out->back().x1 + 1e-9) {
      if (r.x1 > out->back().x1) out->back().x1 = r.x1;
    } else {
      out->push_back(r);
    }
  }
  return Error::kOk;
}

}  // namespace caption_track263
