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

/** \file blender/blenkernel/intern/volume.cc
 *  \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "DNA_object_types.h"
#include "DNA_volume_types.h"

#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_animsys.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_packedFile.h"
#include "BKE_volume.h"

#ifdef WITH_OPENVDB
#  include <mutex>
#  include <openvdb/openvdb.h>

struct VolumeGrid {
  VolumeGrid(const openvdb::GridBase::Ptr &vdb, const bool has_tree) : vdb(vdb), has_tree(has_tree)
  {
  }

  VolumeGrid(const VolumeGrid &other) : vdb(other.vdb), has_tree(other.has_tree)
  {
  }

  /* OpenVDB grid. */
  openvdb::GridBase::Ptr vdb;
  /* Grid may have only metadata and no tree. */
  bool has_tree;
  /* Mutex for on-demand reading of tree. */
  std::mutex mutex;
};

struct VolumeGridVector : public std::vector<VolumeGrid> {
  /* Absolute file path to read voxels from on-demand. */
  char filepath[FILE_MAX];
};
#endif

/* Volume datablock */

void BKE_volume_init(Volume *volume)
{
  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(volume, id));

  volume->filepath[0] = '\0';
  volume->packedfile = NULL;
  volume->flag = 0;
  BKE_volume_init_grids(volume);
}

void BKE_volume_init_grids(Volume *volume)
{
#ifdef WITH_OPENVDB
  if (volume->grids == NULL) {
    volume->grids = OBJECT_GUARDED_NEW(VolumeGridVector);
  }
#else
  UNUSED_VARS(volume);
#endif
}

void *BKE_volume_add(Main *bmain, const char *name)
{
  Volume *volume = (Volume *)BKE_libblock_alloc(bmain, ID_VO, name, 0);

  BKE_volume_init(volume);

  return volume;
}

void BKE_volume_copy_data(Main *UNUSED(bmain),
                          Volume *volume_dst,
                          const Volume *volume_src,
                          const int UNUSED(flag))
{
  if (volume_src->packedfile) {
    volume_dst->packedfile = BKE_packedfile_duplicate(volume_src->packedfile);
  }

  volume_dst->mat = (Material **)MEM_dupallocN(volume_src->mat);
#ifdef WITH_OPENVDB
  if (volume_src->grids) {
    const VolumeGridVector &grids_src = *(volume_src->grids);
    volume_dst->grids = OBJECT_GUARDED_NEW(VolumeGridVector, grids_src);
  }
#endif
}

Volume *BKE_volume_copy(Main *bmain, const Volume *volume)
{
  Volume *volume_copy;
  BKE_id_copy(bmain, &volume->id, (ID **)&volume_copy);
  return volume_copy;
}

void BKE_volume_make_local(Main *bmain, Volume *volume, const bool lib_local)
{
  BKE_id_make_local_generic(bmain, &volume->id, true, lib_local);
}

void BKE_volume_free(Volume *volume)
{
  BKE_animdata_free((ID *)volume, false);
  BKE_volume_batch_cache_free(volume);
  MEM_SAFE_FREE(volume->mat);
#ifdef WITH_OPENVDB
  OBJECT_GUARDED_SAFE_DELETE(volume->grids, VolumeGridVector);
#endif
}

void BKE_volume_reload(Main *bmain, Volume *volume)
{
#ifdef WITH_OPENVDB
  volume->grids->clear();

  /* Get absolute file path. */
  STRNCPY(volume->grids->filepath, volume->filepath);
  BLI_path_abs(volume->grids->filepath, ID_BLEND_PATH(bmain, &volume->id));

  /* TODO: move this to a better place. */
  openvdb::initialize();

  /* Open OpenVDB file. */
  openvdb::io::File file(volume->grids->filepath);
  openvdb::GridPtrVec vdb_grids;

  try {
    file.setCopyMaxBytes(0);
    file.open();
    vdb_grids = *(file.readAllGridMetadata());
  }
  catch (const openvdb::IoError &e) {
    /* TODO: report error to user. */
    std::cerr << e.what() << '\n';
  }

  /* Add grids read from file to own vector, filtering out any NULL pointers. */
  for (const openvdb::GridBase::Ptr vdb_grid : vdb_grids) {
    if (vdb_grid) {
      volume->grids->emplace_back(vdb_grid, false);
    }
  }
#else
  UNUSED_VARS(bmain, volume);
#endif
}

BoundBox *BKE_volume_boundbox_get(Object *ob)
{
  BLI_assert(ob->type == OB_VOLUME);

  if (ob->runtime.bb != NULL && (ob->runtime.bb->flag & BOUNDBOX_DIRTY) == 0) {
    return ob->runtime.bb;
  }

  if (ob->runtime.bb == NULL) {
    ob->runtime.bb = (BoundBox *)MEM_callocN(sizeof(BoundBox), "volume boundbox");

    float min[3], max[3];
    bool have_minmax = false;
    INIT_MINMAX(min, max);

    Volume *volume = (Volume *)ob->data;
    const int num_grids = BKE_volume_num_grids(volume);

    for (int i = 0; i < num_grids; i++) {
      /* TODO: this is quite expensive, how often is it computed? Is there a faster
       * way without actually reading grids? We should ensure copy-on-write does not
       * compute this over and over for static files. */
      const VolumeGrid *grid = BKE_volume_grid_for_tree(volume, i);
      float grid_min[3], grid_max[3];

      if (BKE_volume_grid_bounds(grid, grid_min, grid_max)) {
        DO_MIN(grid_min, min);
        DO_MAX(grid_max, max);
        have_minmax = true;
      }
    }

    if (!have_minmax) {
      min[0] = min[1] = min[2] = -1.0f;
      max[0] = max[1] = max[2] = 1.0f;
    }

    BKE_boundbox_init_from_minmax(ob->runtime.bb, min, max);
  }

  return ob->runtime.bb;
}

