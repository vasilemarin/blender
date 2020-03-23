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

#include "DNA_modifier_types.h"
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
    is_animated_ = true /* TODO(Sybren): args_.export_params.export_animation */ &&
                   check_is_animated(context);
  }
  else if (!is_animated_) {
    /* A frame has already been written, and without animation one frame is enough. */
    return;
  }

  do_write(context);

  frame_has_been_written_ = true;
}

const OObject ABCAbstractWriter::get_alembic_parent(HierarchyContext &context) const
{
  OObject parent;

  /* If there is a parent context known, try to find its Alembic object. */
  if (context.parent_context != nullptr && context.parent_context->custom_data != nullptr) {
    ABCAbstractWriter *parent_writer = reinterpret_cast<ABCAbstractWriter *>(
        context.parent_context->custom_data);
    parent = parent_writer->get_alembic_object();
  }

  if (!parent.valid()) {
    /* An invalid parent object means "no parent", which should be translated to Alembic's top
     * archive object. */
    return args_.abc_archive->archive->getTop();
  }

  return parent;
}

}  // namespace ABC
