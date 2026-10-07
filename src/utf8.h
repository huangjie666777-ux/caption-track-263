#ifndef CAPTION_TRACK263_SRC_UTF8_H
#define CAPTION_TRACK263_SRC_UTF8_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace caption_track263 {

struct DecodedText {
  std::vector<uint32_t> codepoints;
  // Byte offset of each code point, plus text.size() as final sentinel.
  std::vector<size_t> offsets;
};

// Strictly decodes UTF-8: rejects overlong forms, surrogates, values
// above U+10FFFF, and stray continuation bytes. Returns false on error.
bool DecodeUtf8(const std::string& text, DecodedText* out);

// True for characters that must not produce a visible glyph: bidi
// controls and default-ignorable directional marks.
bool IsBidiControl(uint32_t cp);

}  // namespace caption_track263

#endif
