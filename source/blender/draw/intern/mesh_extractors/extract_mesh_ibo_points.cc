#include "draw_cache_extract_mesh_private.h"

#include "BLI_vector.hh"

#include "MEM_guardedalloc.h"

namespace blender::draw {

/* ---------------------------------------------------------------------- */
/** \name Extract Point Indices
 * \{ */
struct ExtractPointsUserData {
  GPUIndexBufBuilder builder;
  /* TODO(jbakker): make unique_ptr<Array<GPUIndexBufBuilder>> */
  Vector<GPUIndexBufBuilder> subbuilders;
};

static void *extract_points_init(const MeshRenderData *mr,
                                 struct MeshBatchCache *UNUSED(cache),
                                 void *UNUSED(buf),
                                 const uint task_len)
{
  ExtractPointsUserData *userdata = new ExtractPointsUserData();
  GPU_indexbuf_init(
      &userdata->builder, GPU_PRIM_POINTS, mr->vert_len, mr->loop_len + mr->loop_loose_len);
  userdata->subbuilders.resize(task_len);
  return userdata;
}

static void extract_points_task_init(const uint task_id, void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPU_indexbuf_subbuilder_init(&userdata->builder, &userdata->subbuilders[task_id]);
}

BLI_INLINE void vert_set_bm(GPUIndexBufBuilder *elb, BMVert *eve, int l_index)
{
  const int v_index = BM_elem_index_get(eve);
  if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
    GPU_indexbuf_set_point_vert(elb, v_index, l_index);
  }
  else {
    GPU_indexbuf_set_point_restart(elb, v_index);
  }
}

BLI_INLINE void vert_set_mesh(GPUIndexBufBuilder *elb,
                              const MeshRenderData *mr,
                              const int v_index,
                              const int l_index)
{
  const MVert *mv = &mr->mvert[v_index];
  if (!((mr->use_hide && (mv->flag & ME_HIDE)) ||
        ((mr->extract_type == MR_EXTRACT_MAPPED) && (mr->v_origindex) &&
         (mr->v_origindex[v_index] == ORIGINDEX_NONE)))) {
    GPU_indexbuf_set_point_vert(elb, v_index, l_index);
  }
  else {
    GPU_indexbuf_set_point_restart(elb, v_index);
  }
}

static void extract_points_iter_poly_bm(const MeshRenderData *UNUSED(mr),
                                        const uint task_id,
                                        BMFace *f,
                                        const int UNUSED(f_index),
                                        void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPUIndexBufBuilder *elb = &userdata->subbuilders[task_id];
  BMLoop *l_iter, *l_first;
  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    const int l_index = BM_elem_index_get(l_iter);

    vert_set_bm(elb, l_iter->v, l_index);
  } while ((l_iter = l_iter->next) != l_first);
}

static void extract_points_iter_poly_mesh(const MeshRenderData *mr,
                                          const uint task_id,
                                          const MPoly *mp,
                                          const int UNUSED(mp_index),
                                          void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPUIndexBufBuilder *elb = &userdata->subbuilders[task_id];
  const MLoop *mloop = mr->mloop;
  const int ml_index_end = mp->loopstart + mp->totloop;
  for (int ml_index = mp->loopstart; ml_index < ml_index_end; ml_index += 1) {
    const MLoop *ml = &mloop[ml_index];
    vert_set_mesh(elb, mr, ml->v, ml_index);
  }
}

static void extract_points_iter_ledge_bm(const MeshRenderData *mr,
                                         const uint task_id,
                                         BMEdge *eed,
                                         const int ledge_index,
                                         void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPUIndexBufBuilder *elb = &userdata->subbuilders[task_id];
  vert_set_bm(elb, eed->v1, mr->loop_len + (ledge_index * 2));
  vert_set_bm(elb, eed->v2, mr->loop_len + (ledge_index * 2) + 1);
}

static void extract_points_iter_ledge_mesh(const MeshRenderData *mr,
                                           const uint task_id,
                                           const MEdge *med,
                                           const uint ledge_index,
                                           void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPUIndexBufBuilder *elb = &userdata->subbuilders[task_id];
  vert_set_mesh(elb, mr, med->v1, mr->loop_len + (ledge_index * 2));
  vert_set_mesh(elb, mr, med->v2, mr->loop_len + (ledge_index * 2) + 1);
}

static void extract_points_iter_lvert_bm(const MeshRenderData *mr,
                                         const uint task_id,
                                         BMVert *eve,
                                         const int lvert_index,
                                         void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPUIndexBufBuilder *elb = &userdata->subbuilders[task_id];
  const int offset = mr->loop_len + (mr->edge_loose_len * 2);
  vert_set_bm(elb, eve, offset + lvert_index);
}

static void extract_points_iter_lvert_mesh(const MeshRenderData *mr,
                                           const uint task_id,
                                           const MVert *UNUSED(mv),
                                           const int lvert_index,
                                           void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPUIndexBufBuilder *elb = &userdata->subbuilders[task_id];
  const int offset = mr->loop_len + (mr->edge_loose_len * 2);
  vert_set_mesh(elb, mr, mr->lverts[lvert_index], offset + lvert_index);
}

static void extract_points_task_finish(const uint task_id, void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPU_indexbuf_subbuilder_finish(&userdata->builder, &userdata->subbuilders[task_id]);
}

static void extract_points_finish(const MeshRenderData *UNUSED(mr),
                                  struct MeshBatchCache *UNUSED(cache),
                                  void *buf,
                                  void *_userdata)
{
  ExtractPointsUserData *userdata = static_cast<ExtractPointsUserData *>(_userdata);
  GPUIndexBufBuilder *elb = &userdata->builder;
  GPUIndexBuf *ibo = static_cast<GPUIndexBuf *>(buf);
  GPU_indexbuf_build_in_place(elb, ibo);
  delete userdata;
}

}  // namespace blender::draw

extern "C" {
const MeshExtract extract_points = {
    .init = blender::draw::extract_points_init,
    .task_init = blender::draw::extract_points_task_init,
    .iter_poly_bm = blender::draw::extract_points_iter_poly_bm,
    .iter_poly_mesh = blender::draw::extract_points_iter_poly_mesh,
    .iter_ledge_bm = blender::draw::extract_points_iter_ledge_bm,
    .iter_ledge_mesh = blender::draw::extract_points_iter_ledge_mesh,
    .iter_lvert_bm = blender::draw::extract_points_iter_lvert_bm,
    .iter_lvert_mesh = blender::draw::extract_points_iter_lvert_mesh,
    .task_finish = blender::draw::extract_points_task_finish,
    .finish = blender::draw::extract_points_finish,
    .data_type = MR_DATA_DEFAULT,
    .use_threading = true,
    .mesh_buffer_offset = offsetof(MeshBufferCache, ibo.points),
};
}

/** \} */