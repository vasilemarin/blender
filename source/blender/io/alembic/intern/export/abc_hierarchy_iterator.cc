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
#include "abc_writer_camera.h"
#include "abc_writer_curve.h"
#include "abc_writer_mesh.h"
#include "abc_writer_metaball.h"
#include "abc_writer_nurbs.h"
#include "abc_writer_points.h"
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
  return ABCWriterConstructorArgs{.depsgraph = depsgraph_,
                                  .abc_archive = abc_archive_,
                                  .abc_parent = get_alembic_parent(context),
                                  .abc_name = context->export_name,
                                  .abc_path = context->export_path,
                                  .hierarchy_iterator = this,
                                  .export_params = params_};
}

AbstractHierarchyWriter *ABCHierarchyIterator::create_transform_writer(
    const HierarchyContext *context)
{
  ABCAbstractWriter *transform_writer = new ABCTransformWriter(writer_constructor_args(context));
  transform_writer->create_alembic_objects(context);
  return transform_writer;
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
      data_writer = new ABCCameraWriter(writer_args);
      break;
    case OB_CURVE:
      if (params_.curves_as_mesh) {
        data_writer = new ABCCurveMeshWriter(writer_args);
      }
      else {
        data_writer = new ABCCurveWriter(writer_args);
      }
      break;
    case OB_SURF:
      if (params_.curves_as_mesh) {
        data_writer = new ABCCurveMeshWriter(writer_args);
      }
      else {
        data_writer = new ABCNurbsWriter(writer_args);
      }
      break;
    case OB_MBALL:
      data_writer = new ABCMetaballWriter(writer_args);
      break;

    case OB_EMPTY:
    case OB_LAMP:
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

  data_writer->create_alembic_objects(context);
  return data_writer;
}

AbstractHierarchyWriter *ABCHierarchyIterator::create_hair_writer(
    const HierarchyContext * /*context*/)
{
  if (!params_.export_hair) {
    return nullptr;
  }
  // ABCAbstractWriter *hair_writer = new ABCHairWriter(writer_constructor_args(context));
  // hair_writer->create_alembic_objects();
  // return hair_writer;
  return nullptr;
}

AbstractHierarchyWriter *ABCHierarchyIterator::create_particle_writer(
    const HierarchyContext *context)
{
  ABCWriterConstructorArgs writer_args = writer_constructor_args(context);
  ABCAbstractWriter *particle_writer = new ABCPointsWriter(writer_args);

  if (!particle_writer->is_supported(context)) {
    delete particle_writer;
    return nullptr;
  }

  particle_writer->create_alembic_objects(context);
  return particle_writer;
}

}  // namespace ABC
