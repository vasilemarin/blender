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
#include "abc_writer_abstract.h"
#include "abc_hierarchy_iterator.h"

extern "C" {
#include "BKE_animsys.h"
#include "BKE_key.h"
#include "BKE_object.h"

#include "DNA_modifier_types.h"

#include "CLG_log.h"

static CLG_LogRef LOG = {"io.alembic"};
}

namespace ABC {

using Alembic::Abc::OObject;
using Alembic::Abc::TimeSamplingPtr;

ABCAbstractWriter::ABCAbstractWriter(const ABCWriterConstructorArgs &args)
    : args_(args), frame_has_been_written_(false), is_animated_(false)
{
}

ABCAbstractWriter::~ABCAbstractWriter()
{
}

bool ABCAbstractWriter::is_supported(const HierarchyContext * /*context*/) const
{
  return true;
}

void ABCAbstractWriter::write(HierarchyContext &context)
{
  if (!frame_has_been_written_) {
    /* The start and end frames being the same means "no animation". */
    is_animated_ = (args_.export_params.frame_start != args_.export_params.frame_end) &&
                   check_is_animated(context);
  }
  else if (!is_animated_) {
    /* A frame has already been written, and without animation one frame is enough. */
    return;
  }

  do_write(context);

  frame_has_been_written_ = true;
}

const OObject ABCAbstractWriter::get_alembic_parent(HierarchyContext &context,
                                                    bool is_obdata) const
{
  OObject parent;

  if (is_obdata) {
    /* The Alembic parent of object data is always the transform of the object. */
    if (context.custom_data != nullptr) {
      ABCAbstractWriter *xform_writer = reinterpret_cast<ABCAbstractWriter *>(context.custom_data);
      parent = xform_writer->get_alembic_object();
    }
  }
  else {
    /* If there is a parent context known, try to find its Alembic object. */
    if (context.parent_context != nullptr && context.parent_context->custom_data != nullptr) {
      ABCAbstractWriter *parent_writer = reinterpret_cast<ABCAbstractWriter *>(
          context.parent_context->custom_data);
      parent = parent_writer->get_alembic_object();
    }
  }

  if (!parent.valid()) {
    /* An invalid parent object means "no parent", which should be translated to Alembic's top
     * archive object. */
    return args_.abc_archive->archive->getTop();
  }

  return parent;
}

void ABCAbstractWriter::update_bounding_box(Object *object)
{
  BoundBox *bb = BKE_object_boundbox_get(object);

  if (!bb) {
    if (object->type != OB_CAMERA) {
      CLOG_WARN(&LOG, "Bounding box is null!\n");
    }
    bounding_box_.min.x = bounding_box_.min.y = bounding_box_.min.z = 0;
    bounding_box_.max.x = bounding_box_.max.y = bounding_box_.max.z = 0;
    return;
  }

  /* Convert Z-up to Y-up. This also changes which vector goes into which min/max property. */
  bounding_box_.min.x = bb->vec[0][0];
  bounding_box_.min.y = bb->vec[0][2];
  bounding_box_.min.z = -bb->vec[6][1];

  bounding_box_.max.x = bb->vec[6][0];
  bounding_box_.max.y = bb->vec[6][2];
  bounding_box_.max.z = -bb->vec[0][1];
}

}  // namespace ABC
