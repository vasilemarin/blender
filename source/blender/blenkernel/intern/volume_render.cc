/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/volume_render.cc
 *  \ingroup bke
 */

#include "BLI_math_matrix.h"
#include "BLI_math_vector.h"

#include "BKE_volume.h"
#include "BKE_volume_render.h"

#ifdef WITH_OPENVDB
#  include <openvdb/openvdb.h>
#  include <openvdb/tools/Dense.h>
#endif

/* Dense Voxels */

bool BKE_volume_grid_dense_bounds(const Volume *volume,
                                  VolumeGrid *volume_grid,
                                  size_t min[3],
                                  size_t max[3])
{
#ifdef WITH_OPENVDB
  openvdb::GridBase::ConstPtr grid = BKE_volume_grid_openvdb_for_read(volume, volume_grid);

  openvdb::CoordBBox bbox = grid->evalActiveVoxelBoundingBox();
  if (!bbox.empty()) {
    /* OpenVDB bbox is inclusive, so add 1 to convert. */
    min[0] = bbox.min().x();
    min[1] = bbox.min().y();
    min[2] = bbox.min().z();
    max[0] = bbox.max().x() + 1;
    max[1] = bbox.max().y() + 1;
    max[2] = bbox.max().z() + 1;
    return true;
  }
#else
  UNUSED_VARS(volume, volume_grid);
#endif

  min[0] = 0;
  min[1] = 0;
  min[2] = 0;
  max[0] = 0;
  max[1] = 0;
  max[2] = 0;
  return false;
}

/* Transform matrix from unit cube to object space, for 3D texture sampling. */
void BKE_volume_grid_dense_transform_matrix(const VolumeGrid *volume_grid,
                                            const size_t min[3],
                                            const size_t max[3],
                                            float mat[4][4])
{
#ifdef WITH_OPENVDB
  float index_to_world[4][4];
  BKE_volume_grid_transform_matrix(volume_grid, index_to_world);

  float texture_to_index[4][4];
  float loc[3] = {(float)min[0], (float)min[1], (float)min[2]};
  float size[3] = {(float)(max[0] - min[0]), (float)(max[1] - min[1]), (float)(max[2] - min[2])};
  size_to_mat4(texture_to_index, size);
  copy_v3_v3(texture_to_index[3], loc);

  mul_m4_m4m4(mat, index_to_world, texture_to_index);
#else
  UNUSED_VARS(volume_grid, min, max);
  unit_m4(mat);
#endif
}

void BKE_volume_grid_dense_voxels(const Volume *volume,
                                  VolumeGrid *volume_grid,
                                  const size_t min[3],
                                  const size_t max[3],
                                  float *voxels)
{
#ifdef WITH_OPENVDB
  openvdb::GridBase::ConstPtr grid = BKE_volume_grid_openvdb_for_read(volume, volume_grid);

  /* Convert to OpenVDB inclusive bbox with -1. */
  openvdb::CoordBBox bbox(min[0], min[1], min[2], max[0] - 1, max[1] - 1, max[2] - 1);

  switch (BKE_volume_grid_type(volume_grid)) {
    case VOLUME_GRID_BOOLEAN: {
      openvdb::tools::Dense<float, openvdb::tools::LayoutXYZ> dense(bbox, voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::BoolGrid>(grid), dense);
      break;
    }
    case VOLUME_GRID_FLOAT: {
      openvdb::tools::Dense<float, openvdb::tools::LayoutXYZ> dense(bbox, voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::FloatGrid>(grid), dense);
      break;
    }
    case VOLUME_GRID_DOUBLE: {
      openvdb::tools::Dense<float, openvdb::tools::LayoutXYZ> dense(bbox, voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::DoubleGrid>(grid), dense);
      break;
    }
    case VOLUME_GRID_INT: {
      openvdb::tools::Dense<float, openvdb::tools::LayoutXYZ> dense(bbox, voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::Int32Grid>(grid), dense);
      break;
    }
    case VOLUME_GRID_INT64: {
      openvdb::tools::Dense<float, openvdb::tools::LayoutXYZ> dense(bbox, voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::Int64Grid>(grid), dense);
      break;
    }
    case VOLUME_GRID_MASK: {
      openvdb::tools::Dense<float, openvdb::tools::LayoutXYZ> dense(bbox, voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::MaskGrid>(grid), dense);
      break;
    }
    case VOLUME_GRID_VECTOR_FLOAT: {
      openvdb::tools::Dense<openvdb::Vec3f, openvdb::tools::LayoutXYZ> dense(
          bbox, (openvdb::Vec3f *)voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::Vec3fGrid>(grid), dense);
      break;
    }
    case VOLUME_GRID_VECTOR_DOUBLE: {
      openvdb::tools::Dense<openvdb::Vec3f, openvdb::tools::LayoutXYZ> dense(
          bbox, (openvdb::Vec3f *)voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::Vec3dGrid>(grid), dense);
      break;
    }
    case VOLUME_GRID_VECTOR_INT: {
      openvdb::tools::Dense<openvdb::Vec3f, openvdb::tools::LayoutXYZ> dense(
          bbox, (openvdb::Vec3f *)voxels);
      openvdb::tools::copyToDense(*openvdb::gridConstPtrCast<openvdb::Vec3IGrid>(grid), dense);
      break;
    }
    case VOLUME_GRID_STRING:
    case VOLUME_GRID_UNKNOWN: {
      /* Zero channels to copy. */
      break;
    }
  }
#else
  UNUSED_VARS(volume, volume_grid, min, max, voxels);
#endif
}
