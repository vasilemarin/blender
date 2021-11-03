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
 * Copyright 2020, Blender Foundation.
 */

/** \file
 * \ingroup draw_editors
 *
 * Draw engine to draw the Image/UV editor
 */

#include "DRW_render.h"

#include <memory>
#include <optional>

#include "BKE_image.h"
#include "BKE_main.h"
#include "BKE_object.h"

#include "DNA_camera_types.h"
#include "DNA_screen_types.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "ED_image.h"

#include "GPU_batch.h"

#include "image_engine.h"
#include "image_private.hh"

namespace blender::draw::image_engine {

/* Forward declarations. */
class AbstractSpaceAccessor;
static void image_cache_image(
    AbstractSpaceAccessor &space, IMAGE_Data *vedata, Image *image, ImageUser *iuser, ImBuf *ibuf);

#define IMAGE_DRAW_FLAG_SHOW_ALPHA (1 << 0)
#define IMAGE_DRAW_FLAG_APPLY_ALPHA (1 << 1)
#define IMAGE_DRAW_FLAG_SHUFFLING (1 << 2)
#define IMAGE_DRAW_FLAG_DEPTH (1 << 3)
#define IMAGE_DRAW_FLAG_DO_REPEAT (1 << 4)
#define IMAGE_DRAW_FLAG_USE_WORLD_POS (1 << 5)

struct ShaderParameters {
  constexpr static float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  int flags = 0;
  float shuffle[4];
  float far_near[2];
  bool use_premul_alpha = false;

  ShaderParameters()
  {
    copy_v4_fl(shuffle, 1.0f);
    copy_v2_fl2(far_near, 100.0f, 0.0f);
  }

  void set_far_near(Camera *camera)
  {
    far_near[1] = camera->clip_start;
    far_near[0] = camera->clip_end;
  }
};

class AbstractSpaceAccessor {
 public:
  virtual void release_buffer(Image *image, ImBuf *ibuf, void *lock) = 0;
  virtual Image *get_image(Main *bmain) = 0;
  virtual ImageUser *get_image_user() = 0;
  virtual ImBuf *acquire_image_buffer(Image *image, void **lock) = 0;
  virtual void get_shader_parameters(ShaderParameters &r_shader_parameters,
                                     ImBuf *ibuf,
                                     bool is_tiled) = 0;

  virtual std::optional<DRWView *> create_view(const ARegion *UNUSED(region))
  {
    return std::nullopt;
  }
};

class SpaceImageAccessor : public AbstractSpaceAccessor {
  SpaceImage *sima;

 public:
  SpaceImageAccessor(SpaceImage *sima) : sima(sima)
  {
  }

  Image *get_image(Main *UNUSED(bmain)) override
  {
    return ED_space_image(sima);
  }

  ImageUser *get_image_user() override
  {
    return &sima->iuser;
  }

  ImBuf *acquire_image_buffer(Image *UNUSED(image), void **lock) override
  {
    return ED_space_image_acquire_buffer(sima, lock, 0);
  }

  void release_buffer(Image *UNUSED(image), ImBuf *ibuf, void *lock) override
  {
    ED_space_image_release_buffer(sima, ibuf, lock);
  }

