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
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 */

#include "../node_shader_util.h"

#include "BKE_image.h"
#include "BLI_noise.hh"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

namespace blender::nodes {

static void sh_node_tex_image_declare(NodeDeclarationBuilder &b)
{
  b.is_function_node();
  b.add_input<decl::Vector>("Vector").implicit_field();
  b.add_output<decl::Color>("Color").no_muted_links();
  b.add_output<decl::Float>("Alpha").no_muted_links();
};

};  // namespace blender::nodes

static void node_shader_init_tex_image(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeTexImage *tex = (NodeTexImage *)MEM_callocN(sizeof(NodeTexImage), "NodeTexImage");
  BKE_texture_mapping_default(&tex->base.tex_mapping, TEXMAP_TYPE_POINT);
  BKE_texture_colormapping_default(&tex->base.color_mapping);
  BKE_imageuser_default(&tex->iuser);

  node->storage = tex;
}

static int node_shader_gpu_tex_image(GPUMaterial *mat,
                                     bNode *node,
                                     bNodeExecData *UNUSED(execdata),
                                     GPUNodeStack *in,
                                     GPUNodeStack *out)
{
  Image *ima = (Image *)node->id;
  NodeTexImage *tex = (NodeTexImage *)node->storage;

  /* We get the image user from the original node, since GPU image keeps
   * a pointer to it and the dependency refreshes the original. */
  bNode *node_original = node->original ? node->original : node;
  NodeTexImage *tex_original = (NodeTexImage *)node_original->storage;
  ImageUser *iuser = &tex_original->iuser;

  if (!ima) {
    return GPU_stack_link(mat, node, "node_tex_image_empty", in, out);
  }

  GPUNodeLink **texco = &in[0].link;
  if (!*texco) {
    *texco = GPU_attribute(mat, CD_MTFACE, "");
    node_shader_gpu_bump_tex_coord(mat, node, texco);
  }

  node_shader_gpu_tex_mapping(mat, node, in, out);

  eGPUSamplerState sampler_state = GPU_SAMPLER_DEFAULT;

  switch (tex->extension) {
    case SHD_IMAGE_EXTENSION_REPEAT:
      sampler_state |= GPU_SAMPLER_REPEAT;
      break;
    case SHD_IMAGE_EXTENSION_CLIP:
      sampler_state |= GPU_SAMPLER_CLAMP_BORDER;
      break;
    default:
      break;
  }

  if (tex->interpolation != SHD_INTERP_CLOSEST) {
    sampler_state |= GPU_SAMPLER_ANISO | GPU_SAMPLER_FILTER;
    /* TODO(fclem): For now assume mipmap is always enabled. */
    sampler_state |= GPU_SAMPLER_MIPMAP;
  }
  const bool use_cubic = ELEM(tex->interpolation, SHD_INTERP_CUBIC, SHD_INTERP_SMART);

  if (ima->source == IMA_SRC_TILED) {
    const char *gpu_node_name = use_cubic ? "node_tex_tile_cubic" : "node_tex_tile_linear";
    GPUNodeLink *gpu_image = GPU_image_tiled(mat, ima, iuser, sampler_state);
    GPUNodeLink *gpu_image_tile_mapping = GPU_image_tiled_mapping(mat, ima, iuser);
    /* UDIM tiles needs a samper2DArray and sampler1DArray for tile mapping. */
    GPU_stack_link(mat, node, gpu_node_name, in, out, gpu_image, gpu_image_tile_mapping);
  }
  else {
    const char *gpu_node_name = use_cubic ? "node_tex_image_cubic" : "node_tex_image_linear";

    switch (tex->projection) {
      case SHD_PROJ_FLAT: {
        GPUNodeLink *gpu_image = GPU_image(mat, ima, iuser, sampler_state);
        GPU_stack_link(mat, node, gpu_node_name, in, out, gpu_image);
        break;
      }
      case SHD_PROJ_BOX: {
        gpu_node_name = use_cubic ? "tex_box_sample_cubic" : "tex_box_sample_linear";
        GPUNodeLink *wnor, *col1, *col2, *col3;
        GPUNodeLink *vnor = GPU_builtin(GPU_WORLD_NORMAL);
        GPUNodeLink *ob_mat = GPU_builtin(GPU_OBJECT_MATRIX);
        GPUNodeLink *blend = GPU_uniform(&tex->projection_blend);
        GPUNodeLink *gpu_image = GPU_image(mat, ima, iuser, sampler_state);
        /* equivalent to normal_world_to_object */
        GPU_link(mat, "normal_transform_transposed_m4v3", vnor, ob_mat, &wnor);
        GPU_link(mat, gpu_node_name, in[0].link, wnor, gpu_image, &col1, &col2, &col3);
        GPU_link(mat, "tex_box_blend", wnor, col1, col2, col3, blend, &out[0].link, &out[1].link);
        break;
      }
      case SHD_PROJ_SPHERE: {
        /* This projection is known to have a derivative discontinuity.
         * Hide it by turning off mipmapping. */
        sampler_state &= ~GPU_SAMPLER_MIPMAP;
        GPUNodeLink *gpu_image = GPU_image(mat, ima, iuser, sampler_state);
        GPU_link(mat, "point_texco_remap_square", *texco, texco);
        GPU_link(mat, "point_map_to_sphere", *texco, texco);
        GPU_stack_link(mat, node, gpu_node_name, in, out, gpu_image);
        break;
      }
      case SHD_PROJ_TUBE: {
        /* This projection is known to have a derivative discontinuity.
         * Hide it by turning off mipmapping. */
        sampler_state &= ~GPU_SAMPLER_MIPMAP;
        GPUNodeLink *gpu_image = GPU_image(mat, ima, iuser, sampler_state);
        GPU_link(mat, "point_texco_remap_square", *texco, texco);
        GPU_link(mat, "point_map_to_tube", *texco, texco);
        GPU_stack_link(mat, node, gpu_node_name, in, out, gpu_image);
        break;
      }
    }
  }

  if (out[0].hasoutput) {
    if (ELEM(ima->alpha_mode, IMA_ALPHA_IGNORE, IMA_ALPHA_CHANNEL_PACKED) ||
        IMB_colormanagement_space_name_is_data(ima->colorspace_settings.name)) {
      /* Don't let alpha affect color output in these cases. */
      GPU_link(mat, "color_alpha_clear", out[0].link, &out[0].link);
    }
    else {
      /* Output premultiplied alpha depending on alpha socket usage. This makes
       * it so that if we blend the color with a transparent shader using alpha as
       * a factor, we don't multiply alpha into the color twice. And if we do
       * not, then there will be no artifacts from zero alpha areas. */
      if (ima->alpha_mode == IMA_ALPHA_PREMUL) {
        if (out[1].hasoutput) {
          GPU_link(mat, "color_alpha_unpremultiply", out[0].link, &out[0].link);
        }
        else {
          GPU_link(mat, "color_alpha_clear", out[0].link, &out[0].link);
        }
      }
      else {
        if (out[1].hasoutput) {
          GPU_link(mat, "color_alpha_clear", out[0].link, &out[0].link);
        }
        else {
          GPU_link(mat, "color_alpha_premultiply", out[0].link, &out[0].link);
        }
      }
    }
  }

  return true;
}

