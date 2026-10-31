#pragma once

#include <cstddef>

namespace onion::overlay {

inline constexpr float kRefFontSize = 18.0f;
inline constexpr float kMinTextWidth = 32.0f;

/* Approximate 18pt bold advances; padded per-core spaces are much narrower. */
inline constexpr float glyph_advance(unsigned char ch) {
  if (ch == ' ')
    return 5.0f;
  if (ch >= '0' && ch <= '9')
    return 11.0f;
  switch (ch) {
  case '%':
    return 15.0f;
  case '.':
  case '|':
    return 5.0f;
  case '-':
    return 8.0f;
  default:
    return 12.0f;
  }
}

/** UTF-8 lead byte of the degree sign (U+00B0 = C2 B0). */
inline constexpr unsigned char kUtf8DegreeLead = 0xC2;
inline constexpr unsigned char kUtf8DegreeTail = 0xB0;
inline constexpr float kDegreeAdvance = 7.0f;

/** Sum of glyph advances only: no padding and no minimum. One advance per
 *  UTF-8 code point, so "°" counts once instead of once per byte. */
inline float text_advance(const char *text, float font_size = kRefFontSize) {
  if (!text)
    return 0.0f;
  const float scale =
      (font_size > 0.0f ? font_size : kRefFontSize) / kRefFontSize;
  float width = 0.0f;
  for (std::size_t i = 0; text[i]; ++i) {
    const auto ch = static_cast<unsigned char>(text[i]);
    if ((ch & 0xC0) == 0x80)
      continue; /* continuation byte */
    if (ch == kUtf8DegreeLead &&
        static_cast<unsigned char>(text[i + 1]) == kUtf8DegreeTail)
      width += kDegreeAdvance * scale;
    else
      width += glyph_advance(ch) * scale;
  }
  return width;
}

inline float estimate_text_width(const char *text,
                                 float font_size = kRefFontSize) {
  if (!text || !text[0])
    return 0.0f;

  const float scale =
      (font_size > 0.0f ? font_size : kRefFontSize) / kRefFontSize;
  const float width = 6.0f * scale + text_advance(text, font_size);
  const float min_width = kMinTextWidth * scale;
  return width < min_width ? min_width : width;
}

} // namespace onion::overlay
