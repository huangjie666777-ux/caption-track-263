#include <hb.h>
#include <hb-ot.h>

#include <cmath>
#include <vector>

#include "caption_track263/caption_track263.h"
#include "engine_internal.h"
#include "segments.h"
#include "utf8.h"

namespace caption_track263 {

constexpr size_t kMaxCodepoints = 4096;

const char* ErrorMessage(Error error) {
  switch (error) {
    case Error::kOk: return "ok";
    case Error::kInvalidUtf8: return "invalid UTF-8";
    case Error::kNewline: return "text contains a line break";
    case Error::kTooLong: return "text exceeds 4096 code points";
    case Error::kMissingGlyph: return "font is missing a required glyph";
    case Error::kBadFont: return "cannot load font";
    case Error::kBadFontSize: return "font size must be positive and finite";
    case Error::kBadSelection: return "invalid selection range";
    case Error::kBadStyle: return "invalid raster style";
    case Error::kImageTooLarge: return "rasterized image exceeds 4,000,000 pixels";
    case Error::kNoInk: return "line has no visible pixels";
    case Error::kFileIo: return "cannot write PNG file";
    case Error::kBadCue: return "invalid caption cue";
    case Error::kTooManyCues: return "more than 100 caption cues";
    case Error::kDuplicateId: return "duplicate caption cue id";
    case Error::kBadMargin: return "invalid margin or line spacing";
    case Error::kBadFrame: return "invalid video frame";
    case Error::kDoesNotFit: return "captions do not fit the frame";
  }
  return "unknown error";
}

Engine::Engine(const std::string& font_path) : impl_(new Impl) {
  impl_->font_path = font_path;
  impl_->blob = hb_blob_create_from_file(font_path.c_str());
  if (!impl_->blob || hb_blob_get_length(impl_->blob) == 0) {
    impl_->error = Error::kBadFont;
    return;
  }
  impl_->face = hb_blob_get_length(impl_->blob)
                    ? hb_face_create(impl_->blob, 0)
                    : nullptr;
  if (!impl_->face || hb_face_get_upem(impl_->face) == 0) {
    impl_->error = Error::kBadFont;
    return;
  }
  impl_->font = hb_font_create(impl_->face);
  hb_ot_font_set_funcs(impl_->font);

  if (FT_Init_FreeType(&impl_->ft_library) == 0) {
    if (FT_New_Memory_Face(impl_->ft_library,
                           reinterpret_cast<const FT_Byte*>
                               (hb_blob_get_data(impl_->blob, nullptr)),
                           static_cast<FT_Long>(hb_blob_get_length(
                               impl_->blob)),
                           0, &impl_->ft_face) != 0) {
      impl_->ft_face = nullptr;
    }
  }
}

Engine::~Engine() {
  if (impl_->ft_face) FT_Done_Face(impl_->ft_face);
  if (impl_->ft_library) FT_Done_FreeType(impl_->ft_library);
  if (impl_->font) hb_font_destroy(impl_->font);
  if (impl_->face) hb_face_destroy(impl_->face);
  if (impl_->blob) hb_blob_destroy(impl_->blob);
  delete impl_;
}

Error Engine::GetError() const { return impl_->error; }

namespace {

hb_language_t LanguageForScript(hb_script_t script) {
  switch (script) {
    case HB_SCRIPT_ARABIC: return hb_language_from_string("ar", -1);
    case HB_SCRIPT_HEBREW: return hb_language_from_string("he", -1);
    default: return hb_language_get_default();
  }
}

}  // namespace

Error Engine::LayoutLine(const std::string& utf8_text, double font_size,
                         BaseDirection base_direction, Layout* out) {
  *out = Layout();
  if (GetError() != Error::kOk) return GetError();
  if (!(font_size > 0.0) || !std::isfinite(font_size)) {
    return Error::kBadFontSize;
  }

  DecodedText decoded;
  if (!DecodeUtf8(utf8_text, &decoded)) return Error::kInvalidUtf8;
  for (uint32_t cp : decoded.codepoints) {
    if (cp == 0x0A || cp == 0x0D || cp == 0x2028 || cp == 0x2029) {
      return Error::kNewline;
    }
  }
  if (decoded.codepoints.size() > kMaxCodepoints) return Error::kTooLong;

  out->text_size = utf8_text.size();
  out->codepoint_offsets = decoded.offsets;

  hb_font_set_scale(impl_->font, static_cast<int>(font_size * 64.0),
                    static_cast<int>(font_size * 64.0));
  hb_font_extents_t extents;
  if (hb_font_get_h_extents(impl_->font, &extents)) {
    out->ascender = extents.ascender / 64.0;
    out->descender = extents.descender / 64.0;
  }

  const size_t n = decoded.codepoints.size();
  if (n == 0) {
    out->resolved_direction =
        base_direction == BaseDirection::kRtl ? BaseDirection::kRtl
                                              : BaseDirection::kLtr;
    return Error::kOk;
  }

  std::vector<uint8_t> levels;
  bool paragraph_ltr = true;
  if (!AnalyzeLevels(decoded, base_direction, &levels, &paragraph_ltr)) {
    return Error::kInvalidUtf8;  // Unreachable in practice.
  }
  out->resolved_direction =
      paragraph_ltr ? BaseDirection::kLtr : BaseDirection::kRtl;

  // Mirroring is left to HarfBuzz: it applies the OpenType 'rtlm'
  // feature to RTL runs, so brackets render mirrored without touching
  // the source code points.
  const std::vector<uint32_t>& shaped_cps = decoded.codepoints;

  std::vector<Segment> segments = BuildSegments(decoded, levels);
  ReorderSegmentsVisual(&segments);

  double pen_x = 0.0;
  for (const Segment& seg : segments) {
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_set_direction(buffer, seg.direction);
    hb_buffer_set_script(buffer, seg.script);
    hb_buffer_set_language(buffer, LanguageForScript(seg.script));
    hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
    hb_buffer_set_flags(buffer, static_cast<hb_buffer_flags_t>(HB_BUFFER_FLAG_BOT | HB_BUFFER_FLAG_EOT));
    for (size_t i = seg.begin; i < seg.end; ++i) {
      hb_buffer_add(buffer, shaped_cps[i],
                    static_cast<uint32_t>(decoded.offsets[i]));
    }
    hb_shape(impl_->font, buffer, nullptr, 0);

    unsigned int glyph_count = 0;
    hb_glyph_info_t* infos =
        hb_buffer_get_glyph_infos(buffer, &glyph_count);
    hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(buffer, &glyph_count);

    // Cluster end = byte offset of the next distinct cluster in this
    // segment, or the byte end of the segment.
    std::vector<size_t> cluster_starts;
    for (unsigned int g = 0; g < glyph_count; ++g) {
      size_t c = infos[g].cluster;
      if (cluster_starts.empty() || cluster_starts.back() != c) {
        cluster_starts.push_back(c);
      }
    }
    const size_t seg_byte_end = decoded.offsets[seg.end];

    for (unsigned int g = 0; g < glyph_count; ++g) {
      size_t cluster_begin = infos[g].cluster;
      size_t cluster_end = seg_byte_end;
      for (size_t c : cluster_starts) {
        if (c > cluster_begin && c < cluster_end) cluster_end = c;
      }
      uint32_t cp = 0;
      for (size_t i = seg.begin; i < seg.end; ++i) {
        if (decoded.offsets[i] == cluster_begin) {
          cp = decoded.codepoints[i];
          break;
        }
      }
      bool control = IsBidiControl(cp);
      if (infos[g].codepoint == 0 && !control) {
        hb_buffer_destroy(buffer);
        return Error::kMissingGlyph;
      }
      Glyph glyph;
      glyph.cluster_begin = cluster_begin;
      glyph.cluster_end = cluster_end;
      if (control) {
        glyph.glyph_id = 0;  // Bidi controls stay invisible.
      } else {
        glyph.glyph_id = infos[g].codepoint;
        glyph.advance_x = positions[g].x_advance / 64.0;
        glyph.advance_y = positions[g].y_advance / 64.0;
        glyph.offset_x = positions[g].x_offset / 64.0;
        glyph.offset_y = positions[g].y_offset / 64.0;
      }
      glyph.x = pen_x + glyph.offset_x;
      glyph.y = glyph.offset_y;
      pen_x += glyph.advance_x;
      out->glyphs.push_back(glyph);
    }
    hb_buffer_destroy(buffer);
  }
  out->width = pen_x;
  return Error::kOk;
}

}  // namespace caption_track263
