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

#include "usd_reader_light.h"
#include "usd_light_convert.h"

extern "C" {
#include "DNA_light_types.h"
#include "DNA_object_types.h"

#include "BKE_light.h"
#include "BKE_object.h"

#include "WM_api.h"
#include "WM_types.h"
}

#include <pxr/pxr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>

#include <pxr/usd/usdLux/light.h>

#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>

#include <iostream>

namespace usdtokens {
// Attribute names.
static const pxr::TfToken angle("angle", pxr::TfToken::Immortal);
static const pxr::TfToken color("color", pxr::TfToken::Immortal);
static const pxr::TfToken height("height", pxr::TfToken::Immortal);
static const pxr::TfToken intensity("intensity", pxr::TfToken::Immortal);
static const pxr::TfToken radius("radius", pxr::TfToken::Immortal);
static const pxr::TfToken specular("specular", pxr::TfToken::Immortal);
static const pxr::TfToken width("width", pxr::TfToken::Immortal);
}  // namespace usdtokens

namespace {

template<typename T>
bool get_authored_value(const pxr::UsdAttribute attr, const double motionSampleTime, T *r_value)
{
  if (attr && attr.HasAuthoredValue()) {
    return attr.Get<T>(r_value, motionSampleTime);
  }

  return false;
}

}  // End anonymous namespace.

