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
#include "usd_writer_material.h"

#include "usd.h"

#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

#include "BKE_light.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_scene.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BlI_math.h"

#include "DNA_light_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include <string>

namespace usdtokens {
// Attribute names.
static const pxr::TfToken color("color", pxr::TfToken::Immortal);
static const pxr::TfToken intensity("intensity", pxr::TfToken::Immortal);
static const pxr::TfToken texture_file("texture:file", pxr::TfToken::Immortal);
}  // namespace usdtokens

namespace blender::io::usd {

static const float nits_to_watts_per_meter_sq = 0.0014641f;

static const float watts_per_meter_sq_to_nits = 1.0f / nits_to_watts_per_meter_sq;

/* Return the scale factor to convert nits to light energy
 * (Watts or Watts per meter squared) for the given light. */
float nits_to_energy_scale_factor(const Light *light,
                                  const float meters_per_unit,
                                  const float radius_scale)
{
  if (!light) {
    return 1.0f;
  }

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

/* If the Blender scene has an environment texture,
 * export it as a USD dome light. */
void world_material_to_dome_light(const USDExportParams &params,
                                  const Scene *scene,
                                  pxr::UsdStageRefPtr stage)
{
  if (!(stage && scene && scene->world && scene->world->use_nodes)) {
    return;
  }

  float world_color[3] = {1.0f, 1.0f, 1.0f};
  float world_intensity = 0.0f;
  float tex_rot[3] = {0.0f, 0.0f, 0.0f};

  std::string file_path;

  bool background_found = false;
  bool env_tex_found = false;

  pxr::SdfPath light_path(std::string(params.root_prim_path) + "/lights");

  usd_define_or_over<pxr::UsdGeomScope>(stage, light_path, params.export_as_overs);

  /* Convert node graph to USD Dome Light.
   * TODO(makowalski): verify node connections. */
  for (bNode *node = (bNode *)scene->world->nodetree->nodes.first; node; node = node->next) {

    if (ELEM(node->type, SH_NODE_BACKGROUND)) {
      /* Get light color and intensity */
      bNodeSocketValueRGBA *color_data =
          (bNodeSocketValueRGBA *)((bNodeSocket *)BLI_findlink(&node->inputs, 0))->default_value;
      bNodeSocketValueFloat *strength_data =
          (bNodeSocketValueFloat *)((bNodeSocket *)BLI_findlink(&node->inputs, 1))->default_value;

      background_found = true;
      world_intensity = strength_data->value;
      world_color[0] = color_data->value[0];
      world_color[1] = color_data->value[1];
      world_color[2] = color_data->value[2];
    }
    else if (ELEM(node->type, SH_NODE_TEX_ENVIRONMENT)) {
      /* Get env tex path. */

      file_path = get_node_tex_image_filepath(node, stage, params);

      if (!file_path.empty()) {
        /* Get the rotation. */
        NodeTexEnvironment *tex = static_cast<NodeTexEnvironment *>(node->storage);
        copy_v3_v3(tex_rot, tex->base.tex_mapping.rot);

        env_tex_found = true;

        if (params.export_textures) {
          export_texture(node, stage);
        }
      }
    }
  }

  // Create USD dome light
  if (background_found || env_tex_found) {

    pxr::SdfPath env_light_path = light_path.AppendChild(pxr::TfToken("environment"));

    pxr::UsdLuxDomeLight dome_light = usd_define_or_over<pxr::UsdLuxDomeLight>(
        stage, env_light_path, params.export_as_overs);

    if (env_tex_found) {

      /* Convert radians to degrees. */
      mul_v3_fl(tex_rot, 180.0f / M_PI);

      /* For now, just setting the z-rotation.
       * Note the negative Z rotation with 180 deg offset, to match Create and Maya. */
      pxr::GfVec3f rot(0.0f, 0.0f, -tex_rot[2] + 180.0f);

      pxr::UsdGeomXformCommonAPI xform_api(dome_light);

      xform_api.SetRotate(rot);

      pxr::SdfAssetPath path(file_path);
      dome_light.CreateTextureFileAttr().Set(path);

      if (params.backward_compatible) {
        pxr::UsdAttribute attr = dome_light.GetPrim().CreateAttribute(
            usdtokens::texture_file, pxr::SdfValueTypeNames->Asset, true);
        if (attr) {
          attr.Set(path);
        }
      }
    }
    else {
      pxr::GfVec3f color_val(world_color[0], world_color[1], world_color[2]);
      dome_light.CreateColorAttr().Set(color_val);

      if (params.backward_compatible) {
        pxr::UsdAttribute attr = dome_light.GetPrim().CreateAttribute(
            usdtokens::color, pxr::SdfValueTypeNames->Color3f, true);
        if (attr) {
          attr.Set(color_val);
        }
      }
    }

    if (background_found) {
      float usd_intensity = world_intensity * params.light_intensity_scale;

      if (params.convert_light_to_nits) {
        usd_intensity *= watts_per_meter_sq_to_nits;
      }

      dome_light.CreateIntensityAttr().Set(usd_intensity);

      if (params.backward_compatible) {
        pxr::UsdAttribute attr = dome_light.GetPrim().CreateAttribute(
            usdtokens::intensity, pxr::SdfValueTypeNames->Float, true);
        if (attr) {
          attr.Set(usd_intensity);
        }
      }
    }
  }
}

}  // namespace blender::io::usd
