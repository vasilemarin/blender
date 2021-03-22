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
 */

#pragma once

#ifdef WITH_PYTHON

#include <pxr/usd/usdShade/material.h>

#include "Python.h"

struct bNode;
struct bNodeTree;
struct Main;
struct Material;
struct USDImportParams;

namespace blender::io::usd {

// Helper struct used when arranging nodes in columns, keeping track the
// occupancy information for a given column.  I.e., for column n,
// column_offsets[n] is the y-offset (from top to bottom) of the occupied
// region in that column.
struct UMMNodePlacementContext {
  float origx;
  float origy;
  std::vector<float> column_offsets;
  const float horizontal_step;

  UMMNodePlacementContext(float in_origx, float in_origy, float in_horizontal_step = 300.0f)
    : origx(in_origx),
    origy(in_origy),
    column_offsets(64, 0.0f),
    horizontal_step(in_horizontal_step)
  {
  }
};

class USDUMM {
 private:
   static PyObject *s_umm_module;

   USDImportParams params_;
   Main *bmain_;

 public:
   USDUMM(const USDImportParams &params, Main *bmain);

   ~USDUMM();

   static bool ensure_module_loaded();
   static void test_python();

   bool map_mdl(Material *mtl,
                const pxr::UsdShadeMaterial &usd_material) const;

 protected:
   bool map_omni_pbr(Material *mtl,
                     const pxr::UsdShadeShader &usd_shader) const;

   PyObject *get_omni_pbr_source_data(const pxr::UsdShadeShader &usd_shader) const;

   void create_blender_nodes(Material *mtl, PyObject *data_tuple) const;

   void add_texture_node(const char *tex_file,
                         bNode *dest_node,
                         const char *dest_socket_name,
                         bNodeTree *ntree,
                         int column,
                         UMMNodePlacementContext &r_ctx) const;
};

}  // Namespace blender::io::usd

#endif
