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

#include <pxr/usd/usdGeom/mesh.h>

#include "BKE_mesh.h"
#include "BKE_modifier.h"
#include "BKE_object.h"

#include "DNA_mesh_types.h"
#include "DNA_meta_types.h"

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

USDSkinnedMeshWriter::USDSkinnedMeshWriter(const USDExporterContext &ctx) : USDMeshWriter(ctx)
{
}

void USDSkinnedMeshWriter::do_write(HierarchyContext &context)
{
  USDMeshWriter::do_write(context);
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
