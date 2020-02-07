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
 *
 * The Original Code is Copyright (C) 2020 by Blender Foundation.
 */
#include "testing/testing.h"
#include "MEM_guardedalloc.h"

extern "C" {
#include "BKE_fcurve.h"

#include "ED_keyframing.h"

#include "DNA_anim_types.h"
}

TEST(evaluate_fcurve, EmptyFCurve)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));
  EXPECT_EQ(evaluate_fcurve(fcu, 47.0f), 0.0f);
  free_fcurve(fcu);
}

TEST(evaluate_fcurve, OnKeys)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));

  insert_vert_fcurve(fcu, 1.0f, 7.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF);
  insert_vert_fcurve(fcu, 2.0f, 13.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF);
  insert_vert_fcurve(fcu, 3.0f, 19.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF);

  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.0f), 7.0f);   // hits 'on or before first' function
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 2.0f), 13.0f);  // hits 'between' function
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 3.0f), 19.0f);  // hits 'on or after last' function

  /* Also test within an epsilon of the keys, as this was an issue in T39207.
   * This epsilon is just slightly smaller than the epsilon given to binarysearch_bezt_index_ex()
   * in fcurve_eval_between_keyframes(), so it should hit the "exact" code path. */
  float epsilon = 0.00008f;
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 2.0f - epsilon), 13.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 2.0f + epsilon), 13.0f);

  free_fcurve(fcu);
}

TEST(evaluate_fcurve, InterpolationConstant)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));

  EXPECT_EQ(insert_vert_fcurve(fcu, 1.0f, 7.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 0);
  EXPECT_EQ(insert_vert_fcurve(fcu, 2.0f, 13.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 1);

  fcu->bezt[0].ipo = BEZT_IPO_CONST;
  fcu->bezt[1].ipo = BEZT_IPO_CONST;

  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.25f), 7.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.50f), 7.0f);

  free_fcurve(fcu);
}

TEST(evaluate_fcurve, InterpolationLinear)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));

  EXPECT_EQ(insert_vert_fcurve(fcu, 1.0f, 7.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 0);
  EXPECT_EQ(insert_vert_fcurve(fcu, 2.0f, 13.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 1);

  fcu->bezt[0].ipo = BEZT_IPO_LIN;
  fcu->bezt[1].ipo = BEZT_IPO_LIN;

  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.25f), 8.5f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.50f), 10.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.75f), 11.5f);

  free_fcurve(fcu);
}

TEST(evaluate_fcurve, InterpolationBezier)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));

  EXPECT_EQ(insert_vert_fcurve(fcu, 1.0f, 7.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 0);
  EXPECT_EQ(insert_vert_fcurve(fcu, 2.0f, 13.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 1);

  EXPECT_EQ(fcu->bezt[0].ipo, BEZT_IPO_BEZ);
  EXPECT_EQ(fcu->bezt[1].ipo, BEZT_IPO_BEZ);

  // Test with default handles.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.25f), 7.8297067f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.50f), 10.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.75f), 12.170294f);

  // Test with modified handles.
  fcu->bezt[0].vec[0][0] = 0.71855f;  // left handle X
  fcu->bezt[0].vec[0][1] = 6.22482f;  // left handle Y
  fcu->bezt[0].vec[2][0] = 1.35148f;  // right handle X
  fcu->bezt[0].vec[2][1] = 7.96806f;  // right handle Y

  fcu->bezt[1].vec[0][0] = 1.66667f;  // left handle X
  fcu->bezt[1].vec[0][1] = 10.4136f;  // left handle Y
  fcu->bezt[1].vec[2][0] = 2.33333f;  // right handle X
  fcu->bezt[1].vec[2][1] = 15.5864f;  // right handle Y

  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.25f), 7.945497f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.50f), 9.3495407f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.75f), 11.088551f);

  free_fcurve(fcu);
}

TEST(evaluate_fcurve, InterpolationBounce)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));

  EXPECT_EQ(insert_vert_fcurve(fcu, 1.0f, 7.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 0);
  EXPECT_EQ(insert_vert_fcurve(fcu, 2.0f, 13.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 1);

  fcu->bezt[0].ipo = BEZT_IPO_BOUNCE;
  fcu->bezt[1].ipo = BEZT_IPO_BOUNCE;

  fcu->bezt[0].easing = BEZT_IPO_EASE_IN;
  fcu->bezt[1].easing = BEZT_IPO_EASE_AUTO;

  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.4f), 8.3649998f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.5f), 8.4062500f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 1.8f), 11.184999f);

  free_fcurve(fcu);
}

TEST(evaluate_fcurve, ExtrapolationLinearKeys)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));

  EXPECT_EQ(insert_vert_fcurve(fcu, 1.0f, 7.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 0);
  EXPECT_EQ(insert_vert_fcurve(fcu, 2.0f, 13.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 1);
  fcu->bezt[0].ipo = BEZT_IPO_LIN;
  fcu->bezt[1].ipo = BEZT_IPO_LIN;

  fcu->extend = FCURVE_EXTRAPOLATE_LINEAR;
  // Before first keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 0.75f), 5.5f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 0.50f), 4.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, -1.50f), -8.0f);
  // After last keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 2.75f), 17.5f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 3.50f), 22.0f);

  fcu->extend = FCURVE_EXTRAPOLATE_CONSTANT;
  // Before first keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 0.75f), 7.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, -1.50f), 7.0f);
  // After last keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 2.75f), 13.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 3.50f), 13.0f);

  free_fcurve(fcu);
}

TEST(evaluate_fcurve, ExtrapolationBezierKeys)
{
  FCurve *fcu = static_cast<FCurve *>(MEM_callocN(sizeof(FCurve), "FCurve"));

  EXPECT_EQ(insert_vert_fcurve(fcu, 1.0f, 7.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 0);
  EXPECT_EQ(insert_vert_fcurve(fcu, 2.0f, 13.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_NO_USERPREF), 1);

  fcu->bezt[0].vec[0][0] = 0.71855f;  // left handle X
  fcu->bezt[0].vec[0][1] = 6.22482f;  // left handle Y
  fcu->bezt[0].vec[2][0] = 1.35148f;  // right handle X
  fcu->bezt[0].vec[2][1] = 7.96806f;  // right handle Y

  fcu->bezt[1].vec[0][0] = 1.66667f;  // left handle X
  fcu->bezt[1].vec[0][1] = 10.4136f;  // left handle Y
  fcu->bezt[1].vec[2][0] = 2.33333f;  // right handle X
  fcu->bezt[1].vec[2][1] = 15.5864f;  // right handle Y

  fcu->extend = FCURVE_EXTRAPOLATE_LINEAR;
  // Before first keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 0.75f), 6.3114409f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, -0.50f), 2.8686447f);
  // After last keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 2.75f), 18.81946f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 3.50f), 24.63892f);

  fcu->extend = FCURVE_EXTRAPOLATE_CONSTANT;
  // Before first keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 0.75f), 7.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, -1.50f), 7.0f);
  // After last keyframe.
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 2.75f), 13.0f);
  EXPECT_FLOAT_EQ(evaluate_fcurve(fcu, 3.50f), 13.0f);

  free_fcurve(fcu);
}