  void get_shader_parameters(ShaderParameters &r_shader_parameters,
                             ImBuf *ibuf,
                             bool is_tiled) override
  {
    const int sima_flag = sima->flag & ED_space_image_get_display_channel_mask(ibuf);
    const bool do_repeat = (!is_tiled) && ((sima->flag & SI_DRAW_TILE) != 0);
    SET_FLAG_FROM_TEST(r_shader_parameters.flags, do_repeat, IMAGE_DRAW_FLAG_DO_REPEAT);
    SET_FLAG_FROM_TEST(r_shader_parameters.flags, is_tiled, IMAGE_DRAW_FLAG_USE_WORLD_POS);
    if ((sima_flag & SI_USE_ALPHA) != 0) {
      /* Show RGBA */
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHOW_ALPHA | IMAGE_DRAW_FLAG_APPLY_ALPHA;
    }
    else if ((sima_flag & SI_SHOW_ALPHA) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else if ((sima_flag & SI_SHOW_ZBUF) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_DEPTH | IMAGE_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(r_shader_parameters.shuffle, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    else if ((sima_flag & SI_SHOW_R) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    else if ((sima_flag & SI_SHOW_G) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 1.0f, 0.0f, 0.0f);
    }
    else if ((sima_flag & SI_SHOW_B) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    else /* RGB */ {
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
    }
  }
};

class SpaceNodeAccessor : public AbstractSpaceAccessor {
  SpaceNode *snode;

 public:
  SpaceNodeAccessor(SpaceNode *snode) : snode(snode)
  {
  }

  Image *get_image(Main *bmain) override
  {
    return BKE_image_ensure_viewer(bmain, IMA_TYPE_COMPOSITE, "Viewer Node");
  }

  ImageUser *get_image_user() override
  {
    return nullptr;
  }

  ImBuf *acquire_image_buffer(Image *image, void **lock) override
  {
    return BKE_image_acquire_ibuf(image, nullptr, lock);
  }

  void release_buffer(Image *image, ImBuf *ibuf, void *lock) override
  {
    BKE_image_release_ibuf(image, ibuf, lock);
  }

  std::optional<DRWView *> create_view(const ARegion *region) override
  {
    /* Setup a screen pixel view. The backdrop of the node editor doesn't follow the region. */
    float winmat[4][4], viewmat[4][4];
    orthographic_m4(viewmat, 0.0, region->winx, 0.0, region->winy, 0.0, 1.0);
    unit_m4(winmat);
    return std::make_optional(DRW_view_create(viewmat, winmat, nullptr, nullptr, nullptr));
  }

  void get_shader_parameters(ShaderParameters &r_shader_parameters,
                             ImBuf *ibuf,
                             bool UNUSED(is_tiled)) override
  {
    if ((snode->flag & SNODE_USE_ALPHA) != 0) {
      /* Show RGBA */
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHOW_ALPHA | IMAGE_DRAW_FLAG_APPLY_ALPHA;
    }
    else if ((snode->flag & SNODE_SHOW_ALPHA) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else if ((snode->flag & SNODE_SHOW_R) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    else if ((snode->flag & SNODE_SHOW_G) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 1.0f, 0.0f, 0.0f);
    }
    else if ((snode->flag & SNODE_SHOW_B) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    else /* RGB */ {
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
    }
  }
};

static std::unique_ptr<AbstractSpaceAccessor> space_accessor_from_context(
    const DRWContextState *draw_ctx)
{
  const char space_type = draw_ctx->space_data->spacetype;
  if (space_type == SPACE_IMAGE) {
    return std::make_unique<SpaceImageAccessor>((SpaceImage *)draw_ctx->space_data);
  }
  if (space_type == SPACE_NODE) {
    return std::make_unique<SpaceNodeAccessor>((SpaceNode *)draw_ctx->space_data);
  }
  BLI_assert_unreachable();
  return nullptr;
}

class ImageEngine {
 private:
  const DRWContextState *draw_ctx;
  IMAGE_Data *vedata;
  std::unique_ptr<AbstractSpaceAccessor> space;

 public:
  ImageEngine(const DRWContextState *draw_ctx, IMAGE_Data *vedata)
      : draw_ctx(draw_ctx), vedata(vedata), space(space_accessor_from_context(draw_ctx))
  {
  }

  virtual ~ImageEngine() = default;

  static DRWPass *create_image_pass()
  {
    /* Write depth is needed for background overlay rendering. Near depth is used for
     * transparency checker and Far depth is used for indicating the image size. */
    DRWState state = static_cast<DRWState>(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                           DRW_STATE_DEPTH_ALWAYS | DRW_STATE_BLEND_ALPHA_PREMUL);
    return DRW_pass_create("Image", state);
  }

  void cache_init()
  {
    IMAGE_StorageList *stl = vedata->stl;
    IMAGE_PrivateData *pd = stl->pd;
    IMAGE_PassList *psl = vedata->psl;

    psl->image_pass = create_image_pass();

    Main *bmain = CTX_data_main(draw_ctx->evil_C);
    const ARegion *region = draw_ctx->region;
    pd->image = space->get_image(bmain);
    pd->ibuf = space->acquire_image_buffer(pd->image, &pd->lock);
    ImageUser *iuser = space->get_image_user();
    image_cache_image(*space, vedata, pd->image, iuser, pd->ibuf);

    pd->view = nullptr;
    std::optional<DRWView *> view_override = space->create_view(region);
    if (view_override.has_value()) {
      pd->view = view_override.value();
    }
  }

  void draw_finish()
  {
    IMAGE_StorageList *stl = vedata->stl;
    IMAGE_PrivateData *pd = stl->pd;

    space->release_buffer(pd->image, pd->ibuf, pd->lock);

    pd->image = nullptr;
    pd->ibuf = nullptr;

    if (pd->texture && pd->owns_texture) {
      GPU_texture_free(pd->texture);
      pd->owns_texture = false;
    }
    pd->texture = nullptr;
  }

  void draw_scene()
  {
    IMAGE_PassList *psl = vedata->psl;
    IMAGE_PrivateData *pd = vedata->stl->pd;

    DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
    GPU_framebuffer_bind(dfbl->default_fb);
    static float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GPU_framebuffer_clear_color_depth(dfbl->default_fb, clear_col, 1.0);

    DRW_view_set_active(pd->view);
    DRW_draw_pass(psl->image_pass);
    DRW_view_set_active(nullptr);

    draw_finish();
  }
};

static void image_cache_image_add(DRWShadingGroup *grp, Image *image, ImBuf *ibuf)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const ARegion *region = draw_ctx->region;
  const char space_type = draw_ctx->space_data->spacetype;

  float zoom_x = 1.0f;
  float zoom_y = 1.0f;
  float translate_x = 0.0f;
  float translate_y = 0.0f;

  /* User can freely move the backdrop in the space of the node editor */
  if (space_type == SPACE_NODE) {
    SpaceNode *snode = (SpaceNode *)draw_ctx->space_data;
    const float ibuf_width = ibuf->x;
    const float ibuf_height = ibuf->y;
    const float x = (region->winx - snode->zoom * ibuf_width) / 2 + snode->xof;
    const float y = (region->winy - snode->zoom * ibuf_height) / 2 + snode->yof;

    zoom_x = ibuf_width * snode->zoom;
    zoom_y = ibuf_height * snode->zoom;
    translate_x = x;
    translate_y = y;
  }

  const bool is_tiled_texture = image && image->source == IMA_SRC_TILED;
  float obmat[4][4];
  unit_m4(obmat);

  GPUBatch *geom = DRW_cache_quad_get();

  obmat[0][0] = zoom_x;
  obmat[1][1] = zoom_y;
  obmat[3][1] = translate_y;
  obmat[3][0] = translate_x;

  if (is_tiled_texture) {
    LISTBASE_FOREACH (ImageTile *, tile, &image->tiles) {
      const int tile_x = ((tile->tile_number - 1001) % 10);
      const int tile_y = ((tile->tile_number - 1001) / 10);
      obmat[3][1] = (float)tile_y + translate_y;
      obmat[3][0] = (float)tile_x + translate_x;
      DRW_shgroup_call_obmat(grp, geom, obmat);
    }
  }
  else {
    DRW_shgroup_call_obmat(grp, geom, obmat);
  }
}

static void space_image_gpu_texture_get(Image *image,
                                        ImageUser *iuser,
                                        ImBuf *ibuf,
                                        GPUTexture **r_gpu_texture,
                                        bool *r_owns_texture,
                                        GPUTexture **r_tex_tile_data)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;
  if (image->rr != nullptr) {
    /* Update multi-index and pass for the current eye. */
    BKE_image_multilayer_index(image->rr, &sima->iuser);
  }
  else {
    BKE_image_multiview_index(image, &sima->iuser);
  }

  if (ibuf == nullptr) {
    return;
  }

  if (ibuf->rect == nullptr && ibuf->rect_float == nullptr) {
    /* This code-path is only supposed to happen when drawing a lazily-allocatable render result.
     * In all the other cases the `ED_space_image_acquire_buffer()` is expected to return nullptr
     * as an image buffer when it has no pixels. */

    BLI_assert(image->type == IMA_TYPE_R_RESULT);

    float zero[4] = {0, 0, 0, 0};
    *r_gpu_texture = GPU_texture_create_2d(__func__, 1, 1, 0, GPU_RGBA16F, zero);
    *r_owns_texture = true;
    return;
  }

  const int sima_flag = sima->flag & ED_space_image_get_display_channel_mask(ibuf);
  if (sima_flag & SI_SHOW_ZBUF && (ibuf->zbuf || ibuf->zbuf_float || (ibuf->channels == 1))) {
    if (ibuf->zbuf) {
      BLI_assert_msg(0, "Integer based depth buffers not supported");
    }
    else if (ibuf->zbuf_float) {
      *r_gpu_texture = GPU_texture_create_2d(
          __func__, ibuf->x, ibuf->y, 0, GPU_R16F, ibuf->zbuf_float);
      *r_owns_texture = true;
    }
    else if (ibuf->rect_float && ibuf->channels == 1) {
      *r_gpu_texture = GPU_texture_create_2d(
          __func__, ibuf->x, ibuf->y, 0, GPU_R16F, ibuf->rect_float);
      *r_owns_texture = true;
    }
  }
  else if (image->source == IMA_SRC_TILED) {
    *r_gpu_texture = BKE_image_get_gpu_tiles(image, iuser, ibuf);
    *r_tex_tile_data = BKE_image_get_gpu_tilemap(image, iuser, nullptr);
    *r_owns_texture = false;
  }
  else {
    *r_gpu_texture = BKE_image_get_gpu_texture(image, iuser, ibuf);
    *r_owns_texture = false;
  }
}

static void space_node_gpu_texture_get(Image *image,
                                       ImageUser *iuser,
                                       ImBuf *ibuf,
                                       GPUTexture **r_gpu_texture,
                                       bool *r_owns_texture,
                                       GPUTexture **r_tex_tile_data)
{
  *r_gpu_texture = BKE_image_get_gpu_texture(image, iuser, ibuf);
  *r_owns_texture = false;
  *r_tex_tile_data = nullptr;
}

static void image_gpu_texture_get(Image *image,
                                  ImageUser *iuser,
                                  ImBuf *ibuf,
                                  GPUTexture **r_gpu_texture,
                                  bool *r_owns_texture,
                                  GPUTexture **r_tex_tile_data)
{
  if (!image) {
    return;
  }

  const DRWContextState *draw_ctx = DRW_context_state_get();
  const char space_type = draw_ctx->space_data->spacetype;

  if (space_type == SPACE_IMAGE) {
    space_image_gpu_texture_get(
        image, iuser, ibuf, r_gpu_texture, r_owns_texture, r_tex_tile_data);
  }
  else if (space_type == SPACE_NODE) {
    space_node_gpu_texture_get(image, iuser, ibuf, r_gpu_texture, r_owns_texture, r_tex_tile_data);
  }
}

static void image_cache_image(
    AbstractSpaceAccessor &space, IMAGE_Data *vedata, Image *image, ImageUser *iuser, ImBuf *ibuf)
{
  IMAGE_PassList *psl = vedata->psl;
  IMAGE_StorageList *stl = vedata->stl;
  IMAGE_PrivateData *pd = stl->pd;

  GPUTexture *tex_tile_data = nullptr;
  image_gpu_texture_get(image, iuser, ibuf, &pd->texture, &pd->owns_texture, &tex_tile_data);
  if (pd->texture == nullptr) {
    return;
  }
  const bool is_tiled_texture = tex_tile_data != nullptr;

  ShaderParameters sh_params;

  sh_params.use_premul_alpha = BKE_image_has_gpu_texture_premultiplied_alpha(image, ibuf);

  const DRWContextState *draw_ctx = DRW_context_state_get();
  const Scene *scene = draw_ctx->scene;
  if (scene->camera && scene->camera->type == OB_CAMERA) {
    Camera *camera = static_cast<Camera *>(scene->camera->data);
    sh_params.set_far_near(camera);
  }
  space.get_shader_parameters(sh_params, ibuf, is_tiled_texture);

  GPUShader *shader = IMAGE_shader_image_get(is_tiled_texture);
  DRWShadingGroup *shgrp = DRW_shgroup_create(shader, psl->image_pass);
  if (is_tiled_texture) {
    DRW_shgroup_uniform_texture_ex(shgrp, "imageTileArray", pd->texture, GPU_SAMPLER_DEFAULT);
    DRW_shgroup_uniform_texture(shgrp, "imageTileData", tex_tile_data);
  }
  else {
    DRW_shgroup_uniform_texture_ex(shgrp, "imageTexture", pd->texture, GPU_SAMPLER_DEFAULT);
  }
  DRW_shgroup_uniform_vec2_copy(shgrp, "farNearDistances", sh_params.far_near);
  DRW_shgroup_uniform_vec4_copy(shgrp, "color", ShaderParameters::color);
  DRW_shgroup_uniform_vec4_copy(shgrp, "shuffle", sh_params.shuffle);
  DRW_shgroup_uniform_int_copy(shgrp, "drawFlags", sh_params.flags);
  DRW_shgroup_uniform_bool_copy(shgrp, "imgPremultiplied", sh_params.use_premul_alpha);
  image_cache_image_add(shgrp, image, ibuf);
}

/* -------------------------------------------------------------------- */
/** \name Engine Callbacks
 * \{ */

static void IMAGE_engine_init(void *ved)
{
  IMAGE_shader_library_ensure();
  IMAGE_Data *vedata = (IMAGE_Data *)ved;
  IMAGE_StorageList *stl = vedata->stl;
  if (!stl->pd) {
    stl->pd = static_cast<IMAGE_PrivateData *>(MEM_callocN(sizeof(IMAGE_PrivateData), __func__));
  }
  IMAGE_PrivateData *pd = stl->pd;

  pd->ibuf = nullptr;
  pd->lock = nullptr;
  pd->texture = nullptr;
}

static void IMAGE_cache_init(void *vedata)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  ImageEngine image_engine(draw_ctx, static_cast<IMAGE_Data *>(vedata));
  image_engine.cache_init();
}

static void IMAGE_cache_populate(void *UNUSED(vedata), Object *UNUSED(ob))
{
  /* Function intentional left empty. `cache_populate` is required to be implemented. */
}

static void IMAGE_draw_scene(void *vedata)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  ImageEngine image_engine(draw_ctx, static_cast<IMAGE_Data *>(vedata));
  image_engine.draw_scene();
}

static void IMAGE_engine_free()
{
  IMAGE_shader_free();
}

/** \} */

static const DrawEngineDataSize IMAGE_data_size = DRW_VIEWPORT_DATA_SIZE(IMAGE_Data);

}  // namespace blender::draw::image_engine

extern "C" {

using namespace blender::draw::image_engine;

DrawEngineType draw_engine_image_type = {
    nullptr,               /* next */
    nullptr,               /* prev */
    N_("UV/Image"),        /* idname */
    &IMAGE_data_size,      /* vedata_size */
    &IMAGE_engine_init,    /* engine_init */
    &IMAGE_engine_free,    /* engine_free */
    &IMAGE_cache_init,     /* cache_init */
    &IMAGE_cache_populate, /* cache_populate */
    nullptr,               /* cache_finish */
    &IMAGE_draw_scene,     /* draw_scene */
    nullptr,               /* view_update */
    nullptr,               /* id_update */
    nullptr,               /* render_to_image */
    nullptr,               /* store_metadata */
};
}
