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
  bool alpha_clear_;
  int alpha_mode_;
  bool is_tiled_;
  ImBuf *ibuf_;

 public:
  ImageFunction(int interpolation,
                int projection,
                float projection_blend,
                int extension,
                bool alpha_clear,
                int alpha_mode,
                bool is_tiled,
                ImBuf *ibuf)
      : interpolation_(interpolation),
        projection_(projection),
        projection_blend_(projection_blend),
        extension_(extension),
        alpha_clear_(alpha_clear),
        alpha_mode_(alpha_mode),
        is_tiled_(is_tiled),
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
  static inline float3 point_texco_remap_square(float3 co)
  {
    return co * 2.0f - 1.0f;
  }

  /* projections */
  static inline float3 point_map_to_tube(const float3 co)
  {
    float u, v;
    v = (co.z + 1.0f) * 0.5f;
    const float len = safe_sqrtf(co.x * co.x + co.y * co.y);
    if (len > 0.0f) {
      u = (1.0f - (atan2f(co.x / len, co.y / len) / (float)M_PI)) * 0.5f;
    }
    else {
      v = u = 0.0f;
    }

    return float3(u, v, 0.0f);
  }

  static inline float3 point_map_to_sphere(const float3 co)
  {
    const float len = len_v3(co);
    float v, u;
    if (len > 0.0f) {
      if (co.x == 0.0f && co.y == 0.0f) {
        u = 0.0f;
      }
      else {
        u = (1.0f - atan2f(co.x, co.y) / (float)M_PI) / 2.0f;
      }

      v = 1.0f - safe_acosf(co.z / len) / (float)M_PI;
    }
    else {
      v = u = 0.0f;
    }

    return float3(u, v, 0.0f);
  }

  static float3 point_map_to_box(const float3 co)
  {
    float u, v;

    const float x1 = fabsf(co.x);
    const float y1 = fabsf(co.y);
    const float z1 = fabsf(co.z);

    if (z1 >= x1 && z1 >= y1) {
      u = (co.x + 1.0f) / 2.0f;
      v = (co.y + 1.0f) / 2.0f;
    }
    else if (y1 >= x1 && y1 >= z1) {
      u = (co.x + 1.0f) / 2.0f;
      v = (co.z + 1.0f) / 2.0f;
    }
    else {
      u = (co.y + 1.0f) / 2.0f;
      v = (co.z + 1.0f) / 2.0f;
    }
    return float3(u, v, 0.0f);
  }

  static inline int wrap_periodic(int x, const int width)
  {
    x %= width;
    if (x < 0)
      x += width;
    return x;
  }

  static inline int wrap_clamp(int x, const int width)
  {
    return std::clamp(x, 0, width - 1);
  }

  static inline float4 image_pixel_lookup(ImBuf *ibuf, const int px, const int py)
  {
    const float *result;
    // result = ibuf->rect_float + py * ibuf->x * 4 + px * 4;
    result = ibuf->rect_float + ((px + py * ibuf->x) * 4);

    /* Clamp 16bits floats limits. Higher/Lower values produce +/-inf. */
    float4::clamp(result, -65520.0f, 65520.0f);

    return result;
  }

  static inline float frac(const float x, int *ix)
  {
    const int i = (int)x - ((x < 0.0f) ? 1 : 0);
    *ix = i;
    return x - (float)i;
  }

  static float4 image_cubic_texture_lookup(ImBuf *ibuf,
                                           const float px,
                                           const float py,
                                           const int extension)
  {
    const int width = ibuf->x;
    const int height = ibuf->y;
    int ix, iy, nix, niy;
    const float tx = frac(px * (float)width - 0.5f, &ix);
    const float ty = frac(py * (float)width - 0.5f, &iy);
    int pix, piy, nnix, nniy;

    switch (extension) {
      case SHD_IMAGE_EXTENSION_REPEAT:
        ix = wrap_periodic(ix, width);
        iy = wrap_periodic(iy, height);
        pix = wrap_periodic(ix - 1, width);
        piy = wrap_periodic(iy - 1, height);
        nix = wrap_periodic(ix + 1, width);
        niy = wrap_periodic(iy + 1, height);
        nnix = wrap_periodic(ix + 2, width);
        nniy = wrap_periodic(iy + 2, height);
        break;
      case SHD_IMAGE_EXTENSION_CLIP:
        if (tx < 0.0f || ty < 0.0f || tx > 1.0f || ty > 1.0f) {
          return float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        ATTR_FALLTHROUGH;
      case SHD_IMAGE_EXTENSION_EXTEND:
        pix = wrap_clamp(ix - 1, width);
        piy = wrap_clamp(iy - 1, height);
        nix = wrap_clamp(ix + 1, width);
        niy = wrap_clamp(iy + 1, height);
        nnix = wrap_clamp(ix + 2, width);
        nniy = wrap_clamp(iy + 2, height);
        ix = wrap_clamp(ix, width);
        iy = wrap_clamp(iy, height);
        break;
      default:
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    /*
    if (px < 0 || px > in->x - 1 || y2 < 0 || y1 > in->y - 1) {
      return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    */

    const int xc[4] = {pix, ix, nix, nnix};
    const int yc[4] = {piy, iy, niy, nniy};
    float u[4], v[4];

    u[0] = (((-1.0f / 6.0f) * tx + 0.5f) * tx - 0.5f) * tx + (1.0f / 6.0f);
    u[1] = ((0.5f * tx - 1.0f) * tx) * tx + (2.0f / 3.0f);
    u[2] = ((-0.5f * tx + 0.5f) * tx + 0.5f) * tx + (1.0f / 6.0f);
    u[3] = (1.0f / 6.0f) * tx * tx * tx;

    v[0] = (((-1.0f / 6.0f) * ty + 0.5f) * ty - 0.5f) * ty + (1.0f / 6.0f);
    v[1] = ((0.5f * ty - 1.0f) * ty) * ty + (2.0f / 3.0f);
    v[2] = ((-0.5f * ty + 0.5f) * ty + 0.5f) * ty + (1.0f / 6.0f);
    v[3] = (1.0f / 6.0f) * ty * ty * ty;

    return (v[0] * (u[0] * (image_pixel_lookup(ibuf, xc[0], yc[0])) +
                    u[1] * (image_pixel_lookup(ibuf, xc[1], yc[0])) +
                    u[2] * (image_pixel_lookup(ibuf, xc[2], yc[0])) +
                    u[3] * (image_pixel_lookup(ibuf, xc[3], yc[0])))) +
           (v[1] * (u[0] * (image_pixel_lookup(ibuf, xc[0], yc[1])) +
                    u[1] * (image_pixel_lookup(ibuf, xc[1], yc[1])) +
                    u[2] * (image_pixel_lookup(ibuf, xc[2], yc[1])) +
                    u[3] * (image_pixel_lookup(ibuf, xc[3], yc[1])))) +
           (v[2] * (u[0] * (image_pixel_lookup(ibuf, xc[0], yc[2])) +
                    u[1] * (image_pixel_lookup(ibuf, xc[1], yc[2])) +
                    u[2] * (image_pixel_lookup(ibuf, xc[2], yc[2])) +
                    u[3] * (image_pixel_lookup(ibuf, xc[3], yc[2])))) +
           (v[3] * (u[0] * (image_pixel_lookup(ibuf, xc[0], yc[3])) +
                    u[1] * (image_pixel_lookup(ibuf, xc[1], yc[3])) +
                    u[2] * (image_pixel_lookup(ibuf, xc[2], yc[3])) +
                    u[3] * (image_pixel_lookup(ibuf, xc[3], yc[3]))));
  }

  static float4 image_linear_texture_lookup(ImBuf *ibuf,
                                            const float px,
                                            const float py,
                                            const int extension)
  {
    const int width = ibuf->x;
    const int height = ibuf->y;
    int ix, iy, nix, niy;
    const float tx = frac(px * (float)width - 0.5f, &ix);
    const float ty = frac(py * (float)width - 0.5f, &iy);

    switch (extension) {
      case SHD_IMAGE_EXTENSION_CLIP:
        if (tx < 0.0f || ty < 0.0f || tx > 1.0f || ty > 1.0f) {
          return float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        ATTR_FALLTHROUGH;
      case SHD_IMAGE_EXTENSION_EXTEND:
        nix = wrap_clamp(ix + 1, width);
        niy = wrap_clamp(iy + 1, height);
        ix = wrap_clamp(ix, width);
        iy = wrap_clamp(iy, height);
        break;
      default:
      case SHD_IMAGE_EXTENSION_REPEAT:
        ix = wrap_periodic(ix, width);
        iy = wrap_periodic(iy, height);
        nix = wrap_periodic(ix + 1, width);
        niy = wrap_periodic(iy + 1, height);
        break;
    }

    return (float4(1.0f) - ty) * (float4(1.0f) - tx) * image_pixel_lookup(ibuf, ix, iy) +
           (float4(1.0f) - ty) * tx * image_pixel_lookup(ibuf, nix, iy) +
           ty * (float4(1.0f) - tx) * image_pixel_lookup(ibuf, ix, niy) +
           ty * tx * image_pixel_lookup(ibuf, nix, niy);
  }

  static float4 image_closest_texture_lookup(ImBuf *ibuf,
                                             const float px,
                                             const float py,
                                             const int extension)
  {
    const int width = ibuf->x;
    const int height = ibuf->y;
    int ix, iy;
    const float tx = frac(px * (float)width - 0.5f, &ix);
    const float ty = frac(py * (float)width - 0.5f, &iy);

    switch (extension) {
      case SHD_IMAGE_EXTENSION_REPEAT:
        ix = wrap_periodic(ix, width);
        iy = wrap_periodic(iy, height);
        break;
      case SHD_IMAGE_EXTENSION_CLIP:
        if (tx < 0.0f || ty < 0.0f || tx > 1.0f || ty > 1.0f) {
          return float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        ATTR_FALLTHROUGH;
      case SHD_IMAGE_EXTENSION_EXTEND:
        ix = wrap_clamp(ix, width);
        iy = wrap_clamp(iy, height);
        break;
      default:
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return image_pixel_lookup(ibuf, ix, iy);
  }

  void call(IndexMask mask, fn::MFParams params, fn::MFContext UNUSED(context)) const override
  {
    const VArray<float3> &vector = params.readonly_single_input<float3>(0, "Vector");
    MutableSpan<ColorGeometry4f> r_color = params.uninitialized_single_output<ColorGeometry4f>(
        1, "Color");
    MutableSpan<float> r_alpha = params.uninitialized_single_output_if_required<float>(2, "Alpha");

    const bool output_color = !r_color.is_empty();
    const bool output_alpha = !r_alpha.is_empty();

    if (ibuf_) {

      /* Hacked together from old Tex nodes and texture.c and cycles and eevee!
       * texture_procedural.c multitex()
       * texture_image.c imagewrap()
       * BKE_texture_get_value()
       * multitex_nodes_intern
       * gpu_shader_material_tex_image.glsl
       */

      /* Do this outside the loop. Not sure it is required. */
      if (!ibuf_->rect_float) {
        BLI_thread_lock(LOCK_IMAGE);
        if (!ibuf_->rect_float) {
          IMB_float_from_rect(ibuf_);
        }
        BLI_thread_unlock(LOCK_IMAGE);
      }

      /* Set flags. */
      const bool use_cubic = ELEM(interpolation_, SHD_INTERP_CUBIC, SHD_INTERP_SMART);
      const bool use_linear = interpolation_ == SHD_INTERP_LINEAR;

      for (int64_t i : mask) {
        float4 color;
        float3 p = vector[i];

        /* Projection. */
        if (projection_ == SHD_PROJ_TUBE) {
          p = point_texco_remap_square(p);
          p = point_map_to_tube(p);
        }
        else if (projection_ == SHD_PROJ_SPHERE) {
          p = point_texco_remap_square(p);
          p = point_map_to_sphere(p);
        }
        else if (projection_ == SHD_PROJ_BOX) {
          p = point_map_to_box(p);  // no blending or normal data
        }

        /* Sample image texture. */
        if (use_cubic) {
          color = image_cubic_texture_lookup(ibuf_, p.x, p.y, extension_);
        }
        else if (use_linear) {
          color = image_linear_texture_lookup(ibuf_, p.x, p.y, extension_);
        }
        else {
          color = image_closest_texture_lookup(ibuf_, p.x, p.y, extension_);
        }

        /* Handle alpha. */
        if (alpha_clear_) {
          /* Don't let alpha affect color output. */
          color.w = 1.0f;
        }
        else {
          /* Output premultiplied alpha depending on alpha socket usage. This makes
           * it so that if we blend the color with a transparent shader using alpha as
           * a factor, we don't multiply alpha into the color twice. And if we do
           * not, then there will be no artifacts from zero alpha areas. */
          if (alpha_mode_ == IMA_ALPHA_PREMUL) {
            if (output_alpha) {
              premul_to_straight_v4_v4(color, color);
            }
            else {
              color.w = 1.0f;
            }
          }
          else {
            if (output_alpha) {
              color.w = 1.0f;
            }
            else {
              straight_to_premul_v4_v4(color, color);
            }
          }
        }

        if (output_color) {
          r_color[i] = (ColorGeometry4f)color;
        }

        if (output_alpha) {
          r_alpha[i] = color.w;
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
      bool is_tiled = ima->source == IMA_SRC_TILED;
      bool alpha_clear = ELEM(ima->alpha_mode, IMA_ALPHA_IGNORE, IMA_ALPHA_CHANNEL_PACKED) ||
                         IMB_colormanagement_space_name_is_data(ima->colorspace_settings.name);

      builder.construct_and_set_matching_fn<ImageFunction>(tex->interpolation,
                                                           tex->projection,
                                                           tex->projection_blend,
                                                           tex->extension,
                                                           alpha_clear,
                                                           ima->alpha_mode,
                                                           is_tiled,
                                                           ibuf);
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
