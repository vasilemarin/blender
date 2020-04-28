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

#include "abc_writer_points.h"

extern "C" {
#include "DNA_object_types.h"
#include "DNA_particle_types.h"

#include "BKE_lattice.h"
#include "BKE_particle.h"

#include "BLI_math.h"

#include "DEG_depsgraph_query.h"
}

#include "CLG_log.h"
static CLG_LogRef LOG = {"io.alembic"};

namespace ABC {

using Alembic::AbcGeom::kVertexScope;
using Alembic::AbcGeom::OPoints;
using Alembic::AbcGeom::OPointsSchema;

ABCPointsWriter::ABCPointsWriter(const ABCWriterConstructorArgs &args) : ABCAbstractWriter(args)
{
}

void ABCPointsWriter::create_alembic_objects()
{
  /* If the object is static, use the default static time sampling. */
  uint32_t timesample_index = is_animated_ ? timesample_index_geometry_ : 0;

  CLOG_INFO(&LOG,
            2,
            "exporting OPoints %s, child of %s, named %s",
            args_.abc_path.c_str(),
            args_.abc_parent.getFullName().c_str(),
            args_.abc_name.c_str());
  abc_points_ = OPoints(args_.abc_parent, args_.abc_name, timesample_index);
  abc_points_schema_ = abc_points_.getSchema();
}

const Alembic::Abc::OObject ABCPointsWriter::get_alembic_object() const
{
  return abc_points_;
}

void ABCPointsWriter::do_write(HierarchyContext &context)
{
  BLI_assert(context.particle_system != nullptr);

  std::vector<Imath::V3f> points;
  std::vector<Imath::V3f> velocities;
  std::vector<float> widths;
  std::vector<uint64_t> ids;

  ParticleSystem *psys = context.particle_system;
  ParticleKey state;
  ParticleSimulationData sim;
  sim.depsgraph = args_.depsgraph;
  sim.scene = DEG_get_input_scene(args_.depsgraph);
  sim.ob = context.object;
  sim.psys = psys;

  psys->lattice_deform_data = psys_create_lattice_deform_data(&sim);

  state.time = DEG_get_ctime(args_.depsgraph);
  CLOG_INFO(&LOG, 2, "%s: time is %f", args_.abc_path.c_str(), state.time);

  uint64_t index = 0;
  for (int p = 0; p < psys->totpart; p++) {
    float pos[3], vel[3];

    if (psys->particles[p].flag & (PARS_NO_DISP | PARS_UNEXIST)) {
      continue;
    }

    if (psys_get_particle_state(&sim, p, &state, 0) == 0) {
      CLOG_INFO(&LOG, 2, "%s: no particle %d!", args_.abc_path.c_str(), p);
      continue;
    }

    /* location */
    mul_v3_m4v3(pos, context.object->imat, state.co);
    CLOG_INFO(&LOG,
              2,
              "%s: particle %d at %6.3f, %6.3f, %6.3f",
              args_.abc_path.c_str(),
              p,
              pos[0],
              pos[1],
              pos[2]);

    /* velocity */
    sub_v3_v3v3(vel, state.co, psys->particles[p].prev_state.co);

    /* Convert Z-up to Y-up. */
    points.push_back(Imath::V3f(pos[0], pos[2], -pos[1]));
    velocities.push_back(Imath::V3f(vel[0], vel[2], -vel[1]));
    widths.push_back(psys->particles[p].size);
    ids.push_back(index++);
  }

  if (psys->lattice_deform_data) {
    end_latt_deform(psys->lattice_deform_data);
    psys->lattice_deform_data = NULL;
  }

  Alembic::Abc::P3fArraySample psample(points);
  Alembic::Abc::UInt64ArraySample idsample(ids);
  Alembic::Abc::V3fArraySample vsample(velocities);
  Alembic::Abc::FloatArraySample wsample_array(widths);
  Alembic::AbcGeom::OFloatGeomParam::Sample wsample(wsample_array, kVertexScope);

  OPointsSchema::Sample sample(psample, idsample, vsample, wsample);
  update_bounding_box(context.object);
  sample.setSelfBounds(bounding_box_);
  abc_points_schema_.set(sample);
}

}  // namespace ABC
