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
#include "usd_reader_prim.h"

#include "BKE_image.h"
#include "BKE_light.h"
#include "BKE_node.h"

#include "BLI_listbase.h"
#include "BlI_math.h"
#include "DNA_light_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"
#include "ED_node.h"

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include <iostream>

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

/* Import the dome light as a world material. */

void dome_light_to_world_material(const USDImportParams &params,
                                  const ImportSettings &settings,
                                  Scene *scene,
                                  Main *bmain,
                                  const pxr::UsdLuxDomeLight &dome_light,
                                  const double time)
{
  if (!(scene && scene->world && dome_light)) {
    return;
  }

  if (!scene->world->use_nodes) {
    scene->world->use_nodes = true;
  }

  if (!scene->world->nodetree) {
    scene->world->nodetree = ntreeAddTree(NULL, "Shader Nodetree", "ShaderNodeTree");
    if (!scene->world->nodetree) {
      std::cerr << "WARNING: couldn't create world ntree.\n";
      return;
    }
  }

  bNodeTree *ntree = scene->world->nodetree;
  bNode *output = nullptr;
  bNode *shader = nullptr;

  /* We never delete existing nodes, but we might disconnect them
   * and move them out of the way. */

  /* Look for the output and background shader nodes, which we will reuse.
   * TODO(makowalski): add logic to properly verify node connections. */
  for (bNode *node = static_cast<bNode *>(ntree->nodes.first); node; node = node->next) {
    if (ELEM(node->type, SH_NODE_OUTPUT_WORLD)) {
      output = node;
    }
    else if (ELEM(node->type, SH_NODE_BACKGROUND)) {
      shader = node;
    }
    else {
      /* Move node out of the way. */
      node->locy += 300;
    }
  }

  /* Create the output and shader nodes, if they don't exist. */
  if (!output) {
    output = nodeAddStaticNode(NULL, ntree, SH_NODE_OUTPUT_WORLD);

    if (!output) {
      std::cerr << "WARNING: couldn't create world output node.\n";
      return;
    }

    output->locx = 300.0f;
    output->locy = 300.0f;
  }

  if (!shader) {
    shader = nodeAddStaticNode(NULL, ntree, SH_NODE_BACKGROUND);

    if (!shader) {
      std::cerr << "WARNING: couldn't create world shader node.\n";
      return;
    }

    nodeAddLink(scene->world->nodetree,
                shader,
                nodeFindSocket(shader, SOCK_OUT, "Background"),
                output,
                nodeFindSocket(output, SOCK_IN, "Surface"));

    bNodeSocket *color_sock = nodeFindSocket(shader, SOCK_IN, "Color");
    copy_v3_v3(((bNodeSocketValueRGBA *)color_sock->default_value)->value, &scene->world->horr);

    shader->locx = output->locx - 200;
    shader->locy = output->locy;
  }

  /* Make sure the first input to the shader node is disconnected. */
  bNodeSocket *shader_input = static_cast<bNodeSocket *>(BLI_findlink(&shader->inputs, 0));

  if (shader_input && shader_input->link) {
    nodeRemLink(ntree, shader_input->link);
  }

  pxr::UsdAttribute intensity_attr = dome_light.GetIntensityAttr();

  float intensity = 1.0f;
  intensity_attr.Get(&intensity, time);

  intensity *= params.light_intensity_scale;

  if (params.convert_light_from_nits) {
    intensity *= nits_to_watts_per_meter_sq;
  }

  bNodeSocket *strength_sock = nodeFindSocket(shader, SOCK_IN, "Strength");
  ((bNodeSocketValueFloat *)strength_sock->default_value)->value = intensity;

  bool has_tex = dome_light.GetTextureFileAttr().HasAuthoredValue();

  bool has_color = dome_light.GetColorAttr().HasAuthoredValue();

  pxr::GfVec3f color;

  if (has_color) {
    dome_light.GetColorAttr().Get(&color, time);
  }

  if (!has_tex) {
    if (has_color) {
      bNodeSocket *color_sock = nodeFindSocket(shader, SOCK_IN, "Color");
      copy_v3_v3(((bNodeSocketValueRGBA *)color_sock->default_value)->value, color.data());
    }

    nodeSetActive(ntree, output);
    ntreeUpdateTree(bmain, ntree);

    return;
  }

  /* If the light has authored color, create the color multiply for the env texture output. */
  bNode *mult = nullptr;

  if (has_color) {
    mult = nodeAddStaticNode(NULL, ntree, SH_NODE_VECTOR_MATH);

    if (!mult) {
      std::cerr << "WARNING: couldn't create vector multiply node.\n";
      return;
    }

    nodeAddLink(scene->world->nodetree,
                mult,
                nodeFindSocket(mult, SOCK_OUT, "Vector"),
                shader,
                nodeFindSocket(shader, SOCK_IN, "Color"));

    mult->locx = shader->locx - 200;
    mult->locy = shader->locy;

    mult->custom1 = NODE_VECTOR_MATH_MULTIPLY;

    bNodeSocket *vec_sock = nodeFindSocket(mult, SOCK_IN, "Vector");
    if (vec_sock) {
      vec_sock = vec_sock->next;
    }

    if (vec_sock) {
      copy_v3_v3(((bNodeSocketValueVector *)vec_sock->default_value)->value, color.data());
    }
    else {
      std::cout << "ERROR: couldn't find vector multiply second vector input.\n";
    }
  }

  bNode *tex = nodeAddStaticNode(NULL, ntree, SH_NODE_TEX_ENVIRONMENT);

  if (!tex) {
    std::cerr << "WARNING: couldn't create world environment texture node.\n";
    return;
  }

  if (mult) {
    nodeAddLink(scene->world->nodetree,
                tex,
                nodeFindSocket(tex, SOCK_OUT, "Color"),
                mult,
                nodeFindSocket(mult, SOCK_IN, "Vector"));

    tex->locx = mult->locx - 400;
    tex->locy = mult->locy;
  }
  else {
    nodeAddLink(scene->world->nodetree,
                tex,
                nodeFindSocket(tex, SOCK_OUT, "Color"),
                shader,
                nodeFindSocket(shader, SOCK_IN, "Color"));

    tex->locx = shader->locx - 400;
    tex->locy = shader->locy;
  }

  pxr::SdfAssetPath tex_path;
  dome_light.GetTextureFileAttr().Get(&tex_path, time);

  std::string tex_path_str = tex_path.GetResolvedPath();

  if (tex_path_str.empty()) {
    std::cerr << "WARNING: Couldn't get resolved path for asset " << tex_path
              << " for Texture Image node.\n";
    return;
  }

  Image *image = BKE_image_load_exists(bmain, tex_path_str.c_str());
  if (!image) {
    std::cerr << "WARNING: Couldn't open image file '" << tex_path_str
              << "' for Texture Image node.\n";
    return;
  }

  tex->id = &image->id;

  /* Set the transform. */
  pxr::UsdGeomXformCache xf_cache(time);

  pxr::GfMatrix4d xf = xf_cache.GetLocalToWorldTransform(dome_light.GetPrim());

  if (settings.do_convert_mat) {
    /* Apply matrix for z-up conversion. */
    pxr::GfMatrix4d convert_xf(pxr::GfMatrix4f(settings.conversion_mat));
    xf *= convert_xf;
  }

  pxr::GfRotation rot = xf.ExtractRotation();

  pxr::GfVec3d rot_vec = rot.Decompose(
      pxr::GfVec3d::XAxis(), pxr::GfVec3d::YAxis(), pxr::GfVec3d::ZAxis());

  NodeTexEnvironment *tex_env = static_cast<NodeTexEnvironment *>(tex->storage);
  tex_env->base.tex_mapping.rot[0] = -static_cast<float>(rot_vec[0]);
  tex_env->base.tex_mapping.rot[1] = -static_cast<float>(rot_vec[1]);
  tex_env->base.tex_mapping.rot[2] = 180 - static_cast<float>(rot_vec[2]);

  /* Convert radians to degrees. */
  mul_v3_fl(tex_env->base.tex_mapping.rot, M_PI / 180.0f);

  eul_to_mat4(tex_env->base.tex_mapping.mat, tex_env->base.tex_mapping.rot);

  nodeSetActive(ntree, output);
  ntreeUpdateTree(bmain, ntree);
}

}  // namespace blender::io::usd