namespace blender::io::usd {

USDLightReader::USDLightReader(const pxr::UsdPrim &prim,
                               const USDImportParams &import_params,
                               const ImportSettings &settings,
                               pxr::UsdGeomXformCache *xf_cache)
    : USDXformReader(prim, import_params, settings), usd_world_scale_(1.0f)
{
  if (xf_cache && import_params.convert_light_from_nits) {
    pxr::GfMatrix4d xf = xf_cache->GetLocalToWorldTransform(prim);
    pxr::GfMatrix4d r;
    pxr::GfVec3d s;
    pxr::GfMatrix4d u;
    pxr::GfVec3d t;
    pxr::GfMatrix4d p;
    xf.Factor(&r, &s, &u, &t, &p);

    usd_world_scale_ = (s[0] + s[1] + s[2]) / 3.0f;
  }
}

void USDLightReader::create_object(Main *bmain, const double /* motionSampleTime */)
{
  Light *blight = static_cast<Light *>(BKE_light_add(bmain, name_.c_str()));

  object_ = BKE_object_add_only_object(bmain, OB_LAMP, name_.c_str());
  object_->data = blight;
}

void USDLightReader::read_object_data(Main *bmain, const double motionSampleTime)
{
  Light *blight = (Light *)object_->data;

  pxr::UsdLuxLight light_prim(prim_);

  if (!light_prim) {
    return;
  }

  pxr::UsdLuxShapingAPI shapingAPI(light_prim);

  // Set light type

  if (prim_.IsA<pxr::UsdLuxDiskLight>()) {
    blight->type = LA_AREA;
    blight->area_shape = LA_AREA_DISK;
    // Ellipse lights are not currently supported
  }
  else if (prim_.IsA<pxr::UsdLuxRectLight>()) {
    blight->type = LA_AREA;
    blight->area_shape = LA_AREA_RECT;
  }
  else if (prim_.IsA<pxr::UsdLuxSphereLight>()) {
    blight->type = LA_LOCAL;

    if (shapingAPI.GetShapingConeAngleAttr().IsAuthored()) {
      blight->type = LA_SPOT;
    }
  }
  else if (prim_.IsA<pxr::UsdLuxDistantLight>()) {
    blight->type = LA_SUN;
  }

  // Set light values

/* In USD 21, light attributes were renamed to have an 'inputs:' prefix
 * (e.g., 'inputs:intensity'). Here and below, for backward compatibility
 * with older USD versions, we also query attributes using the previous
 * naming scheme that omits this prefix. */

  // TODO: Not currently supported
  // pxr::VtValue exposure;
  // light_prim.GetExposureAttr().Get(&exposure, motionSampleTime);

  // TODO: Not currently supported
  // pxr::VtValue diffuse;
  // light_prim.GetDiffuseAttr().Get(&diffuse, motionSampleTime);

  float specular;
  if (get_authored_value(light_prim.GetSpecularAttr(), motionSampleTime, &specular) ||
      prim_.GetAttribute(usdtokens::specular).Get(&specular, motionSampleTime)) {
    blight->spec_fac = specular;
  }

  pxr::GfVec3f color;
  if (get_authored_value(light_prim.GetColorAttr(), motionSampleTime, &color) ||
      prim_.GetAttribute(usdtokens::color).Get(&color, motionSampleTime)) {
    blight->r = color[0];
    blight->g = color[1];
    blight->b = color[2];
  }

  // TODO: Not currently supported
  // pxr::VtValue use_color_temp;
  // light_prim.GetEnableColorTemperatureAttr().Get(&use_color_temp, motionSampleTime);

  // TODO: Not currently supported
  // pxr::VtValue color_temp;
  // light_prim.GetColorTemperatureAttr().Get(&color_temp, motionSampleTime);

  // XXX - apply scene scale to local and spot lights but not area lights (?)
  switch (blight->type) {
    case LA_AREA:
      if (blight->area_shape == LA_AREA_RECT && prim_.IsA<pxr::UsdLuxRectLight>()) {

        pxr::UsdLuxRectLight rect_light(prim_);

        float width;
        if (get_authored_value(rect_light.GetWidthAttr(), motionSampleTime, &width) ||
            prim_.GetAttribute(usdtokens::width).Get(&width, motionSampleTime)) {
          blight->area_size = width;
        }

        float height;
        if (get_authored_value(rect_light.GetHeightAttr(), motionSampleTime, &height) ||
            prim_.GetAttribute(usdtokens::height).Get(&height, motionSampleTime)) {
          blight->area_sizey = height;
        }
      }
      else if (blight->area_shape == LA_AREA_DISK && prim_.IsA<pxr::UsdLuxDiskLight>()) {

        pxr::UsdLuxDiskLight disk_light(prim_);

        float radius;
        if (get_authored_value(disk_light.GetRadiusAttr(), motionSampleTime, &radius) ||
            prim_.GetAttribute(usdtokens::radius).Get(&radius, motionSampleTime)) {
          blight->area_size = radius * 2.0f;
        }
      }
      break;
    case LA_LOCAL:
      if (prim_.IsA<pxr::UsdLuxSphereLight>()) {

        pxr::UsdLuxSphereLight sphere_light(prim_);

        float radius;
        if (get_authored_value(sphere_light.GetRadiusAttr(), motionSampleTime, &radius) ||
            prim_.GetAttribute(usdtokens::radius).Get(&radius, motionSampleTime)) {
          blight->area_size = radius;
        }
      }
      break;
    case LA_SPOT:
      if (prim_.IsA<pxr::UsdLuxSphereLight>()) {

        pxr::UsdLuxSphereLight sphere_light(prim_);

        float radius;
        if (get_authored_value(sphere_light.GetRadiusAttr(), motionSampleTime, &radius) ||
            prim_.GetAttribute(usdtokens::radius).Get(&radius, motionSampleTime)) {
          blight->area_size = radius;
        }

        pxr::VtValue coneAngle;
        shapingAPI.GetShapingConeAngleAttr().Get(&coneAngle, motionSampleTime);
        float spot_size = coneAngle.Get<float>() * ((float)M_PI / 180.0f) * 2.0f;

        if (spot_size <= M_PI) {
          blight->spotsize = spot_size;

          pxr::VtValue spotBlend;
          shapingAPI.GetShapingConeSoftnessAttr().Get(&spotBlend, motionSampleTime);
          blight->spotblend = spotBlend.Get<float>();
        }
        else {
          /* The spot size is greter the 180 degrees, which Blender doesn't support so we
           * make this a sphere light instead. */
          blight->type = LA_LOCAL;
        }
      }
      break;
    case LA_SUN:
      if (prim_.IsA<pxr::UsdLuxDistantLight>()) {
        pxr::UsdLuxDistantLight distant_light(prim_);

        float angle;
        if (get_authored_value(distant_light.GetAngleAttr(), motionSampleTime, &angle) ||
            prim_.GetAttribute(usdtokens::angle).Get(&angle, motionSampleTime)) {
          blight->sun_angle = angle;
        }
      }
      break;
  }


  float intensity;
  if (get_authored_value(light_prim.GetIntensityAttr(), motionSampleTime, &intensity) ||
    prim_.GetAttribute(usdtokens::intensity).Get(&intensity, motionSampleTime)) {

    float intensity_scale = this->import_params_.light_intensity_scale;

    if (import_params_.convert_light_from_nits) {
      /* It's important that we perform the light unit conversion before applying any scaling to the
       * light size, so we can use the USD's meters per unit value. */
      const float meters_per_unit = static_cast<float>(
        pxr::UsdGeomGetStageMetersPerUnit(prim_.GetStage()));
      intensity_scale *= nits_to_energy_scale_factor(blight, meters_per_unit * usd_world_scale_);
    }

    blight->energy = intensity * intensity_scale;
  }

  if ((blight->type == LA_SPOT || blight->type == LA_LOCAL) && import_params_.scale_light_radius) {
    blight->area_size *= settings_->scale;
  }

  USDXformReader::read_object_data(bmain, motionSampleTime);
}

}  // namespace blender::io::usd
