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
#include "abc_writer_transform.h"
#include "abc_hierarchy_iterator.h"
#include "intern/abc_axis_conversion.h"
#include "intern/abc_util.h"

extern "C" {
#include "BKE_object.h"

#include "BLI_math_matrix.h"

#include "DNA_layer_types.h"
}

#include "CLG_log.h"
static CLG_LogRef LOG = {"io.alembic"};

namespace ABC {

using Alembic::Abc::OObject;
using Alembic::AbcGeom::OXform;
using Alembic::AbcGeom::OXformSchema;
using Alembic::AbcGeom::XformSample;

ABCTransformWriter::ABCTransformWriter(const ABCWriterConstructorArgs &args)
    : ABCAbstractWriter(args)
{
}

void ABCTransformWriter::create_alembic_objects()
{
  CLOG_INFO(&LOG, 2, "exporting %s", args_.abc_path.c_str());
  uint32_t ts_index = args_.abc_archive->time_sampling_index_transforms();
  abc_xform_ = OXform(args_.abc_parent, args_.abc_name, ts_index);
  abc_xform_schema_ = abc_xform_.getSchema();
}

void ABCTransformWriter::do_write(HierarchyContext &context)
{
  float parent_relative_matrix[4][4];  // The object matrix relative to the parent.
  mul_m4_m4m4(parent_relative_matrix, context.parent_matrix_inv_world, context.matrix_world);

  // After this, parent_relative_matrix uses Y=up.
  copy_m44_axis_swap(parent_relative_matrix, parent_relative_matrix, ABC_YUP_FROM_ZUP);

  XformSample xform_sample;
  xform_sample.setMatrix(convert_matrix_datatype(parent_relative_matrix));
  xform_sample.setInheritsXforms(true);
  abc_xform_schema_.set(xform_sample);
}

const OObject ABCTransformWriter::get_alembic_object() const
{
  return abc_xform_;
}

bool ABCTransformWriter::check_is_animated(const HierarchyContext &context) const
{
  if (context.duplicator != NULL) {
    /* This object is being duplicated, so could be emitted by a particle system and thus
     * influenced by forces. TODO(Sybren): Make this more strict. Probably better to get from the
     * depsgraph whether this object instance has a time source. */
    return true;
  }
  return BKE_object_moves_in_time(context.object, context.animation_check_include_parent);
}

}  // namespace ABC
