/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include "BLI_color.hh"

namespace blender::tests {

TEST(color, SrgbStraightByteToFloat)
{
  Color4b<Srgb, eAlpha::Straight> srgb(192, 128, 64, 128);
  Color4f<Srgb, eAlpha::Straight> linear(srgb);
  EXPECT_NEAR(0.75f, linear.r, 0.01f);
  EXPECT_NEAR(0.5f, linear.g, 0.01f);
  EXPECT_NEAR(0.25f, linear.b, 0.01f);
  EXPECT_NEAR(0.5f, linear.a, 0.01f);
}

TEST(color, SrgbStraightToLinearStraight)
{
  BLI_init_srgb_conversion();
  Color4b<Srgb, eAlpha::Straight> srgb(192, 128, 64, 128);
  Color4f<LinearRGB, eAlpha::Straight> linear(srgb);
  EXPECT_NEAR(0.52f, linear.r, 0.01f);
  EXPECT_NEAR(0.21f, linear.g, 0.01f);
  EXPECT_NEAR(0.05f, linear.b, 0.01f);
  EXPECT_NEAR(0.5f, linear.a, 0.01f);
}

TEST(color, SrgbStraightToLinearPremultiplied)
{
  BLI_init_srgb_conversion();
  Color4b<Srgb, eAlpha::Straight> srgb(192, 128, 64, 128);
  Color4f<LinearRGB, eAlpha::Premultiplied> linear(srgb);
  EXPECT_NEAR(0.26f, linear.r, 0.01f);
  EXPECT_NEAR(0.11f, linear.g, 0.01f);
  EXPECT_NEAR(0.02f, linear.b, 0.01f);
  EXPECT_NEAR(0.5f, linear.a, 0.01f);
}

TEST(color, LinearStraightToPremultiplied)
{
  BLI_init_srgb_conversion();
  Color4f<LinearRGB, eAlpha::Straight> unassociated(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<LinearRGB, eAlpha::Premultiplied> associated(unassociated);
  EXPECT_NEAR(0.37f, associated.r, 0.01f);
  EXPECT_NEAR(0.25f, associated.g, 0.01f);
  EXPECT_NEAR(0.12f, associated.b, 0.01f);
  EXPECT_NEAR(0.5f, associated.a, 0.01f);
}

TEST(color, LinearPremultipliedToStraight)
{
  BLI_init_srgb_conversion();
  Color4f<LinearRGB, eAlpha::Premultiplied> associated(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<LinearRGB, eAlpha::Straight> unassociated(associated);
  EXPECT_NEAR(1.5f, associated.r, 0.01f);
  EXPECT_NEAR(1.0f, associated.g, 0.01f);
  EXPECT_NEAR(0.5f, associated.b, 0.01f);
  EXPECT_NEAR(0.5f, associated.a, 0.01f);
}

TEST(color, LinearStraightSrgbFloat)
{
  BLI_init_srgb_conversion();
  Color4f<LinearRGB, eAlpha::Straight> linear(0.75f, 0.5f, 0.25f, 0.5f);
  /* Error: this uses the float constructor and does not do any conversions. */
  Color4f<Srgb, eAlpha::Straight> srgb(linear);
  EXPECT_NEAR(0, srgb.r, 0.01);
  EXPECT_NEAR(0, srgb.g, 0.01);
  EXPECT_NEAR(0, srgb.b, 0.01);
  EXPECT_NEAR(0, srgb.a, 0.01);
}

TEST(color, LinearPremultipliedToSrgbFloat)
{
  BLI_init_srgb_conversion();
  Color4f<LinearRGB, eAlpha::Premultiplied> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4f<Srgb, eAlpha::Straight> srgb(linear);
  EXPECT_NEAR(0, srgb.r, 0.01);
  EXPECT_NEAR(0, srgb.g, 0.01);
  EXPECT_NEAR(0, srgb.b, 0.01);
  EXPECT_NEAR(0, srgb.a, 0.01);
}

TEST(color, LinearStraightSrgbByte)
{
  BLI_init_srgb_conversion();
  Color4f<LinearRGB, eAlpha::Straight> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4b<Srgb, eAlpha::Straight> srgb(linear);
  EXPECT_EQ(0, srgb.r);
  EXPECT_EQ(0, srgb.g);
  EXPECT_EQ(0, srgb.b);
  EXPECT_EQ(0, srgb.a);
}

TEST(color, LinearPremultipliedToSrgbByte)
{
  BLI_init_srgb_conversion();
  Color4f<LinearRGB, eAlpha::Premultiplied> linear(0.75f, 0.5f, 0.25f, 0.5f);
  Color4b<Srgb, eAlpha::Straight> srgb(linear);
  EXPECT_EQ(0, srgb.r);
  EXPECT_EQ(0, srgb.g);
  EXPECT_EQ(0, srgb.b);
  EXPECT_EQ(0, srgb.a);
}

}  // namespace blender::tests