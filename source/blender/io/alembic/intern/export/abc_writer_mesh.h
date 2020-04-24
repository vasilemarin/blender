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
#include "intern/abc_customdata.h"

#include <Alembic/AbcGeom/OPolyMesh.h>
#include <Alembic/AbcGeom/OSubD.h>

struct ModifierData;

namespace ABC {

/* Writer for Alembic geometry. Does not assume the object is a mesh object. */
class ABCGenericMeshWriter : public ABCAbstractWriter {
 private:
  /* Either polymesh or subd is used, depending on is_subd_.
   * References to the schema must be kept, or Alembic will not properly write. */
  Alembic::AbcGeom::OPolyMesh abc_poly_mesh_;
  Alembic::AbcGeom::OPolyMeshSchema abc_poly_mesh_schema_;

  Alembic::AbcGeom::OSubD abc_subdiv_;
  Alembic::AbcGeom::OSubDSchema abc_subdiv_schema_;

  /* Determines whether a poly mesh or a subdivision surface is exported.
   * The value is set by an export option but only true if there is a subsdivision modifier on the
   * exported object. */
  bool is_subd_;
  ModifierData *subsurf_modifier_;
  ModifierData *liquid_sim_modifier_;

  /* Either the same as 'timesample_index_geometry_' or 0 to use Alembic's "constant"
   * timesample index. This is chosen based on whether the mesh is considered animated or not.
   *
   * TODO(Sybren): maybe move this logic to ABCAbstractWriter? */
  uint32_t timesample_index_;

  CDStreamConfig m_custom_data_config;

 public:
  ABCGenericMeshWriter(const ABCWriterConstructorArgs &args);
  virtual ~ABCGenericMeshWriter();

  virtual void create_alembic_objects() override;
  virtual const Alembic::Abc::OObject get_alembic_object() const;

 protected:
  virtual bool is_supported(const HierarchyContext *context) const override;
  virtual void do_write(HierarchyContext &context) override;

  virtual Mesh *get_export_mesh(Object *object_eval, bool &r_needsfree) = 0;
  virtual void free_export_mesh(Mesh *mesh);

  virtual bool export_as_subdivision_surface(Object *ob_eval) const;

 private:
  void write_mesh(HierarchyContext &context, Mesh *mesh);
  void write_subd(HierarchyContext &context, Mesh *mesh);
  template<typename Schema> void write_face_sets(Object *object, Mesh *mesh, Schema &schema);

  ModifierData *get_liquid_sim_modifier(Scene *scene_eval, Object *ob_eval);

  void write_arb_geo_params(Mesh *me);
  void get_velocities(Mesh *mesh, std::vector<Imath::V3f> &vels);
  void get_geo_groups(Object *object,
                      Mesh *mesh,
                      std::map<std::string, std::vector<int32_t>> &geo_groups);
};

/* Writer for Alembic geometry of Blender Mesh objects. */
class ABCMeshWriter : public ABCGenericMeshWriter {
 public:
  ABCMeshWriter(const ABCWriterConstructorArgs &args);

 protected:
  virtual Mesh *get_export_mesh(Object *object_eval, bool &r_needsfree) override;
};

}  // namespace ABC
