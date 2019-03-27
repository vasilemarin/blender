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
#  include <openvdb/openvdb.h>

#endif

/* For casting from C to OpenVDB data structures. */
#define OPENVDB_GRIDS_CAST(vdb_grids) (*(openvdb::GridPtrVec *)(vdb_grids))
#define OPENVDB_GRID_CAST(grid) (*(openvdb::GridBase *)(grid))

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
  if (volume->vdb_grids == NULL) {
    volume->vdb_grids = OBJECT_GUARDED_NEW(openvdb::GridPtrVec);
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
  if (volume_src->vdb_grids) {
    const openvdb::GridPtrVec &grids_src = OPENVDB_GRIDS_CAST(volume_src->vdb_grids);
    volume_dst->vdb_grids = OBJECT_GUARDED_NEW(openvdb::GridPtrVec, grids_src);
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
  OBJECT_GUARDED_SAFE_DELETE(volume->vdb_grids, openvdb::GridPtrVec);
#endif
}

void BKE_volume_reload(Main *bmain, Volume *volume)
{
#ifdef WITH_OPENVDB
  openvdb::GridPtrVec &grids = OPENVDB_GRIDS_CAST(volume->vdb_grids);
  grids.clear();

  /* Get absolute file path. */
  char filepath[FILE_MAX];
  STRNCPY(filepath, volume->filepath);
  BLI_path_abs(filepath, ID_BLEND_PATH(bmain, &volume->id));

  /* TODO: move this to a better place. */
  openvdb::initialize();

  /* Open OpenVDB file. */
  openvdb::io::File file(filepath);
  try {
    file.setCopyMaxBytes(0);
    file.open();
    grids = *(file.readAllGridMetadata());
    /* TODO: need to filter out NULL grids from vector? or check for NULL everywhere? */
  }
  /* Mostly to catch exceptions related to Blosc not being supported. */
  catch (const openvdb::IoError &e) {
    /* TODO: report error to user. */
    std::cerr << e.what() << '\n';
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
      const openvdb::GridBase &grid = OPENVDB_GRID_CAST(BKE_volume_grid_for_read(volume, i));
      if (grid.empty()) {
        continue;
      }

      openvdb::CoordBBox coordbbox = grid.evalActiveVoxelBoundingBox();
      openvdb::BBoxd bbox = grid.transform().indexToWorld(coordbbox);

      float grid_min[3], grid_max[3];
      grid_min[0] = (float)bbox.min().x();
      grid_min[1] = (float)bbox.min().y();
      grid_min[2] = (float)bbox.min().z();
      grid_max[0] = (float)bbox.max().x();
      grid_max[1] = (float)bbox.max().y();
      grid_max[2] = (float)bbox.max().z();

      DO_MIN(min, grid_min);
      DO_MAX(max, grid_max);
      have_minmax = true;
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
                            Object *UNUSED(object))
{
  /* Nothing to do yet. */
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
  const openvdb::GridPtrVec &grids = OPENVDB_GRIDS_CAST(volume->vdb_grids);
  return grids.size();
#else
  UNUSED_VARS(volume);
  return 0;
#endif
}

const VolumeGrid *BKE_volume_grid_for_metadata(Volume *volume, int grid_index)
{
#ifdef WITH_OPENVDB
  const openvdb::GridPtrVec &grids = OPENVDB_GRIDS_CAST(volume->vdb_grids);
  return (const VolumeGrid *)grids.at(grid_index).get();
#else
  UNUSED_VARS(volume, grid_index);
  return NULL;
#endif
}

const VolumeGrid *BKE_volume_grid_for_read(Volume *volume, int grid_index)
{
#ifdef WITH_OPENVDB
  /* TODO: implement */
  return BKE_volume_grid_for_metadata(volume, grid_index);
#else
  UNUSED_VARS(volume, grid_index);
  return NULL;
#endif
}

VolumeGrid *BKE_volume_grid_for_write(Volume *volume, int grid_index)
{
#ifdef WITH_OPENVDB
  /* TODO: implement */
  return (VolumeGrid *)BKE_volume_grid_for_metadata(volume, grid_index);
#else
  UNUSED_VARS(volume, grid_index);
  return NULL;
#endif
}

const char *BKE_volume_grid_name(const VolumeGrid *grid_)
{
#ifdef WITH_OPENVDB
  /* Don't use grid.getName() since it copies the string, we want a pointer to the
   * original so it doesn't get freed out of scope. */
  const openvdb::GridBase &grid = OPENVDB_GRID_CAST(grid_);
  openvdb::StringMetadata::ConstPtr name_meta = grid.getMetadata<openvdb::StringMetadata>(
      openvdb::GridBase::META_GRID_NAME);
  return (name_meta) ? name_meta->value().c_str() : "";
#else
  UNUSED_VARS(volume, grid_index);
  return "density";
#endif
}
