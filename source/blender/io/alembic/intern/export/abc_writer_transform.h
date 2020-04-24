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
#pragma once

#include "abc_writer_abstract.h"

#include <Alembic/AbcGeom/OXform.h>

namespace ABC {

class ABCTransformWriter : public ABCAbstractWriter {
 private:
  Alembic::AbcGeom::OXform abc_xform_;
  Alembic::AbcGeom::OXformSchema abc_xform_schema_;

 public:
  ABCTransformWriter(const ABCWriterConstructorArgs &args);

 protected:
  void do_write(HierarchyContext &context) override;
  bool check_is_animated(const HierarchyContext &context) const override;
  const Alembic::Abc::OObject get_alembic_object() const override;
};

}  // namespace ABC
