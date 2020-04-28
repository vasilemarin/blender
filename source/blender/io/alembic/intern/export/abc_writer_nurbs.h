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
 */
#pragma once

#include "abc_writer_abstract.h"
#include "abc_writer_mesh.h"

#include <vector>

namespace ABC {

class ABCNurbsWriter : public ABCAbstractWriter {
 private:
  std::vector<Alembic::AbcGeom::ONuPatch> abc_nurbs_;
  std::vector<Alembic::AbcGeom::ONuPatchSchema> abc_nurbs_schemas_;

 public:
  ABCNurbsWriter(const ABCWriterConstructorArgs &args);

  void create_alembic_objects() override;
  const Alembic::Abc::OObject get_alembic_object() const override;

 protected:
  bool is_supported(const HierarchyContext *context) const override;
  void do_write(HierarchyContext &context) override;
  bool check_is_animated(const HierarchyContext &context) const override;
};

class ABCNurbsMeshWriter : public ABCGenericMeshWriter {
 public:
  ABCNurbsMeshWriter(const ABCWriterConstructorArgs &args);

 protected:
  virtual Mesh *get_export_mesh(Object *object_eval, bool &r_needsfree) override;
};

}  // namespace ABC