namespace blender::nodes {

class ImageFunction : public fn::MultiFunction {
 private:
  int interpolation_;
  int projection_;
  float projection_blend_;
  int extension_;
  ImBuf *ibuf_;

 public:
  ImageFunction(
      int interpolation, int projection, float projection_blend, int extension, ImBuf *ibuf)
      : interpolation_(interpolation),
        projection_(projection),
        projection_blend_(projection_blend),
        extension_(extension),
        ibuf_(ibuf)
  {
    static fn::MFSignature signature = create_signature();
    this->set_signature(&signature);
  }

  static fn::MFSignature create_signature()
  {
    fn::MFSignatureBuilder signature{"ImageFunction"};
    signature.single_input<float3>("Vector");
    signature.single_output<ColorGeometry4f>("Color");
    signature.single_output<float>("Alpha");
    return signature.build();
  }

  /* Remap coordinate from 0..1 box to -1..-1 */
  static float3 texco_remap_square(float3 co)
  {
    return (co - float3(0.5f, 0.5f, 0.5f)) * 2.0f;
  }

  /* projections */
  static float2 map_to_tube(const float3 co)
  {
    float len, u, v;
    len = sqrtf(co.x * co.x + co.y * co.y);
    if (len > 0.0f) {
      u = (1.0f - (atan2f(co.x / len, co.y / len) / M_PI)) * 0.5f;
      v = (co.z + 1.0f) * 0.5f;
    }
    else {
      u = v = 0.0f;
    }
    return float2(u, v);
  }

  static float2 map_to_sphere(const float3 co)
  {
    float l = len_v3(co);
    float u, v;
    if (l > 0.0f) {
      if (UNLIKELY(co.x == 0.0f && co.y == 0.0f)) {
        u = 0.0f; /* Otherwise domain error. */
      }
      else {
        u = (1.0f - atan2f(co.x, co.y) / M_PI) / 2.0f;
      }
      v = 1.0f - safe_acosf(co.z / l) / M_PI;
    }
    else {
      u = v = 0.0f;
    }
    return float2(u, v);
  }

