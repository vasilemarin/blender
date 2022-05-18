#include "testing/testing.h"

#include "BKE_uv_islands.hh"

namespace blender::bke::uv_islands::tests {

TEST(uv_island, join)
{
  Primitive primitive(0,
                      UVIslandEdge(float2(0.625000, 0.500000), float2(0.875000, 0.750000)),
                      UVIslandEdge(float2(0.875000, 0.750000), float2(0.625000, 0.750000)),
                      UVIslandEdge(float2(0.625000, 0.750000), float2(0.625000, 0.500000)));
  UVIsland island1;
  island1.add(UVIslandEdge(float2(0.375000, 0.000000), float2(0.625000, 0.000000)));  // 0
  island1.add(UVIslandEdge(float2(0.625000, 0.000000), float2(0.625000, 0.250000)));  // 1
  island1.add(UVIslandEdge(float2(0.625000, 0.250000), float2(0.625000, 0.500000)));  // 2
  island1.add(UVIslandEdge(float2(0.625000, 0.500000), float2(0.875000, 0.750000)));  // 3
  island1.add(UVIslandEdge(float2(0.875000, 0.750000), float2(0.625000, 0.750000)));  // 4
  island1.add(UVIslandEdge(float2(0.625000, 0.750000), float2(0.625000, 1.000000)));  // 5
  island1.add(UVIslandEdge(float2(0.625000, 1.000000), float2(0.375000, 1.000000)));  // 6
  island1.add(UVIslandEdge(float2(0.375000, 1.000000), float2(0.375000, 0.750000)));  // 7
  island1.add(UVIslandEdge(float2(0.375000, 0.750000), float2(0.125000, 0.750000)));  // 8
  island1.add(UVIslandEdge(float2(0.125000, 0.750000), float2(0.125000, 0.500000)));  // 9
  island1.add(UVIslandEdge(float2(0.125000, 0.500000), float2(0.375000, 0.500000)));  // 10
  island1.add(UVIslandEdge(float2(0.375000, 0.500000), float2(0.375000, 0.250000)));  // 11
  island1.add(UVIslandEdge(float2(0.375000, 0.250000), float2(0.375000, 0.000000)));  // 12

  UVIsland island2;
  island2.add(UVIslandEdge(float2(0.625000, 0.500000), float2(0.875000, 0.500000)));  // 0
  island2.add(UVIslandEdge(float2(0.875000, 0.500000), float2(0.875000, 0.750000)));  // 1
  island2.add(UVIslandEdge(float2(0.875000, 0.750000), float2(0.625000, 0.750000)));  // 2
  island2.add(UVIslandEdge(float2(0.625000, 0.750000), float2(0.625000, 0.500000)));  // 3

  island1.join(island2, primitive);
}

TEST(uv_islands, join_22_reversed_winding_order)
{
  Primitive primitive(0,
                      UVIslandEdge(float2(0.750000, 0.500000), float2(0.718750, 0.562500)),
                      UVIslandEdge(float2(0.718750, 0.562500), float2(0.718750, 0.500000)),
                      UVIslandEdge(float2(0.718750, 0.500000), float2(0.750000, 0.500000)));
  UVIsland island1;
  island1.add(UVIslandEdge(float2(0.750000, 0.437500), float2(0.750000, 0.500000)));
  island1.add(UVIslandEdge(float2(0.750000, 0.500000), float2(0.718750, 0.562500)));  // 0
  island1.add(UVIslandEdge(float2(0.718750, 0.562500), float2(0.718750, 0.500000)));  // 1
  island1.add(UVIslandEdge(float2(0.718750, 0.500000), float2(0.718750, 0.437500)));
  island1.add(UVIslandEdge(float2(0.718750, 0.437500), float2(0.718750, 0.375000)));
  island1.add(UVIslandEdge(float2(0.718750, 0.375000), float2(0.718750, 0.312500)));
  island1.add(UVIslandEdge(float2(0.718750, 0.312500), float2(0.750000, 0.312500)));
  island1.add(UVIslandEdge(float2(0.750000, 0.312500), float2(0.750000, 0.375000)));
  island1.add(UVIslandEdge(float2(0.750000, 0.375000), float2(0.750000, 0.437500)));

  UVIsland island2;
  island2.add(UVIslandEdge(float2(0.750000, 0.562500), float2(0.750000, 0.625000)));
  island2.add(UVIslandEdge(float2(0.750000, 0.625000), float2(0.718750, 0.625000)));
  island2.add(UVIslandEdge(float2(0.718750, 0.625000), float2(0.718750, 0.562500)));
  island2.add(UVIslandEdge(float2(0.718750, 0.562500), float2(0.718750, 0.500000)));  // 1r
  island2.add(UVIslandEdge(float2(0.718750, 0.500000), float2(0.750000, 0.500000)));  // 0r
  island2.add(UVIslandEdge(float2(0.750000, 0.500000), float2(0.750000, 0.562500)));

  island1.join(island2, primitive);

  /* expected
  UVIsland island1;
  island1.add(UVIslandEdge(float2(0.750000, 0.437500), float2(0.750000, 0.500000)));

  island2.add(UVIslandEdge(float2(0.750000, 0.500000), float2(0.750000, 0.562500)));
  island2.add(UVIslandEdge(float2(0.750000, 0.562500), float2(0.750000, 0.625000)));
  island2.add(UVIslandEdge(float2(0.750000, 0.625000), float2(0.718750, 0.625000)));
  island2.add(UVIslandEdge(float2(0.718750, 0.625000), float2(0.718750, 0.562500)));

  island1.add(UVIslandEdge(float2(0.718750, 0.562500), float2(0.718750, 0.500000)));  // 1
  island1.add(UVIslandEdge(float2(0.718750, 0.500000), float2(0.718750, 0.437500)));
  island1.add(UVIslandEdge(float2(0.718750, 0.437500), float2(0.718750, 0.375000)));
  island1.add(UVIslandEdge(float2(0.718750, 0.375000), float2(0.718750, 0.312500)));
  island1.add(UVIslandEdge(float2(0.718750, 0.312500), float2(0.750000, 0.312500)));
  island1.add(UVIslandEdge(float2(0.750000, 0.312500), float2(0.750000, 0.375000)));
  island1.add(UVIslandEdge(float2(0.750000, 0.375000), float2(0.750000, 0.437500)));

  */
}

}  // namespace blender::bke::uv_islands::tests
