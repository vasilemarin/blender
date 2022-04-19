/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

/* -------------------------------------------------------------------- */
/** \name Grease Pencil Overlay
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_point)
    .do_static_compilation(true)
    .define("USE_POINTS")
    .sampler(0, ImageType::FLOAT_1D, "weightTex")
    .push_constant(Type::FLOAT, "normalSize")
    .push_constant(Type::BOOL, "doMultiframe")
    .push_constant(Type::BOOL, "doStrokeEndpoints")
    .push_constant(Type::BOOL, "hideSelect")
    .push_constant(Type::BOOL, "doWeightColor")
    .push_constant(Type::FLOAT, "gpEditOpacity")
    .push_constant(Type::IVEC4, "gpEditColor")
    .vertex_in(0, Type::VEC3, "pos")
    .vertex_in(1, Type::INT, "ma")
    .vertex_in(2, Type::UINT, "vflag")
    .vertex_in(3, Type::FLOAT, "weight")
    .fragment_out(0, Type::VEC4, "finalColor")
    .vertex_source("edit_gpencil_vert.glsl")
    .fragment_source("gpu_shader_point_varying_color_frag.glsl")
    .additional_info("draw_gpencil");

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_wire)
    .do_static_compilation(true)
    .sampler(0, ImageType::FLOAT_1D, "weightTex")
    .push_constant(Type::FLOAT, "normalSize")
    .push_constant(Type::BOOL, "doMultiframe")
    .push_constant(Type::BOOL, "doStrokeEndpoints")
    .push_constant(Type::BOOL, "hideSelect")
    .push_constant(Type::BOOL, "doWeightColor")
    .push_constant(Type::FLOAT, "gpEditOpacity")
    .push_constant(Type::IVEC4, "gpEditColor")
    .vertex_in(0, Type::VEC3, "pos")
    .vertex_in(1, Type::INT, "ma")
    .vertex_in(2, Type::UINT, "vflag")
    .vertex_in(3, Type::FLOAT, "weight")
    .fragment_out(0, Type::VEC4, "finalColor")
    .vertex_source("edit_gpencil_vert.glsl")
    .fragment_source("gpu_shader_3D_smooth_color_frag.glsl")
    .additional_info("draw_gpencil");

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_guide_point)
    .do_static_compilation(true)
    .push_constant(Type::IVEC4, "pColor")
    .push_constant(Type::FLOAT, "pSize")
    .push_constant(Type::IVEC3, "pPosition")
    .fragment_out(0, Type::VEC4, "finalColor")
    .vertex_source("edit_gpencil_guide_vert.glsl")
    .fragment_source("gpu_shader_point_varying_color_frag.glsl")
    .additional_info("draw_gpencil");

/** \} */
