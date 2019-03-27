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

/** \file blender/blenkernel/intern/pointcloud.c
 *  \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "DNA_object_types.h"
#include "DNA_pointcloud_types.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_animsys.h"
#include "BKE_customdata.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_pointcloud.h"

/* PointCloud datablock */

static void pointcloud_random(PointCloud *pointcloud)
{
  pointcloud->totpoint = 400;
  CustomData_realloc(&pointcloud->pdata, pointcloud->totpoint);
  BKE_pointcloud_update_customdata_pointers(pointcloud);

  for (int i = 0; i < pointcloud->totpoint; i++) {
    Point *point = &pointcloud->points[i];
    point->co[0] = 2.0f * drand48() - 1.0f;
    point->co[1] = 2.0f * drand48() - 1.0f;
    point->co[2] = 2.0f * drand48() - 1.0f;
    point->radius = 0.05f * drand48();
  }
}

void BKE_pointcloud_init(PointCloud *pointcloud)
{
  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(pointcloud, id));

  pointcloud->flag = 0;
  pointcloud->totpoint = 0;

  CustomData_reset(&pointcloud->pdata);
  CustomData_add_layer(&pointcloud->pdata, CD_POINT, CD_CALLOC, NULL, pointcloud->totpoint);
  BKE_pointcloud_update_customdata_pointers(pointcloud);

  pointcloud_random(pointcloud);
}

void *BKE_pointcloud_add(Main *bmain, const char *name)
{
  PointCloud *pointcloud = BKE_libblock_alloc(bmain, ID_PT, name, 0);

  BKE_pointcloud_init(pointcloud);

  return pointcloud;
}

void BKE_pointcloud_copy_data(Main *UNUSED(bmain),
                              PointCloud *pointcloud_dst,
                              const PointCloud *pointcloud_src,
                              const int flag)
{
  pointcloud_dst->mat = MEM_dupallocN(pointcloud_dst->mat);

  const eCDAllocType alloc_type = (flag & LIB_ID_COPY_CD_REFERENCE) ? CD_REFERENCE : CD_DUPLICATE;
  CustomData_copy(&pointcloud_src->pdata,
                  &pointcloud_dst->pdata,
                  CD_MASK_ALL,
                  alloc_type,
                  pointcloud_dst->totpoint);
  BKE_pointcloud_update_customdata_pointers(pointcloud_dst);
}

PointCloud *BKE_pointcloud_copy(Main *bmain, const PointCloud *pointcloud)
{
  PointCloud *pointcloud_copy;
  BKE_id_copy(bmain, &pointcloud->id, (ID **)&pointcloud_copy);
  return pointcloud_copy;
}

void BKE_pointcloud_make_local(Main *bmain, PointCloud *pointcloud, const bool lib_local)
{
  BKE_id_make_local_generic(bmain, &pointcloud->id, true, lib_local);
}

void BKE_pointcloud_free(PointCloud *pointcloud)
{
  BKE_animdata_free((ID *)pointcloud, false);
  BKE_pointcloud_batch_cache_free(pointcloud);
  CustomData_free(&pointcloud->pdata, pointcloud->totpoint);
  MEM_SAFE_FREE(pointcloud->mat);
}

BoundBox *BKE_pointcloud_boundbox_get(Object *ob)
{
  BLI_assert(ob->type == OB_POINTCLOUD);
  PointCloud *pointcloud = ob->data;

  if (ob->runtime.bb != NULL && (ob->runtime.bb->flag & BOUNDBOX_DIRTY) == 0) {
    return ob->runtime.bb;
  }

  if (ob->runtime.bb == NULL) {
    ob->runtime.bb = MEM_callocN(sizeof(BoundBox), "pointcloud boundbox");

    Point *point = pointcloud->points;
    float min[3], max[3];
    INIT_MINMAX(min, max);
    for (int a = 0; a < pointcloud->totpoint; a++, point++) {
      float co_min[3] = {point->co[0] - point->radius,
                         point->co[1] - point->radius,
                         point->co[2] - point->radius};
      float co_max[3] = {point->co[0] + point->radius,
                         point->co[1] + point->radius,
                         point->co[2] + point->radius};
      DO_MIN(co_min, min);
      DO_MAX(co_max, max);
    }

    BKE_boundbox_init_from_minmax(ob->runtime.bb, min, max);
  }

  return ob->runtime.bb;
}

void BKE_pointcloud_update_customdata_pointers(PointCloud *pointcloud)
{
  pointcloud->points = CustomData_get_layer(&pointcloud->pdata, CD_POINT);
}

/* Dependency Graph */

PointCloud *BKE_pointcloud_new_for_eval(const PointCloud *pointcloud_src, int totpoint)
{
  PointCloud *pointcloud_dst = BKE_id_new_nomain(ID_HA, NULL);

  STRNCPY(pointcloud_dst->id.name, pointcloud_src->id.name);
  pointcloud_dst->mat = MEM_dupallocN(pointcloud_src->mat);
  pointcloud_dst->totcol = pointcloud_src->totcol;

  pointcloud_dst->totpoint = totpoint;
  CustomData_copy(
      &pointcloud_src->pdata, &pointcloud_dst->pdata, CD_MASK_ALL, CD_CALLOC, totpoint);
  BKE_pointcloud_update_customdata_pointers(pointcloud_dst);

  return pointcloud_dst;
}

PointCloud *BKE_pointcloud_copy_for_eval(struct PointCloud *pointcloud_src, bool reference)
{
  int flags = LIB_ID_COPY_LOCALIZE;

  if (reference) {
    flags |= LIB_ID_COPY_CD_REFERENCE;
  }

  PointCloud *result;
  BKE_id_copy_ex(NULL, &pointcloud_src->id, (ID **)&result, flags);
  return result;
}

void BKE_pointcloud_data_update(struct Depsgraph *UNUSED(depsgraph),
                                struct Scene *UNUSED(scene),
                                Object *UNUSED(object))
{
  /* Nothing to do yet. */
}

/* Draw Cache */
void (*BKE_pointcloud_batch_cache_dirty_tag_cb)(PointCloud *pointcloud, int mode) = NULL;
void (*BKE_pointcloud_batch_cache_free_cb)(PointCloud *pointcloud) = NULL;

void BKE_pointcloud_batch_cache_dirty_tag(PointCloud *pointcloud, int mode)
{
  if (pointcloud->batch_cache) {
    BKE_pointcloud_batch_cache_dirty_tag_cb(pointcloud, mode);
  }
}

void BKE_pointcloud_batch_cache_free(PointCloud *pointcloud)
{
  if (pointcloud->batch_cache) {
    BKE_pointcloud_batch_cache_free_cb(pointcloud);
  }
}
