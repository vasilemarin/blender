/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include "BLI_color.hh"

namespace blender::tests {

/**
 * \name Conversions
 * \{ */

TEST(color, SrgbStraightByteToFloat)
{
  Color4b<Srgb, eAlpha::Straight> srgb_byte(192, 128, 64, 128);
  Color4f<Srgb, eAlpha::Straight> srgb_float = srgb_byte.to_color4f();
  EXPECT_NEAR(0.75f, srgb_float.r, 0.01f);
  EXPECT_NEAR(0.5f, srgb_float.g, 0.01f);
  EXPECT_NEAR(0.25f, srgb_float.b, 0.01f);
  EXPECT_NEAR(0.5f, srgb_float.a, 0.01f);
}

TEST(color, SrgbStraightFloatToByte)
{
  Color4f<Srgb, eAlpha::Straight> srgb_float(0.75f, 0.5f, 0.25f, 0.5f);
  Color4b<Srgb, eAlpha::Straight> srgb_byte = srgb_float.to_color4b();
  EXPECT_EQ(191, srgb_byte.r);
  EXPECT_EQ(128, srgb_byte.g);
  EXPECT_EQ(64, srgb_byte.b);
  EXPECT_EQ(128, srgb_byte.a);
}

TEST(color, SrgbStraightToSceneLinearPremultiplied)
{
  BLI_init_srgb_conversion();
  Color4b<Srgb, eAlpha::Straight> srgb(192, 128, 64, 128);
  Color4f<Srgb, eAlpha::Straight> srgb_4f = srgb.to_color4f();
  Color4f<SceneLinear, eAlpha::Straight> linear_straight(srgb_4f);
  Color4f<SceneLinear, eAlpha::Premultiplied> linear = linear_straight.premultiply_alpha();
  EXPECT_NEAR(0.26f, linear.r, 0.01f);
  EXPECT_NEAR(0.11f, linear.g, 0.01f);
  EXPECT_NEAR(0.02f, linear.b, 0.01f);
  EXPECT_NEAR(0.5f, linear.a, 0.01f);
}

TEST(color, SceneLinearStraightToPremultiplied)
{
  Color4f<SceneLinear, eAlpha::Straight> straight(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<SceneLinear, eAlpha::Premultiplied> premultiplied = straight.premultiply_alpha();
  EXPECT_NEAR(0.37f, premultiplied.r, 0.01f);
  EXPECT_NEAR(0.25f, premultiplied.g, 0.01f);
  EXPECT_NEAR(0.12f, premultiplied.b, 0.01f);
  EXPECT_NEAR(0.5f, premultiplied.a, 0.01f);
}

TEST(color, SceneLinearPremultipliedToStraight)
{
  Color4f<SceneLinear, eAlpha::Premultiplied> premultiplied(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<SceneLinear, eAlpha::Straight> straight = premultiplied.straight_alpha();
  EXPECT_NEAR(1.5f, straight.r, 0.01f);
  EXPECT_NEAR(1.0f, straight.g, 0.01f);
  EXPECT_NEAR(0.5f, straight.b, 0.01f);
  EXPECT_NEAR(0.5f, straight.a, 0.01f);
}

TEST(color, SceneLinearStraightSrgbFloat)
{
  BLI_init_srgb_conversion();
  Color4f<SceneLinear, eAlpha::Straight> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<Srgb, eAlpha::Straight> srgb(linear);
  EXPECT_NEAR(0.88f, srgb.r, 0.01);
  EXPECT_NEAR(0.73f, srgb.g, 0.01);
  EXPECT_NEAR(0.53f, srgb.b, 0.01);
  EXPECT_NEAR(0.5f, srgb.a, 0.01);
}

TEST(color, SceneLinearPremultipliedToSrgbFloat)
{
  BLI_init_srgb_conversion();
  Color4f<SceneLinear, eAlpha::Premultiplied> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<SceneLinear, eAlpha::Straight> linear_straight = linear.straight_alpha();
  Color4f<Srgb, eAlpha::Straight> srgb(linear_straight);
  EXPECT_NEAR(1.19f, srgb.r, 0.01);
  EXPECT_NEAR(1.0f, srgb.g, 0.01);
  EXPECT_NEAR(0.74f, srgb.b, 0.01);
  EXPECT_NEAR(0.5f, srgb.a, 0.01);
}

TEST(color, SceneLinearStraightSrgbByte)
{
  BLI_init_srgb_conversion();
  Color4f<SceneLinear, eAlpha::Straight> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<Srgb, eAlpha::Straight> srgb_4f(linear);
  Color4b<Srgb, eAlpha::Straight> srgb = srgb_4f.to_color4b();
  EXPECT_EQ(225, srgb.r);
  EXPECT_EQ(188, srgb.g);
  EXPECT_EQ(137, srgb.b);
  EXPECT_EQ(128, srgb.a);
}

TEST(color, SceneLinearPremultipliedToSrgbByte)
{
  BLI_init_srgb_conversion();
  Color4f<SceneLinear, eAlpha::Premultiplied> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<SceneLinear, eAlpha::Straight> linear_straight = linear.straight_alpha();
  Color4f<Srgb, eAlpha::Straight> srgb_4f(linear_straight);
  Color4b<Srgb, eAlpha::Straight> srgb = srgb_4f.to_color4b();
  EXPECT_EQ(255, srgb.r);
  EXPECT_EQ(255, srgb.g);
  EXPECT_EQ(188, srgb.b);
  EXPECT_EQ(128, srgb.a);
}

TEST(color, SceneLinearByteEncoding)
{
  Color4f<SceneLinear, eAlpha::Premultiplied> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4b<SceneLinearByteEncoded, eAlpha::Premultiplied> encoded = linear.encode();
  EXPECT_EQ(225, encoded.r);
  EXPECT_EQ(188, encoded.g);
  EXPECT_EQ(137, encoded.b);
  EXPECT_EQ(128, encoded.a);
}

TEST(color, SceneLinearByteDecoding)
{
  Color4b<SceneLinearByteEncoded, eAlpha::Premultiplied> encoded(225, 188, 137, 128);
  Color4f<SceneLinear, eAlpha::Premultiplied> decoded;
  decoded.decode(encoded);
  EXPECT_NEAR(0.75f, decoded.r, 0.01f);
  EXPECT_NEAR(0.5f, decoded.g, 0.01f);
  EXPECT_NEAR(0.25f, decoded.b, 0.01f);
  EXPECT_NEAR(0.5f, decoded.a, 0.01f);
}

/* \} */

}  // namespace blender::tests
