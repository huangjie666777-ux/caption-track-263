#include "segments.h"

#include <fribidi.h>

#include "caption_track263/caption_track263.h"

namespace caption_track263 {

bool AnalyzeLevels(const DecodedText& text, BaseDirection base,
                   std::vector<uint8_t>* levels, bool* paragraph_ltr) {
  const size_t n = text.codepoints.size();
  levels->assign(n, 0);
  if (n == 0) {
    *paragraph_ltr = base != BaseDirection::kRtl;
    return true;
  }
  std::vector<FriBidiCharType> btypes(n);
  fribidi_get_bidi_types(text.codepoints.data(), n, btypes.data());
  FriBidiParType par = FRIBIDI_PAR_ON;
  if (base == BaseDirection::kLtr) par = FRIBIDI_PAR_LTR;
  if (base == BaseDirection::kRtl) par = FRIBIDI_PAR_RTL;
  std::vector<FriBidiLevel> fri_levels(n);
  FriBidiLevel max_level = fribidi_get_par_embedding_levels(
      btypes.data(), n, &par, fri_levels.data());
  if (max_level == 0) return false;
  for (size_t i = 0; i < n; ++i) (*levels)[i] = fri_levels[i];
  *paragraph_ltr = (par == FRIBIDI_PAR_LTR) ||
                   (par == FRIBIDI_PAR_ON && (max_level & 1) == 0);
  if (par == FRIBIDI_PAR_RTL) *paragraph_ltr = false;
  if (par == FRIBIDI_PAR_LTR) *paragraph_ltr = true;
  if (par == FRIBIDI_PAR_ON) *paragraph_ltr = (fri_levels[0] & 1) == 0;
  return true;
}

namespace {

hb_script_t CharScript(uint32_t cp) {
  return hb_unicode_script(hb_unicode_funcs_get_default(), cp);
}

}  // namespace

std::vector<Segment> BuildSegments(const DecodedText& text,
                                   const std::vector<uint8_t>& levels) {
  std::vector<Segment> segments;
  const size_t n = text.codepoints.size();
  if (n == 0) return segments;

  // Resolve scripts: Common/Inherited characters adopt the script of
  // the preceding strong-script character in the same level run.
  std::vector<hb_script_t> scripts(n, HB_SCRIPT_COMMON);
  hb_script_t carry = HB_SCRIPT_COMMON;
  uint8_t carry_level = levels[0];
  for (size_t i = 0; i < n; ++i) {
    hb_script_t s = CharScript(text.codepoints[i]);
    if (s == HB_SCRIPT_COMMON || s == HB_SCRIPT_INHERITED) {
      if (levels[i] != carry_level) carry = HB_SCRIPT_COMMON;
      scripts[i] = carry;
    } else {
      scripts[i] = s;
      carry = s;
      carry_level = levels[i];
    }
  }
  // Leading common characters adopt the following strong script.
  carry = HB_SCRIPT_COMMON;
  carry_level = levels[0];
  for (size_t i = n; i-- > 0;) {
    if (scripts[i] == HB_SCRIPT_COMMON) {
      if (levels[i] != carry_level) carry = HB_SCRIPT_COMMON;
      scripts[i] = carry;
    } else {
      carry = scripts[i];
      carry_level = levels[i];
    }
  }

  Segment current;
  current.begin = 0;
  current.level = levels[0];
  current.script = scripts[0] == HB_SCRIPT_COMMON ? HB_SCRIPT_LATIN
                                                  : scripts[0];
  for (size_t i = 1; i < n; ++i) {
    hb_script_t s = scripts[i] == HB_SCRIPT_COMMON ? current.script
                                                   : scripts[i];
    if (levels[i] == current.level && s == current.script) continue;
    current.end = i;
    segments.push_back(current);
    current.begin = i;
    current.level = levels[i];
    current.script = s;
  }
  current.end = n;
  segments.push_back(current);

  for (Segment& seg : segments) {
    seg.direction =
        (seg.level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
  }
  return segments;
}

void ReorderSegmentsVisual(std::vector<Segment>* segments) {
  if (segments->size() < 2) return;
  uint8_t max_level = 0;
  uint8_t min_odd = 0xFF;
  for (const Segment& seg : *segments) {
    if (seg.level > max_level) max_level = seg.level;
    if ((seg.level & 1) && seg.level < min_odd) min_odd = seg.level;
  }
  if (min_odd == 0xFF) return;  // No RTL content.
  for (int level = max_level; level >= min_odd; --level) {
    size_t i = 0;
    while (i < segments->size()) {
      if ((*segments)[i].level < level) {
        ++i;
        continue;
      }
      size_t j = i;
      while (j < segments->size() && (*segments)[j].level >= level) ++j;
      // Reverse [i, j).
      size_t lo = i, hi = j;
      while (lo < --hi && lo < hi) {
        Segment tmp = (*segments)[lo];
        (*segments)[lo] = (*segments)[hi];
        (*segments)[hi] = tmp;
        ++lo;
      }
      i = j;
    }
  }
}

}  // namespace caption_track263
