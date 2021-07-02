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
 * The Original Code is Copyright (C) 2021 Blender Foundation.
 * All rights reserved.
 */
#include "usd_writer_skinned_mesh.h"
#include "usd_hierarchy_iterator.h"
#include "usd_writer_armature.h"

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdSkel/bindingAPI.h>

#include "BKE_mesh.h"
#include "BKE_modifier.h"
#include "BKE_object.h"

#include "DNA_mesh_types.h"
#include "DNA_meta_types.h"

#include <string>

namespace blender::io::usd {

bool is_skinned_mesh(Object *obj)
{
  if (!(obj && obj->data)) {
    return false;
  }

  if (obj->type != OB_MESH) {
    return false;
  }

  return BKE_modifiers_findby_type(obj, eModifierType_Armature) != nullptr;
}

static Object *get_armature_obj(Object *obj)
{
  if (!(obj && obj->data)) {
    return false;
  }

  if (obj->type != OB_MESH) {
    return false;
  }

  ArmatureModifierData *mod = reinterpret_cast<ArmatureModifierData*>(BKE_modifiers_findby_type(obj, eModifierType_Armature));

  return mod ? mod->object : nullptr;
}

USDSkinnedMeshWriter::USDSkinnedMeshWriter(const USDExporterContext &ctx) : USDMeshWriter(ctx)
{
}

void USDSkinnedMeshWriter::do_write(HierarchyContext &context)
{
  USDMeshWriter::do_write(context);

  pxr::UsdStageRefPtr stage = usd_export_context_.stage;
  pxr::UsdTimeCode timecode = get_export_time_code();

  pxr::UsdPrim mesh_prim = stage->GetPrimAtPath(usd_export_context_.usd_path);

  if (!mesh_prim.IsValid()) {
    printf("WARNING: couldn't get valid mesh prim for skinned mesh %s\n", this->usd_export_context_.usd_path.GetString().c_str());
    return;
  }

  pxr::UsdSkelBindingAPI usd_skel_api(mesh_prim);

  Object *obj = get_armature_obj(context.object);

  if (!obj) {
    printf("WARNING: couldn't get armature object for skinned mesh %s\n", this->usd_export_context_.usd_path.GetString().c_str());
    return;
  }

  if (!obj->data) {
    printf("WARNING: couldn't get armature object data for skinned mesh %s\n", this->usd_export_context_.usd_path.GetString().c_str());
    return;
  }

  ID *arm_id = reinterpret_cast<ID*>(obj->data);

  std::string skel_path = usd_export_context_.hierarchy_iterator->get_object_export_path(arm_id);

  if (skel_path.empty()) {
    printf("WARNING: couldn't get USD skeleton path for skinned mesh %s\n", this->usd_export_context_.usd_path.GetString().c_str());
    return;
  }

  usd_skel_api.CreateSkeletonRel().SetTargets(pxr::SdfPathVector({ pxr::SdfPath(skel_path) }));

  std::vector<std::string> bone_names;
  USDArmatureWriter::get_armature_bone_names(obj, bone_names);

  for (const std::string &name : bone_names) {
    printf("bone %s\n", name.c_str());
  }

}

bool USDSkinnedMeshWriter::is_supported(const HierarchyContext *context) const
{
  return is_skinned_mesh(context->object) && USDGenericMeshWriter::is_supported(context);
}

bool USDSkinnedMeshWriter::check_is_animated(const HierarchyContext & /*context*/) const
{
  /* We assume that skinned meshes are never animated, as the source of
   * any animation is the mesh's bound skeleton. */
  return false;
}

}  // namespace blender::io::usd
