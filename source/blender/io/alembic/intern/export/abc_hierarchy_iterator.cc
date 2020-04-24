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
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

#include "abc_hierarchy_iterator.h"
#include "abc_writer_abstract.h"
// #include "abc_writer_camera.h"
// #include "abc_writer_hair.h"
// #include "abc_writer_light.h"
#include "abc_writer_mesh.h"
// #include "abc_writer_metaball.h"
#include "abc_writer_transform.h"

#include <string>

extern "C" {

#include "BLI_assert.h"

#include "DEG_depsgraph_query.h"

#include "DNA_ID.h"
#include "DNA_layer_types.h"
#include "DNA_object_types.h"
}

namespace ABC {

ABCHierarchyIterator::ABCHierarchyIterator(Depsgraph *depsgraph,
                                           ABCArchive *abc_archive,
                                           const AlembicExportParams &params)
    : AbstractHierarchyIterator(depsgraph), abc_archive_(abc_archive), params_(params)
{
}

bool ABCHierarchyIterator::mark_as_weak_export(const Object *object) const
{
  if (params_.selected_only && (object->base_flag & BASE_SELECTED) == 0) {
    return true;
  }
  /* TODO(Sybren): handle other flags too? */
  return false;
}

void ABCHierarchyIterator::delete_object_writer(AbstractHierarchyWriter *writer)
{
  delete static_cast<ABCAbstractWriter *>(writer);
}

std::string ABCHierarchyIterator::make_valid_name(const std::string &name) const
{
  std::string abc_name(name);
  std::replace(abc_name.begin(), abc_name.end(), ' ', '_');
  std::replace(abc_name.begin(), abc_name.end(), '.', '_');
  std::replace(abc_name.begin(), abc_name.end(), ':', '_');
  return abc_name;
}

Alembic::Abc::OObject ABCHierarchyIterator::get_alembic_parent(
    const HierarchyContext *context) const
{
  Alembic::Abc::OObject parent;

  if (!context->higher_up_export_path.empty()) {
    AbstractHierarchyWriter *writer = get_writer(context->higher_up_export_path);
    ABCAbstractWriter *abc_writer = static_cast<ABCAbstractWriter *>(writer);
    parent = abc_writer->get_alembic_object();
  }

  if (!parent.valid()) {
    /* An invalid parent object means "no parent", which should be translated to Alembic's top
     * archive object. */
    return abc_archive_->archive->getTop();
  }

  return parent;
}

ABCWriterConstructorArgs ABCHierarchyIterator::writer_constructor_args(
    const HierarchyContext *context) const
{
  return ABCWriterConstructorArgs{context->object,
                                  depsgraph_,
                                  abc_archive_,
                                  get_alembic_parent(context),
                                  context->export_name,
                                  context->export_path,
                                  this,
                                  params_};
}

AbstractHierarchyWriter *ABCHierarchyIterator::create_transform_writer(
    const HierarchyContext *context)
{
  return new ABCTransformWriter(writer_constructor_args(context));
}

AbstractHierarchyWriter *ABCHierarchyIterator::create_data_writer(const HierarchyContext *context)
{
  ABCWriterConstructorArgs writer_args = writer_constructor_args(context);
  ABCAbstractWriter *data_writer = nullptr;

  switch (context->object->type) {
    case OB_MESH:
      data_writer = new ABCMeshWriter(writer_args);
      break;
    case OB_CAMERA:
      // data_writer = new USDCameraWriter(writer_args);
      return nullptr;
      break;
    case OB_LAMP:
      // data_writer = new USDLightWriter(writer_args);
      return nullptr;
      break;
    case OB_MBALL:
      // data_writer = new USDMetaballWriter(writer_args);
      return nullptr;
      break;

    case OB_EMPTY:
    case OB_CURVE:
    case OB_SURF:
    case OB_FONT:
    case OB_SPEAKER:
    case OB_LIGHTPROBE:
    case OB_LATTICE:
    case OB_ARMATURE:
    case OB_GPENCIL:
      return nullptr;
    case OB_TYPE_MAX:
      BLI_assert(!"OB_TYPE_MAX should not be used");
      return nullptr;
  }

  if (!data_writer->is_supported(context)) {
    delete data_writer;
    return nullptr;
  }

  return data_writer;
}

AbstractHierarchyWriter *ABCHierarchyIterator::create_hair_writer(
    const HierarchyContext * /*context*/)
{
  if (!params_.export_hair) {
    return nullptr;
  }
  // return new USDHairWriter(writer_constructor_args(context));
  return nullptr;
}

AbstractHierarchyWriter *ABCHierarchyIterator::create_particle_writer(const HierarchyContext *)
{
  return nullptr;
}

}  // namespace ABC
