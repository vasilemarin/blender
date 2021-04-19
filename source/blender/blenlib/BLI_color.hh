/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#pragma once

#include <iostream>

#include "BLI_math_color.h"

namespace blender {

/**
 * CPP based color structures.
 *
 * Strongly typed color storage structures with space and alpha association.
 * Will increase readability and visibility of typically mistakes when
 * working with colors.
 *
 * The storage structs can hold 4 bytes (Color4b) or 4 floats (Color4f).
 *
 * Usage:
 *
 * Convert an srgb byte color to a linearrgb premultiplied.
 * ```
 * Color4b<Srgb, eAlpha::Straight> srgb_color;
 * Color4f<SceneLinear, eAlpha::Premultiplied> linearrgb_color(srgb_color);
 * ```
 *
 * Common mistakes are:
 * - Storing linear colors in 4 bytes. Reducing the bit depth leads to banding
 *   artifacts.
 * - Missing conversion between Srgb/linearrgb color spaces. Colors are to
 *   bright or dark.
 * - Ignoring premultiplied or straight alpha.
 *
 * Extending this file:
 * - This file can be extended with `ColorHex/Hsl/Hsv` for other
 *   representation of rgb based colors.
 * - Add ColorXyz.
 */

/* Enumeration containing the different alpha modes. */
enum class eAlpha : uint8_t {
  /* Alpha is unassociated (color is straight). */
  Straight,
  /* Alpha is associated (color is premultiplied with alpha). */
  Premultiplied,
};

template<typename Space, eAlpha Alpha> struct Color4f;
template<typename Space, eAlpha Alpha> struct Color4b;

/* Predefinition of Spaces. */
class Srgb;
class SceneLinear;

/* Spaces are defined as classes to be extended with meta-data in the future.
 * The meta data could contain CIE 1931 coordinates of whitepoints and the
 * individual components.
 */

class ByteEncodingNotSupported;

class Srgb {
 public:
  using ByteEncodedSpace = ByteEncodingNotSupported;
};

BLI_INLINE void convert_space(const Color4f<Srgb, eAlpha::Straight> src,
                              Color4f<SceneLinear, eAlpha::Straight> &dst);

class SceneLinearByteEncoded {
};
/* Primary linear colorspace used in Blender.
 * Float precision color corresponding to the scene linear role in the OpenColorIO config.
 */
class SceneLinear {
 public:
  using ByteEncodedSpace = SceneLinearByteEncoded;

  BLI_INLINE void byte_encode(const float *decoded, uint8_t *r_byte_encoded)
  {
    float float_encoded[4];
    linearrgb_to_srgb_v4(float_encoded, decoded);
    rgba_float_to_uchar(r_byte_encoded, float_encoded);
  }

