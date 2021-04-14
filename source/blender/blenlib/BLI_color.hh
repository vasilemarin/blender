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

/* Spaces are defined as classes to be extended with meta-data in the future.
 * The meta data could contain CIE mapping and whitepoints. */
class Srgb {
};
class LinearRGB {
};

/* Enumeration containing the different alpha modes. */
enum class eAlpha : uint8_t {
  /* Alpha is unassociated (color is straight). */
  Straight,
  /* Alpha is associated (color is premultiplied with alpha). */
  Premultiplied,
};

template<typename Space, eAlpha Alpha> struct Color4f;
template<typename Space, eAlpha Alpha> struct Color4b;

/* Internal roles. To shorten the type names and hide complexity in areas where transformations are
 * unlikely to happen. */
using ColorRender = Color4f<LinearRGB, eAlpha::Premultiplied>;
using ColorReference = Color4f<LinearRGB, eAlpha::Premultiplied>;
using ColorCompositor = Color4f<LinearRGB, eAlpha::Premultiplied>;
using ColorTheme = Color4b<Srgb, eAlpha::Straight>;
using ColorGeometry = Color4f<LinearRGB, eAlpha::Premultiplied>;

MINLINE void associate_alpha(const Color4f<LinearRGB, eAlpha::Straight> &src,
                             Color4f<LinearRGB, eAlpha::Premultiplied> &r_out);
MINLINE void unassociate_alpha(const Color4f<LinearRGB, eAlpha::Premultiplied> &src,
                               Color4f<LinearRGB, eAlpha::Straight> &r_out);
MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4f<Srgb, eAlpha::Straight> &r_out);
MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4f<LinearRGB, eAlpha::Straight> &r_out);
MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4f<LinearRGB, eAlpha::Premultiplied> &r_out);
MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4b<Srgb, eAlpha::Straight> &r_out);
MINLINE void transfer_color(const Color4f<LinearRGB, eAlpha::Premultiplied> &src,
                            Color4b<Srgb, eAlpha::Straight> &r_out);

template<typename Space, eAlpha Alpha> struct Color4f {
  float r, g, b, a;

  Color4f() = default;

  Color4f(const float *rgba) : r(rgba[0]), g(rgba[1]), b(rgba[2]), a(rgba[3])
  {
  }

  Color4f(float r, float g, float b, float a) : r(r), g(g), b(b), a(a)
  {
  }

  template<typename OtherSpace, eAlpha OtherAlpha>
  Color4f(const Color4b<OtherSpace, OtherAlpha> &src)
  {
    transfer_color(src, *this);
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

  template<typename OtherSpace, eAlpha OtherAlpha>
  Color4b(const Color4f<OtherSpace, OtherAlpha> &src)
  {
    transfer_color(src, *this);
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

/* These functions should invoke a BLI_math_color macro or perform a OCIO color transfer. */
MINLINE void associate_alpha(const Color4f<LinearRGB, eAlpha::Straight> &src,
                             Color4f<LinearRGB, eAlpha::Premultiplied> &r_out)
{
  straight_to_premul_v4_v4(r_out, src);
}

MINLINE void unassociate_alpha(const Color4f<LinearRGB, eAlpha::Premultiplied> &src,
                               Color4f<LinearRGB, eAlpha::Straight> &r_out)
{
  premul_to_straight_v4_v4(r_out, src);
}

MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4f<Srgb, eAlpha::Straight> &r_out)
{
  rgba_uchar_to_float(r_out, src);
}

MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4f<LinearRGB, eAlpha::Premultiplied> &r_out)
{
  Color4f<LinearRGB, eAlpha::Straight> intermediate(src);
  associate_alpha(intermediate, r_out);
}

MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4b<Srgb, eAlpha::Straight> &r_out)
{
  r_out.r = src.r;
  r_out.b = src.b;
  r_out.g = src.g;
  r_out.a = src.a;
}

MINLINE void transfer_color(const Color4f<LinearRGB, eAlpha::Premultiplied> &src,
                            Color4f<LinearRGB, eAlpha::Straight> &r_out)
{
  unassociate_alpha(src, r_out);
}

MINLINE void transfer_color(const Color4f<LinearRGB, eAlpha::Premultiplied> &src,
                            Color4b<Srgb, eAlpha::Straight> &r_out)
{
  Color4f<LinearRGB, eAlpha::Straight> intermediate(src);
  transfer_color(intermediate, r_out);
}

MINLINE void transfer_color(const Color4b<Srgb, eAlpha::Straight> &src,
                            Color4f<LinearRGB, eAlpha::Straight> &r_out)
{
  srgb_to_linearrgb_uchar4(r_out, src);
}

}  // namespace blender
