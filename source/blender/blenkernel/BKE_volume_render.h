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
 * Contributor(s): Brecht Van Lommel.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_VOLUME_RENDER_H__
#define __BKE_VOLUME_RENDER_H__

/** \file BKE_volume_render.h
 *  \ingroup bke
 *  \brief Volume datablock rendering and viewport drawing utilities.
 */
#ifdef __cplusplus
extern "C" {
#endif

struct Volume;
struct VolumeGrid;

/* Dense Voxels */

bool BKE_volume_grid_dense_bounds(const struct Volume *volume,
                                  struct VolumeGrid *volume_grid,
                                  size_t min[3],
                                  size_t max[3]);
void BKE_volume_grid_dense_transform_matrix(const struct VolumeGrid *volume_grid,
                                            const size_t min[3],
                                            const size_t max[3],
                                            float mat[4][4]);
void BKE_volume_grid_dense_voxels(const struct Volume *volume,
                                  struct VolumeGrid *volume_grid,
                                  const size_t min[3],
                                  const size_t max[3],
                                  float *voxels);

#ifdef __cplusplus
}
#endif

#endif