/* Dependency Graph */

Volume *BKE_volume_new_for_eval(const Volume *volume_src)
{
  Volume *volume_dst = (Volume *)BKE_id_new_nomain(ID_VO, NULL);

  STRNCPY(volume_dst->id.name, volume_src->id.name);
  volume_dst->mat = (Material **)MEM_dupallocN(volume_src->mat);
  volume_dst->totcol = volume_src->totcol;
  BKE_volume_init_grids(volume_dst);

  return volume_dst;
}

Volume *BKE_volume_copy_for_eval(Volume *volume_src, bool reference)
{
  int flags = LIB_ID_COPY_LOCALIZE;

  if (reference) {
    flags |= LIB_ID_COPY_CD_REFERENCE;
  }

  Volume *result;
  BKE_id_copy_ex(NULL, &volume_src->id, (ID **)&result, flags);
  result->filepath[0] = '\0';

  return result;
}

void BKE_volume_data_update(struct Depsgraph *UNUSED(depsgraph),
                            struct Scene *UNUSED(scene),
                            Object *object)
{
  /* Free any evaluated data and restore original data. */
  BKE_object_free_derived_caches(object);

  /* Modifier evaluation goes here, using BKE_volume_new_for_eval or
   * BKE_volume_copy_for_eval to create a new Volume. */
  Volume *volume = (Volume *)object->data;
  Volume *volume_eval = volume;

  /* Assign evaluated object. */
  const bool is_owned = (volume != volume_eval);
  BKE_object_eval_assign_data(object, &volume_eval->id, is_owned);
}

/* Draw Cache */

void (*BKE_volume_batch_cache_dirty_tag_cb)(Volume *volume, int mode) = NULL;
void (*BKE_volume_batch_cache_free_cb)(Volume *volume) = NULL;

void BKE_volume_batch_cache_dirty_tag(Volume *volume, int mode)
{
  if (volume->batch_cache) {
    BKE_volume_batch_cache_dirty_tag_cb(volume, mode);
  }
}

void BKE_volume_batch_cache_free(Volume *volume)
{
  if (volume->batch_cache) {
    BKE_volume_batch_cache_free_cb(volume);
  }
}

/* Grids */

int BKE_volume_num_grids(Volume *volume)
{
#ifdef WITH_OPENVDB
  return volume->grids->size();
#else
  UNUSED_VARS(volume);
  return 0;
#endif
}

const VolumeGrid *BKE_volume_grid_for_metadata(Volume *volume, int grid_index)
{
#ifdef WITH_OPENVDB
  return &volume->grids->at(grid_index);
#else
  UNUSED_VARS(volume, grid_index);
  return NULL;
#endif
}

const VolumeGrid *BKE_volume_grid_for_tree(Volume *volume, int grid_index)
{
#ifdef WITH_OPENVDB
  VolumeGrid &grid = volume->grids->at(grid_index);

  if (!grid.has_tree) {
    std::lock_guard<std::mutex> lock(grid.mutex);

    if (!grid.has_tree) {
      /* Read OpenVDB grid on-demand. */
      /* TODO: avoid repeating this for multiple grids when we know we will
       * need them? How best to do it without keeping the file open forever? */
      openvdb::io::File file(volume->grids->filepath);
      openvdb::GridPtrVec vdb_grids;

      try {
        file.setCopyMaxBytes(0);
        file.open();
        grid.vdb = file.readGrid(grid.vdb->getName());
      }
      catch (const openvdb::IoError &e) {
        /* TODO: log error with clog. */
        std::cerr << e.what() << '\n';
      }

      grid.has_tree = true;
    }
  }

  return &grid;
#else
  UNUSED_VARS(volume, grid_index);
  return NULL;
#endif
}

/* Grid Metadata */

const char *BKE_volume_grid_name(const VolumeGrid *volume_grid)
{
#ifdef WITH_OPENVDB
  /* Don't use grid.getName() since it copies the string, we want a pointer to the
   * original so it doesn't get freed out of scope. */
  const openvdb::GridBase::Ptr &grid = volume_grid->vdb;
  openvdb::StringMetadata::ConstPtr name_meta = grid->getMetadata<openvdb::StringMetadata>(
      openvdb::GridBase::META_GRID_NAME);
  return (name_meta) ? name_meta->value().c_str() : "";
#else
  UNUSED_VARS(volume_grid);
  return "density";
#endif
}

/* Grid Tree and Voxels */

bool BKE_volume_grid_bounds(const VolumeGrid *volume_grid, float min[3], float max[3])
{
#ifdef WITH_OPENVDB
  const openvdb::GridBase::Ptr &grid = volume_grid->vdb;
  BLI_assert(volume_grid->has_tree);

  if (grid->empty()) {
    INIT_MINMAX(min, max);
    return false;
  }

  openvdb::CoordBBox coordbbox = grid->evalActiveVoxelBoundingBox();
  openvdb::BBoxd bbox = grid->transform().indexToWorld(coordbbox);

  min[0] = (float)bbox.min().x();
  min[1] = (float)bbox.min().y();
  min[2] = (float)bbox.min().z();
  max[0] = (float)bbox.max().x();
  max[1] = (float)bbox.max().y();
  max[2] = (float)bbox.max().z();
  return true;
#else
  UNUSED_VARS(volume_grid);
  INIT_MINMAX(min, max);
  return false;
#endif
}
