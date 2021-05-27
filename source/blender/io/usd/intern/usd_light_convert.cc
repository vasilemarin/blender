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
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */

#include "usd_light_convert.h"

#include "BKE_light.h"
#include "BlI_math.h"
#include "DNA_light_types.h"

namespace blender::io::usd {

/* Return the scale factor to convert nits to light energy
 * (Watts or Watts per meter squared) for the given light. */
float nits_to_energy_scale_factor(const Light *light,
                                  const float meters_per_unit,
                                  const float radius_scale)
{
  if (!light) {
    return 1.0f;
  }

  const float nits_to_watts_per_meter_sq = 0.0014641f;

  /* Compute meters per unit squared. */
  const float mpu_sq = meters_per_unit * meters_per_unit;

  float scale = nits_to_watts_per_meter_sq;

  /* Scale by the light surface area, for lights other than sun. */
  switch (light->type) {
    case LA_AREA:
      switch (light->area_shape) {
        case LA_AREA_DISK:
        case LA_AREA_ELLIPSE: { /* An ellipse light will deteriorate into a disk light. */
          float r = light->area_size / 2.0f;
          scale *= 2.0f * M_PI * (r * r) * mpu_sq;
          break;
        }
        case LA_AREA_RECT: {
          scale *= light->area_size * light->area_sizey * mpu_sq;
          break;
        }
        case LA_AREA_SQUARE: {
          scale *= light->area_size * light->area_size * mpu_sq;
          break;
        }
      }
      break;
    case LA_LOCAL: {
      float r = light->area_size * radius_scale;
      scale *= 4.0f * M_PI * (r * r) * mpu_sq;
      break;
    }
    case LA_SPOT: {
      float r = light->area_size * radius_scale;
      float angle = light->spotsize / 2.0f;
      scale *= 2.0f * M_PI * (r * r) * (1.0f - cosf(angle)) * mpu_sq;
      break;
    }
    case LA_SUN: {
      /* Sun energy is Watts per square meter so we don't scale by area. */
      break;
    }
    default:
      break;
  }

  return scale;
}

}  // namespace blender::io::usd
