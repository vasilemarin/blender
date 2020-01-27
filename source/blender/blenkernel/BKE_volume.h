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

#ifndef __BKE_VOLUME_H__
#define __BKE_VOLUME_H__

/** \file BKE_volume.h
 *  \ingroup bke
 *  \brief General operations for volumes.
 */
#ifdef __cplusplus
extern "C" {
#endif

struct BoundBox;
struct Depsgraph;
struct Main;
struct Object;
struct Scene;
struct Volume;

void BKE_volume_init(struct Volume *volume);
void BKE_volume_init_grids(struct Volume *volume);
void *BKE_volume_add(struct Main *bmain, const char *name);
void BKE_volume_copy_data(struct Main *bmain,
                          struct Volume *volume_dst,
                          const struct Volume *volume_src,
                          const int flag);
struct Volume *BKE_volume_copy(struct Main *bmain, const struct Volume *volume);
void BKE_volume_make_local(struct Main *bmain, struct Volume *volume, const bool lib_local);
void BKE_volume_free(struct Volume *volume);

void BKE_volume_reload(struct Main *bmain, struct Volume *volume);

struct BoundBox *BKE_volume_boundbox_get(struct Object *ob);

/* Depsgraph */

struct Volume *BKE_volume_new_for_eval(const struct Volume *volume_src);
struct Volume *BKE_volume_copy_for_eval(struct Volume *volume_src, bool reference);

void BKE_volume_data_update(struct Depsgraph *depsgraph,
                            struct Scene *scene,
                            struct Object *object);

/* Draw Cache */

enum {
  BKE_VOLUME_BATCH_DIRTY_ALL = 0,
};

void BKE_volume_batch_cache_dirty_tag(struct Volume *volume, int mode);
void BKE_volume_batch_cache_free(struct Volume *volume);

extern void (*BKE_volume_batch_cache_dirty_tag_cb)(struct Volume *volume, int mode);
extern void (*BKE_volume_batch_cache_free_cb)(struct Volume *volume);

/* Grids
 *
 * When accessing a grid, it must be specified if the grid will be used for
 * reading only metadata, or reading metadata and voxels.
 *
 * This makes it possible to load just the required data on-demand, and share
 * grids between volumes for copy-on-write. */

typedef struct VolumeGrid VolumeGrid;

int BKE_volume_num_grids(struct Volume *volume);
const char *BKE_volume_grids_error_msg(const struct Volume *volume);

/* Grid Metadata */

const VolumeGrid *BKE_volume_grid_for_metadata(struct Volume *volume, int grid_index);
const char *BKE_volume_grid_name(const struct VolumeGrid *grid);

/* Grid Tree and Voxels */

const VolumeGrid *BKE_volume_grid_for_tree(struct Volume *volume, int grid_index);
bool BKE_volume_grid_bounds(const struct VolumeGrid *grid, float min[3], float max[3]);

#ifdef __cplusplus
}
#endif

#endif
