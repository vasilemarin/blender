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

  // TODO: Not currently supported
  // pxr::VtValue exposure;
  // light_prim.GetExposureAttr().Get(&exposure, motionSampleTime);

  // TODO: Not currently supported
  // pxr::VtValue diffuse;
  // light_prim.GetDiffuseAttr().Get(&diffuse, motionSampleTime);

  pxr::VtValue specular;
  light_prim.GetSpecularAttr().Get(&specular, motionSampleTime);
  blight->spec_fac = specular.Get<float>();

  pxr::VtValue color;
  light_prim.GetColorAttr().Get(&color, motionSampleTime);
  // Calling UncheckedGet() to silence compiler warning.
  pxr::GfVec3f color_vec = color.UncheckedGet<pxr::GfVec3f>();
  blight->r = color_vec[0];
  blight->g = color_vec[1];
  blight->b = color_vec[2];

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

        pxr::VtValue width;
        rect_light.GetWidthAttr().Get(&width, motionSampleTime);

        pxr::VtValue height;
        rect_light.GetHeightAttr().Get(&height, motionSampleTime);

        blight->area_size = width.Get<float>();
        blight->area_sizey = height.Get<float>();
      }
      else if (blight->area_shape == LA_AREA_DISK && prim_.IsA<pxr::UsdLuxDiskLight>()) {

        pxr::UsdLuxDiskLight disk_light(prim_);

        pxr::VtValue radius;
        disk_light.GetRadiusAttr().Get(&radius, motionSampleTime);

        blight->area_size = radius.Get<float>() * 2.0f;
      }
      break;
    case LA_LOCAL:
      if (prim_.IsA<pxr::UsdLuxSphereLight>()) {

        pxr::UsdLuxSphereLight sphere_light(prim_);

        pxr::VtValue radius;
        sphere_light.GetRadiusAttr().Get(&radius, motionSampleTime);

        blight->area_size = radius.Get<float>();
      }
      break;
    case LA_SPOT:
      if (prim_.IsA<pxr::UsdLuxSphereLight>()) {

        pxr::UsdLuxSphereLight sphere_light(prim_);

        pxr::VtValue radius;
        sphere_light.GetRadiusAttr().Get(&radius, motionSampleTime);

        blight->area_size = radius.Get<float>();

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

        pxr::VtValue angle;
        distant_light.GetAngleAttr().Get(&angle, motionSampleTime);
        blight->sun_angle = angle.Get<float>();
      }
      break;
  }

  pxr::VtValue intensity;
  light_prim.GetIntensityAttr().Get(&intensity, motionSampleTime);

  float intensity_scale = import_params_.light_intensity_scale;

  if (import_params_.convert_light_from_nits) {
    /* It's important that we perform the light unit conversion before applying any scaling to the
     * light size, so we can use the USD's meters per unit value. */
    const float meters_per_unit = static_cast<float>(
        pxr::UsdGeomGetStageMetersPerUnit(prim_.GetStage()));
    intensity_scale *= nits_to_energy_scale_factor(blight, meters_per_unit * usd_world_scale_);
  }

  blight->energy = intensity.Get<float>() * intensity_scale;

  if ((blight->type == LA_SPOT || blight->type == LA_LOCAL) && import_params_.scale_light_radius) {
    blight->area_size *= settings_->scale;
  }

  USDXformReader::read_object_data(bmain, motionSampleTime);
}

}  // namespace blender::io::usd
