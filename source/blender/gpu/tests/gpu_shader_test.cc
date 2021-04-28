/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include "GPU_capabilities.h"
#include "GPU_compute.h"
#include "GPU_shader.h"
#include "GPU_texture.h"
#include "GPU_vertex_buffer.h"
#include "GPU_vertex_format.h"

#include "MEM_guardedalloc.h"

#include "gpu_testing.hh"

#include "GPU_glew.h"

namespace blender::gpu::tests {

TEST_F(GPUTest, gpu_shader_compute_2d)
{

  if (!GPU_compute_shader_support()) {
    /* We can't test as a the platform does not support compute shaders. */
    std::cout << "Skipping compute shader test: platform not supported";
    return;
  }

  static constexpr uint SIZE = 512;

  /* Build compute shader. */
  const char *compute_glsl = R"(

layout(local_size_x = 1, local_size_y = 1) in;
layout(rgba32f, binding = 0) uniform image2D img_output;

void main() {
  // base pixel colour for image
  vec4 pixel = vec4(1.0, 0.5, 0.2, 1.0);
  
  // output to a specific pixel in the image
  imageStore(img_output, ivec2(gl_GlobalInvocationID.xy), pixel);
}

)";

  GPUShader *shader = GPU_shader_create_compute(
      compute_glsl, nullptr, nullptr, "gpu_shader_compute_2d");
  EXPECT_NE(shader, nullptr);

  /* Create texture to store result and attach to shader. */
  GPUTexture *texture = GPU_texture_create_2d(
      "gpu_shader_compute_2d", SIZE, SIZE, 0, GPU_RGBA32F, nullptr);
  EXPECT_NE(texture, nullptr);

  GPU_shader_bind(shader);
  GPU_texture_image_bind(texture, GPU_shader_get_texture_binding(shader, "img_output"));

  /* Dispatch compute task. */
  GPU_compute_dispatch(shader, SIZE, SIZE, 1);

  /* Check if compute has been done. */
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
  float *data = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
  EXPECT_NE(data, nullptr);
  for (int index = 0; index < SIZE * SIZE; index++) {
    EXPECT_FLOAT_EQ(data[index * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(data[index * 4 + 1], 0.5f);
    EXPECT_FLOAT_EQ(data[index * 4 + 2], 0.2f);
    EXPECT_FLOAT_EQ(data[index * 4 + 3], 1.0f);
  }
  MEM_freeN(data);

  /* Cleanup. */
  GPU_shader_unbind();
  GPU_texture_unbind(texture);
  GPU_texture_free(texture);
  GPU_shader_free(shader);
}

TEST_F(GPUTest, gpu_shader_compute_1d)
{

  if (!GPU_compute_shader_support()) {
    /* We can't test as a the platform does not support compute shaders. */
    std::cout << "Skipping compute shader test: platform not supported";
    return;
  }

  static constexpr uint SIZE = 10;

  /* Build compute shader. */
  const char *compute_glsl = R"(

layout(local_size_x = 1) in;

layout(rgba32f, binding = 1) uniform image1D outputVboData;

void main() {
  int index = int(gl_GlobalInvocationID.x);
  vec4 pos = vec4(gl_GlobalInvocationID.x);
  imageStore(outputVboData, index, pos);
}

)";

  GPUShader *shader = GPU_shader_create_compute(
      compute_glsl, nullptr, nullptr, "gpu_shader_compute_1d");
  EXPECT_NE(shader, nullptr);

  /* Construct Texture. */
  GPUTexture *texture = GPU_texture_create_1d("gpu_shader_compute_1d", SIZE, 0, GPU_RGBA32F, NULL);
  EXPECT_NE(texture, nullptr);

  GPU_shader_bind(shader);
  GPU_texture_image_bind(texture, GPU_shader_get_texture_binding(shader, "outputVboData"));

  /* Dispatch compute task. */
  GPU_compute_dispatch(shader, SIZE, 1, 1);

  /* Check if compute has been done. */
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);

  /* Create texture to load back result. */
  float *data = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
  EXPECT_NE(data, nullptr);
  for (int index = 0; index < SIZE; index++) {
    float expected_value = index;
    EXPECT_FLOAT_EQ(data[index * 4 + 0], expected_value);
    EXPECT_FLOAT_EQ(data[index * 4 + 1], expected_value);
    EXPECT_FLOAT_EQ(data[index * 4 + 2], expected_value);
    EXPECT_FLOAT_EQ(data[index * 4 + 3], expected_value);
  }
  MEM_freeN(data);

  /* Cleanup. */
  GPU_shader_unbind();
  GPU_texture_unbind(texture);
  GPU_texture_free(texture);
  GPU_shader_free(shader);
}

TEST_F(GPUTest, gpu_shader_compute_vbo)
{

  if (!GPU_compute_shader_support()) {
    /* We can't test as a the platform does not support compute shaders. */
    std::cout << "Skipping compute shader test: platform not supported";
    return;
  }

  static constexpr uint SIZE = 128;

  /* Build compute shader. */
  const char *compute_glsl = R"(

layout(local_size_x = 1) in;

layout(std430, binding = 0) buffer outputVboData
{
  vec4 Positions[];
};

void main() {
  int index = int(gl_GlobalInvocationID.x);
  vec4 pos = vec4(gl_GlobalInvocationID.x);
  Positions[index] = pos;
}

)";

  GPUShader *shader = GPU_shader_create_compute(
      compute_glsl, nullptr, nullptr, "gpu_shader_compute_vbo");
  EXPECT_NE(shader, nullptr);
  GPU_shader_bind(shader);

  /* Construct VBO. */
  static GPUVertFormat format = {0};
  GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);
  /* TODO: Should add a new GPU_USAGE_TYPE. (device only) that won't upload the data. */
  GPUVertBuf *vbo = GPU_vertbuf_create_with_format_ex(&format, GPU_USAGE_DYNAMIC);
  GPU_vertbuf_data_alloc(vbo, SIZE);
  /* Store vbo with known values for easier debugging. */
  float dummy[4] = {57005.0f, 57005.0f, 57005.0f, 57005.0f};
  for (int i = 0; i < SIZE; i++) {
    GPU_vertbuf_vert_set(vbo, i, dummy);
  }
  GPU_vertbuf_use(vbo);

  GPU_shader_attach_vertex_buffer(shader, vbo, 0);

  /* Dispatch compute task. */
  GPU_compute_dispatch(shader, SIZE, 1, 1);

  /* Check if compute has been done. */
  GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);

  /* Use opengl function to download the vertex buffer. */
  /* TODO(jbakker): Add function to copy it back to the VertexBuffer data. */
  float *data = static_cast<float *>(glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY));
  ASSERT_NE(data, nullptr);
  /* Create texture to load back result. */
  for (int index = 0; index < SIZE; index++) {
    float expected_value = index;
    EXPECT_FLOAT_EQ(data[index * 4 + 0], expected_value);
    EXPECT_FLOAT_EQ(data[index * 4 + 1], expected_value);
    EXPECT_FLOAT_EQ(data[index * 4 + 2], expected_value);
    EXPECT_FLOAT_EQ(data[index * 4 + 3], expected_value);
  }

  /* Cleanup. */
  GPU_shader_unbind();
  GPU_vertbuf_discard(vbo);
  GPU_shader_free(shader);
}

}  // namespace blender::gpu::tests
