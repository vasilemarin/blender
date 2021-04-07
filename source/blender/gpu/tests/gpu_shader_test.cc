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
  static constexpr int WIDTH = 512;
  static constexpr int HEIGHT = 512;

  if (!GPU_compute_shader_support()) {
    /* We can't test as a the platform does not support compute shaders. */
    std::cout << "Skipping compute shader test: platform not supported";
    return;
  }

  const char *compute_glsl = R"(

uniform float roll;
uniform writeonly image2D destTex;
layout (local_size_x = 16, local_size_y = 16) in;

void main() {
    ivec2 storePos = ivec2(gl_GlobalInvocationID.xy);
    float localCoef = length(vec2(ivec2(gl_LocalInvocationID.xy)-8)/8.0);
    float globalCoef = sin(float(gl_WorkGroupID.x+gl_WorkGroupID.y)*0.1 + roll)*0.5;
    imageStore(destTex, storePos, vec4(1.0-globalCoef*localCoef, 0.0, 0.0, 0.0));
}

)";

  GPUShader *shader = GPU_shader_create_compute(compute_glsl, nullptr, nullptr, __func__);
  EXPECT_NE(shader, nullptr);

  GPUTexture *texture = GPU_texture_create_2d(__func__, WIDTH, HEIGHT, 1, GPU_R32F, nullptr);
  int binding = GPU_shader_get_texture_binding(shader, "destTex");

  GPU_shader_bind(shader);
  GPU_shader_uniform_1f(shader, "roll", 1.0f);
  GPU_texture_bind(texture, binding);
  GPU_compute_dispatch(WIDTH / 16, HEIGHT / 16, 1);
  GPU_flush();
  GPU_finish();
  GPU_shader_unbind();

  float *data = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
  EXPECT_NE(data, nullptr);

  for (int index = 0; index < WIDTH * HEIGHT; index++) {
    printf("%d: %f\n", index, data[index]);
  }

  MEM_freeN(data);

  GPU_texture_unbind(texture);
  GPU_texture_free(texture);

  GPU_shader_free(shader);
}

}  // namespace blender::gpu::tests