  BLI_INLINE void byte_decode(const uint8_t *byte_encoded, float *r_decoded)
  {
    float float_encoded[4];
    rgba_uchar_to_float(float_encoded, byte_encoded);
    srgb_to_linearrgb_v4(r_decoded, float_encoded);
  }
};
BLI_INLINE void convert_space(const Color4f<SceneLinear, eAlpha::Straight> src,
                              Color4b<Srgb, eAlpha::Straight> &dst);
BLI_INLINE void convert_space(const Color4f<SceneLinear, eAlpha::Straight> src,
                              Color4f<Srgb, eAlpha::Straight> &dst);
BLI_INLINE void convert_space(const Color4f<SceneLinear, eAlpha::Premultiplied> src,
                              Color4f<SceneLinear, eAlpha::Premultiplied> &dst);

template<typename Space, eAlpha Alpha> struct Color4f {
  float r, g, b, a;

  Color4f() = default;

  Color4f(const float *rgba) : r(rgba[0]), g(rgba[1]), b(rgba[2]), a(rgba[3])
  {
  }

  Color4f(float r, float g, float b, float a) : r(r), g(g), b(b), a(a)
  {
  }

  template<typename OtherSpace> explicit Color4f(Color4f<OtherSpace, Alpha> &src)
  {
    convert_space(src, *this);
  }

  /**
   * Convert to another space.
   *
   * Doesn't allow altering of alpha mode. This needs to be done separately by calling
   * premultiply/straight_alpha. Supported space conversions are implementing in the
   *
   * Usage:
   */
  // template<typename OtherSpace> Color4f<OtherSpace, Alpha> convert_space() const
  // {
  //   Color4f<OtherSpace, Alpha> result;
  //   convert_space(*this, result);
  //   return result;
  // }

  // template<typename OtherSpace> Color4b<OtherSpace, Alpha> convert_space() const
  // {
  //   Color4b<OtherSpace, Alpha> result;
  //   convert_space(*this, result);
  //   return result;
  // }

  Color4f<Space, eAlpha::Premultiplied> premultiply_alpha() const
  {
    BLI_assert(Alpha == eAlpha::Straight);
    Color4f<Space, eAlpha::Premultiplied> premultiplied_alpha;
    straight_to_premul_v4_v4(premultiplied_alpha, *this);
    return premultiplied_alpha;
  }

  Color4f<Space, eAlpha::Straight> straight_alpha() const
  {
    BLI_assert(Alpha == eAlpha::Premultiplied);
    Color4f<Space, eAlpha::Straight> straight_alpha;
    premul_to_straight_v4_v4(straight_alpha, *this);
    return straight_alpha;
  }

  /* Encode linear colors into 4 bytes.
   * Only relevant spaces support byte encoding/decoding. */
  Color4b<typename Space::ByteEncodedSpace, Alpha> encode() const
  {
    Color4b<typename Space::ByteEncodedSpace, Alpha> result;
    Space::byte_encode(*this, result);
    return result;
  }

  /* Decode byte encoded spaces.
   * Only relevant spaces support byte encoding/decoding. */
  void decode(const Color4b<typename Space::ByteEncodedSpace, Alpha> &encoded)
  {
    Space::byte_decode(encoded, *this);
  }

  Color4b<Space, Alpha> to_color4b() const
  {
    Color4b<Space, Alpha> result;
    rgba_float_to_uchar(result, *this);
    return result;
  }

  operator float *()
  {
    return &r;
  }

  operator const float *() const
  {
    return &r;
  }

  friend std::ostream &operator<<(std::ostream &stream, Color4f c)
  {
    stream << "(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
    return stream;
  }

  friend bool operator==(const Color4f<Space, Alpha> &a, const Color4f<Space, Alpha> &b)
  {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
  }

  friend bool operator!=(const Color4f<Space, Alpha> &a, const Color4f<Space, Alpha> &b)
  {
    return !(a == b);
  }

  uint64_t hash() const
  {
    uint64_t x1 = *reinterpret_cast<const uint32_t *>(&r);
    uint64_t x2 = *reinterpret_cast<const uint32_t *>(&g);
    uint64_t x3 = *reinterpret_cast<const uint32_t *>(&b);
    uint64_t x4 = *reinterpret_cast<const uint32_t *>(&a);
    return (x1 * 1283591) ^ (x2 * 850177) ^ (x3 * 735391) ^ (x4 * 442319);
  }
};

template<typename Space, eAlpha Alpha> struct Color4b {
  uint8_t r, g, b, a;

  Color4b() = default;

  Color4b(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r), g(g), b(b), a(a)
  {
  }

  template<typename OtherSpace> Color4f<OtherSpace, Alpha> convert_space() const
  {
    Color4f<OtherSpace, Alpha> result;
    convert_space(this, result);
    return result;
  }

  Color4f<Space, Alpha> to_color4f() const
  {
    Color4f<Space, Alpha> result;
    rgba_uchar_to_float(result, *this);
    return result;
  }

  operator uint8_t *()
  {
    return &r;
  }

  operator const uint8_t *() const
  {
    return &r;
  }

  friend std::ostream &operator<<(std::ostream &stream, Color4b c)
  {
    stream << "(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
    return stream;
  }

  friend bool operator==(const Color4b<Space, Alpha> &a, const Color4b<Space, Alpha> &b)
  {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
  }

  friend bool operator!=(const Color4b<Space, Alpha> &a, const Color4b<Space, Alpha> &b)
  {
    return !(a == b);
  }

  uint64_t hash() const
  {
    return static_cast<uint64_t>(r * 1283591) ^ static_cast<uint64_t>(g * 850177) ^
           static_cast<uint64_t>(b * 735391) ^ static_cast<uint64_t>(a * 442319);
  }
};

BLI_INLINE void convert_space(const Color4f<SceneLinear, eAlpha::Straight> src,
                              Color4b<Srgb, eAlpha::Straight> &dst)
{
  linearrgb_to_srgb_uchar4(dst, src);
}

BLI_INLINE void convert_space(const Color4f<SceneLinear, eAlpha::Straight> src,
                              Color4f<Srgb, eAlpha::Straight> &dst)
{
  linearrgb_to_srgb_v4(dst, src);
}
BLI_INLINE void convert_space(const Color4f<SceneLinear, eAlpha::Premultiplied> src,
                              Color4f<SceneLinear, eAlpha::Premultiplied> &dst)
{
  dst.r = src.r;
  dst.g = src.g;
  dst.b = src.b;
  dst.a = src.a;
}

BLI_INLINE void convert_space(const Color4f<Srgb, eAlpha::Straight> src,
                              Color4f<SceneLinear, eAlpha::Straight> &dst)
{
  srgb_to_linearrgb_v4(dst, src);
}

/* Internal roles. For convenience to shorten the type names and hide complexity
 * in areas where transformations are unlikely to happen. */
using ColorSceneReference4f = Color4f<SceneLinear, eAlpha::Premultiplied>;
using ColorSceneReference4b =
    Color4b<typename SceneLinear::ByteEncodedSpace, eAlpha::Premultiplied>;
using ColorTheme4b = Color4b<Srgb, eAlpha::Straight>;
using ColorGeometry4f = ColorSceneReference4f;
using ColorGeometry4b = ColorSceneReference4b;

}  // namespace blender
