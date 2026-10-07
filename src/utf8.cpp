#include "utf8.h"

namespace caption_track263 {

bool DecodeUtf8(const std::string& text, DecodedText* out) {
  out->codepoints.clear();
  out->offsets.clear();
  const unsigned char* p =
      reinterpret_cast<const unsigned char*>(text.data());
  const size_t n = text.size();
  size_t i = 0;
  while (i < n) {
    uint32_t cp;
    size_t len;
    unsigned char c = p[i];
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if (c >= 0xC2 && c <= 0xDF) {
      cp = c & 0x1F;
      len = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
      cp = c & 0x0F;
      len = 3;
    } else if (c >= 0xF0 && c <= 0xF4) {
      cp = c & 0x07;
      len = 4;
    } else {
      return false;  // 0x80-0xC1 stray/continuation or > 0xF4.
    }
    if (i + len > n) return false;
    for (size_t k = 1; k < len; ++k) {
      unsigned char cc = p[i + k];
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    // Reject overlong encodings, surrogates, and out-of-range values.
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
        (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF)) {
      return false;
    }
    out->offsets.push_back(i);
    out->codepoints.push_back(cp);
    i += len;
  }
  out->offsets.push_back(n);
  return true;
}

bool IsBidiControl(uint32_t cp) {
  if (cp == 0x061C) return true;                    // ARABIC LETTER MARK
  if (cp == 0x200E || cp == 0x200F) return true;    // LRM / RLM
  if (cp >= 0x202A && cp <= 0x202E) return true;    // LRE..PDF
  if (cp >= 0x2066 && cp <= 0x2069) return true;    // LRI..PDI
  return false;
}

}  // namespace caption_track263
