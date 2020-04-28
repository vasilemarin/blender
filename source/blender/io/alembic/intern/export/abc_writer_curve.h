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
#include "abc_writer_mesh.h"

#include <Alembic/AbcGeom/CurveType.h>

namespace ABC {

extern const std::string ABC_CURVE_RESOLUTION_U_PROPNAME;

class ABCCurveWriter : public ABCAbstractWriter {
 private:
  Alembic::AbcGeom::OCurves abc_curve_;
  Alembic::AbcGeom::OCurvesSchema abc_curve_schema_;

 public:
  ABCCurveWriter(const ABCWriterConstructorArgs &args);

  void create_alembic_objects(const HierarchyContext *context) override;
  const Alembic::Abc::OObject get_alembic_object() const override;

 protected:
  void do_write(HierarchyContext &context) override;
};

class ABCCurveMeshWriter : public ABCGenericMeshWriter {
 public:
  ABCCurveMeshWriter(const ABCWriterConstructorArgs &args);

 protected:
  virtual Mesh *get_export_mesh(Object *object_eval, bool &r_needsfree) override;
};

}  // namespace ABC
