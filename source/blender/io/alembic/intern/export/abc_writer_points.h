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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2016 Kévin Dietrich & 2020 Blender Foundation.
 * All rights reserved.
 */
#pragma once

#include "abc_writer_abstract.h"

#include <Alembic/AbcGeom/OPoints.h>

namespace ABC {

class ABCPointsWriter : public ABCAbstractWriter {
  Alembic::AbcGeom::OPoints abc_points_;
  Alembic::AbcGeom::OPointsSchema abc_points_schema_;

 public:
  ABCPointsWriter(const ABCWriterConstructorArgs &args);

  void create_alembic_objects(const HierarchyContext *context) override;
  const Alembic::Abc::OObject get_alembic_object() const override;

  bool is_supported(const HierarchyContext *context) const override;

 protected:
  bool check_is_animated(const HierarchyContext &context) const override;
  void do_write(HierarchyContext &context) override;
};

}  // namespace ABC
