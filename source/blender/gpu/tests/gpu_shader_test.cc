/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include "GPU_capabilities.h"
#include "GPU_compute.h"
#include "GPU_shader.h"
#include "GPU_texture.h"

#include "MEM_guardedalloc.h"

#include "gpu_testing.hh"

namespace blender::gpu::tests {

TEST_F(GPUTest, gpu_shader_compute)
{

  if (!GPU_compute_shader_support()) {
    /* We can't test as a the platform does not support compute shaders. */
    std::cout << "Skipping compute shader test: platform not supported";
    return;
  }

  static constexpr int SIZE = 512;
  const char *compute_glsl = R"(

layout(local_size_x = 1, local_size_y = 1) in;
layout(rgba32f, binding = 0) uniform image1D img_output;

void main() {
  // base pixel colour for image
  vec4 pixel = vec4(1.0, 0.5, 0.2, 1.0);
  
  // output to a specific pixel in the image
  imageStore(img_output, int(gl_GlobalInvocationID.x), pixel);}

)";

  GPUShader *shader = GPU_shader_create_compute(
      compute_glsl, nullptr, nullptr, "gpu_shader_compute");
  EXPECT_NE(shader, nullptr);

  GPUTexture *texture = GPU_texture_create_2d(
      "gpu_shader_compute", SIZE, SIZE, 0, GPU_RGBA32F, nullptr);
  EXPECT_NE(texture, nullptr);

  GPU_shader_bind(shader);
  int binding = GPU_shader_get_texture_binding(shader, "img_output");
  GPU_texture_image_bind(texture, binding);
  GPU_compute_dispatch(SIZE, SIZE, 1);

  GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);

  float *data = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
  EXPECT_NE(data, nullptr);

  for (int index = 0; index < SIZE * SIZE * 4; index++) {
    printf("%d: %f\n", index, data[index]);
  }

  MEM_freeN(data);

  GPU_shader_unbind();
  GPU_texture_unbind(texture);
  GPU_texture_free(texture);

  GPU_shader_free(shader);
}

}  // namespace blender::gpu::tests
