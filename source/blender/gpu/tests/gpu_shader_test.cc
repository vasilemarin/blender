/* Apache License, Version 2.0 */

#include "testing/testing.h"

#include "GPU_batch.h"
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

  static constexpr int SIZE = 2;

  /* Create a compute batch. */
  GPUBatch *batch = GPU_batch_compute_create();
  EXPECT_NE(batch, nullptr);

  /* Build the compute shader and attach to the compute batch. */
  const char *compute_glsl = R"(
layout(local_size_x = 1, local_size_y = 1) in;
layout(rgba32f, binding = 0) uniform image2D img_output;

void main() {
  vec4 pixel = vec4(1.0, 0.5, 0.2, 1.0);
  imageStore(img_output, ivec2(gl_GlobalInvocationID.xy), pixel);
}
)";

  GPUShader *shader = GPU_shader_create_compute(
      compute_glsl, nullptr, nullptr, "gpu_shader_compute");
  EXPECT_NE(shader, nullptr);
  GPU_batch_set_shader(batch, shader);

  /* Create result texture and bind to the shader. */
  GPUTexture *texture = GPU_texture_create_2d(
      "gpu_shader_compute", SIZE, SIZE, 0, GPU_RGBA32F, nullptr);
  EXPECT_NE(texture, nullptr);
  GPU_batch_texture_image_bind(batch, "img_output", texture);

  /* Dispatch the compute command. */
  GPU_batch_compute(batch, SIZE, SIZE, 1);

  /* Check if compute has been done. */
  float *data = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
  EXPECT_NE(data, nullptr);

  for (int index = 0; index < SIZE * SIZE; index++) {
    EXPECT_FLOAT_EQ(data[index * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(data[index * 4 + 1], 0.5f);
    EXPECT_FLOAT_EQ(data[index * 4 + 2], 0.2f);
    EXPECT_FLOAT_EQ(data[index * 4 + 3], 1.0f);
  }
  MEM_freeN(data);

  /* Clean up. */
  GPU_shader_unbind();
  GPU_shader_free(shader);
  GPU_texture_unbind(texture);
  GPU_texture_free(texture);
  GPU_batch_discard(batch);
}

}  // namespace blender::gpu::tests