  static float2 map_to_box(const float3 co)
  {
    float x1, y1, z1, nor[3];
    int ret;
    float u, v;

    copy_v3_v3(nor, co);

    x1 = fabsf(nor[0]);
    y1 = fabsf(nor[1]);
    z1 = fabsf(nor[2]);

    if (z1 >= x1 && z1 >= y1) {
      u = (co.x + 1.0f) / 2.0f;
      v = (co.y + 1.0f) / 2.0f;
      ret = 0;
    }
    else if (y1 >= x1 && y1 >= z1) {
      u = (co.x + 1.0f) / 2.0f;
      v = (co.z + 1.0f) / 2.0f;
    }
    else {
      u = (co.y + 1.0f) / 2.0f;
      v = (co.z + 1.0f) / 2.0f;
    }
    return float2(u, v);
  }

  void call(IndexMask mask, fn::MFParams params, fn::MFContext UNUSED(context)) const override
  {
    const VArray<float3> &vector = params.readonly_single_input<float3>(0, "Vector");
    MutableSpan<ColorGeometry4f> r_color =
        params.uninitialized_single_output_if_required<ColorGeometry4f>(1, "Color");
    MutableSpan<float> r_alpha = params.uninitialized_single_output_if_required<float>(2, "Alpha");

    const bool compute_color = !r_color.is_empty();
    const bool compute_alpha = !r_alpha.is_empty();

    if (ibuf_) {

      /* Hacked together from old Tex nodes and texture.c and cycles!
       * texture_procedural.c
       * BKE_texture_get_value
       * multitex_nodes_intern
       */

      float xsize = ibuf_->x / 2;
      float ysize = ibuf_->y / 2;

      if ((!xsize) || (!ysize)) {
        return;
      }

      if (!ibuf_->rect_float) {
        BLI_thread_lock(LOCK_IMAGE);
        if (!ibuf_->rect_float) {
          IMB_float_from_rect(ibuf_);
        }
        BLI_thread_unlock(LOCK_IMAGE);
      }

      float xoff, yoff;
      int px, py;

      for (int64_t i : mask) {
        const float *result;
        float2 co;

        /* Remap */
        float3 p = texco_remap_square(vector[i]);

        /* Projection - are these needed for GN - most use cases would be things like height maps.
         */
        if (projection_ == SHD_PROJ_TUBE) {
          co = map_to_tube(p);
        }
        else if (projection_ == SHD_PROJ_SPHERE) {
          co = map_to_sphere(p);
        }
        else if (projection_ == SHD_PROJ_BOX) {
          co = map_to_box(p);  // no blending or normal data
        }
        else {  // SHD_PROJ_FLAT
          co = float2(p.x, p.y);
        }

        xoff = yoff = -1;

        px = (int)((co.x - xoff) * xsize);
        py = (int)((co.y - yoff) * ysize);

        while (px < 0) {
          px += ibuf_->x;
        }
        while (py < 0) {
          py += ibuf_->y;
        }
        while (px >= ibuf_->x) {
          px -= ibuf_->x;
        }
        while (py >= ibuf_->y) {
          py -= ibuf_->y;
        }

        result = ibuf_->rect_float + py * ibuf_->x * 4 + px * 4;

        if (compute_color) {
          copy_v4_v4(r_color[i], result);
        }
        if (compute_alpha) {
          // check for premultiply etc
          r_alpha[i] = result[3];
        }
      }
    }
  }
};

static void sh_node_image_tex_build_multi_function(
    blender::nodes::NodeMultiFunctionBuilder &builder)
{
  bNode &node = builder.node();
  NodeTexImage *tex = (NodeTexImage *)node.storage;
  Image *ima = (Image *)node.id;
  if (ima) {
    /* We get the image user from the original node, since GPU image keeps
     * a pointer to it and the dependency refreshes the original. */
    bNode *node_original = node.original ? node.original : &node;
    NodeTexImage *tex_original = (NodeTexImage *)node_original->storage;
    ImageUser *iuser = &tex_original->iuser;

    ImBuf *ibuf = BKE_image_acquire_ibuf(ima, iuser, NULL);
    if (ibuf) {
      builder.construct_and_set_matching_fn<ImageFunction>(
          tex->interpolation, tex->projection, tex->projection_blend, tex->extension, ibuf);
      BKE_image_release_ibuf(ima, ibuf, NULL);
    }
  }
}

}  // namespace blender::nodes

/* node type definition */
void register_node_type_sh_tex_image(void)
{
  static bNodeType ntype;

  sh_fn_node_type_base(&ntype, SH_NODE_TEX_IMAGE, "Image Texture", NODE_CLASS_TEXTURE, 0);
  ntype.declare = blender::nodes::sh_node_tex_image_declare;
  node_type_init(&ntype, node_shader_init_tex_image);
  node_type_storage(
      &ntype, "NodeTexImage", node_free_standard_storage, node_copy_standard_storage);
  node_type_gpu(&ntype, node_shader_gpu_tex_image);
  node_type_label(&ntype, node_image_label);
  node_type_size_preset(&ntype, NODE_SIZE_LARGE);
  ntype.build_multi_function = blender::nodes::sh_node_image_tex_build_multi_function;

  nodeRegisterType(&ntype);
}
