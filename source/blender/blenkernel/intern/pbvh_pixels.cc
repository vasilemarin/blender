/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

#include "BKE_customdata.h"
#include "BKE_mesh_mapping.h"
#include "BKE_pbvh.h"
#include "BKE_pbvh_pixels.hh"

#include "DNA_image_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "BLI_math.h"
#include "BLI_task.h"

#include "BKE_image_wrappers.hh"

#include "bmesh.h"

#include "pbvh_intern.h"

namespace blender::bke::pbvh::pixels {

/**
 * During debugging this check could be enabled.
 * It will write to each image pixel that is covered by the PBVH.
 */
constexpr bool USE_WATERTIGHT_CHECK = true;

/* -------------------------------------------------------------------- */

/** \name UV Islands
 * \{ */

// TODO: primitives can be added twice
// TODO: Joining uv island should check where the borders could be merged.
// TODO: this isn't optimized for performance.
// TODO: should consider putting the primitive data into a struct as it is reconstructed in
// multiple places.

struct UVIslandEdge {
  float2 uv1;
  float2 uv2;

  UVIslandEdge(const float2 &uv1, const float2 &uv2) : uv1(uv1), uv2(uv2)
  {
  }

  bool operator==(const UVIslandEdge &other) const
  {
    return (uv1 == other.uv1 && uv2 == other.uv2) || (uv1 == other.uv2 && uv2 == other.uv1);
  }
};

/* Mapping between generated primitives and original primitives. */
struct UVIslandPrimitive {
  uint64_t orig_prim;

  UVIslandPrimitive(uint64_t orig_prim) : orig_prim(orig_prim)
  {
  }
};

class UVIsland {
  Vector<UVIslandEdge> borders;
  Vector<UVIslandPrimitive> primitives;

 public:
  void print() const
  {
    printf(">>>> UVIsland(start)\n");
    for (int i = 0; i < borders.size(); i++) {
      const UVIslandEdge &border = borders[i];
      printf(
          " . %d: (%f,%f)-(%f,%f)\n", i, border.uv1.x, border.uv1.y, border.uv2.x, border.uv2.y);
    }
    printf("<<<< UVIsland(end)\n");
  }
  /* Join a given UVIsland into self by using the given tri as the edges that needs to be merged.
   */
  void join(UVIsland &other, const MLoopTri &tri, const MLoopUV *mloopuv)
  {
    printf("Before joining");
    print();
    other.print();
    const float2 uv1(mloopuv[tri.tri[0]].uv);
    const float2 uv2(mloopuv[tri.tri[1]].uv);
    const float2 uv3(mloopuv[tri.tri[2]].uv);
    UVIslandEdge edge1(uv1, uv2);
    UVIslandEdge edge2(uv2, uv3);
    UVIslandEdge edge3(uv3, uv1);
    const int64_t a_edge_index[3] = {borders.first_index_of_try(edge1),
                                     borders.first_index_of_try(edge2),
                                     borders.first_index_of_try(edge3)};
    const int64_t b_edge_index[3] = {other.borders.first_index_of_try(edge1),
                                     other.borders.first_index_of_try(edge2),
                                     other.borders.first_index_of_try(edge3)};

    // CHeck the number of edges. Based on this a different way should be used for joining.
    // these are the cases:
    // * self contains a single edge, other contains a single edge.
    // * self contains a single edge, other contains a double edge.
    // * self contains a double edge, other contains a single edge.
    // * self contains a double edge, other contains a double edge.
    int a_border_len = 0;
    int b_border_len = 0;
    for (int i = 0; i < 3; i++) {
      if (a_edge_index[i] != -1) {
        a_border_len += 1;
      }
      if (b_edge_index[i] != -1) {
        b_border_len += 1;
      }
    }
    BLI_assert_msg(a_border_len == 1 || a_border_len == 2, "Incorrect number of borders.");
    BLI_assert_msg(b_border_len == 1 || b_border_len == 2, "Incorrect number of borders.");

    if (a_border_len == 1 && b_border_len == 1) {
      BLI_assert_unreachable();
    }
    if (a_border_len == 1 && b_border_len == 2) {
      BLI_assert_unreachable();
    }
    if (a_border_len == 2 && b_border_len == 1) {
      BLI_assert_unreachable();
    }
    if (a_border_len == 2 && b_border_len == 2) {
      printf("2-2 join\n");
      /* Determine the common edge. */
      int common_edge = -1;
      for (int i = 0; i < 3; i++) {
        if (a_edge_index[i] != -1 && b_edge_index[i] != -1) {
          BLI_assert(common_edge == -1);
          common_edge = i;
        }
      }

      int next_edge = (common_edge + 1) % 3;
      int prev_edge = 3 - common_edge - next_edge;
      BLI_assert(common_edge != -1);
      int other_b_edge = b_edge_index[next_edge] != -1 ? next_edge : prev_edge;

      // In this case there should be a single common edge. This edge will still be an edge
      // in the merged island. find the index where to insert the other. find the start and
      // end to the other to insert.
      uint64_t end = b_edge_index[common_edge];
      uint64_t start = b_edge_index[other_b_edge];

      int other_a_edge = a_edge_index[next_edge] != -1 ? next_edge : prev_edge;
      uint64_t insert = a_edge_index[common_edge];
      if (other_a_edge == common_edge - 1) {
        borders.remove(insert - 1);
        insert -= 1;
        printf("removed %d: ", insert);
        print();
      }
      if (end < start) {
        for (int i = end - 1; i >= 0; i--) {
          borders.insert(insert, other.borders[i]);
          printf("insert: %d->%d", i, insert);
          print();
        }
        for (int i = other.borders.size() - 1; i > start; i--) {
          borders.insert(insert, other.borders[i]);
          printf("insert: %d->%d", i, insert);
          print();
        }
      }
      else {
        for (int i = end; i >= start; i--) {
          borders.insert(insert, other.borders[i]);
          printf("insert: %d->%d", i, insert);
          print();
        }
      }
    }

    printf("After joining");
    print();

    BLI_assert(validate());
  }

  void extend_border(const int64_t edge_to_remove, UVIslandEdge &border1, UVIslandEdge &border2)
  {
    BLI_assert_msg(border1.uv2 == border2.uv1,
                   "Winding order of replacement borders is not correct.");
    borders[edge_to_remove] = border2;
    borders.insert(edge_to_remove, border1);
    BLI_assert(validate());
  }

  void extend_border(const int64_t edge1_to_remove,
                     const int64_t edge2_to_remove,
                     UVIslandEdge &border)
  {
    borders[edge1_to_remove] = border;
    borders.remove(edge2_to_remove);
    BLI_assert(validate());
  }

  /** Try to extend the border of the uv island by adding the given tri. Returns false when the
   * border couldn't be extended. This happens when there is no common edge in uv space. */
  bool extend_border(const MLoopTri &tri, const MLoopUV *mloopuv)
  {
    const float2 uv1(mloopuv[tri.tri[0]].uv);
    const float2 uv2(mloopuv[tri.tri[1]].uv);
    const float2 uv3(mloopuv[tri.tri[2]].uv);
    UVIslandEdge edge1(uv1, uv2);
    UVIslandEdge edge2(uv2, uv3);
    UVIslandEdge edge3(uv3, uv1);
    const int64_t edge1_index = borders.first_index_of_try(edge1);
    const int64_t edge2_index = borders.first_index_of_try(edge2);
    const int64_t edge3_index = borders.first_index_of_try(edge3);
    const bool has_edge1 = edge1_index != -1;
    const bool has_edge2 = edge2_index != -1;
    const bool has_edge3 = edge3_index != -1;

    if (has_edge1 == false && has_edge2 == false && has_edge3 == false) {
      /* Cannot extend as there is no common edge with a border. */
      return false;
    }
    if (has_edge1 == false && has_edge2 == false && has_edge3 == true) {
      extend_border(edge3_index, edge1, edge2);
      return true;
    }
    if (has_edge1 == false && has_edge2 == true && has_edge3 == false) {
      extend_border(edge2_index, edge3, edge1);
      return true;
    }
    if (has_edge1 == false && has_edge2 == true && has_edge3 == true) {
      extend_border(edge2_index, edge3_index, edge1);
      return true;
    }
    if (has_edge1 == true && has_edge2 == false && has_edge3 == false) {
      extend_border(edge1_index, edge2, edge3);
      return true;
    }
    if (has_edge1 == true && has_edge2 == false && has_edge3 == true) {
      extend_border(edge3_index, edge1_index, edge2);
      return true;
    }
    if (has_edge1 == true && has_edge2 == true && has_edge3 == false) {
      extend_border(edge1_index, edge2_index, edge3);
      return true;
    }
    if (has_edge1 == true && has_edge2 == true && has_edge3 == true) {
      /* Nothing to do as it overlaps completely. */
      return true;
    }
    return false;
  }

  void add_prim(const uint64_t prim_index)
  {
    primitives.append(UVIslandPrimitive(prim_index));
  }

  void add(const MLoopTri &tri, const MLoopUV *mloopuv)
  {
    const float2 uv1(mloopuv[tri.tri[0]].uv);
    const float2 uv2(mloopuv[tri.tri[1]].uv);
    const float2 uv3(mloopuv[tri.tri[2]].uv);
    UVIslandEdge edge1(uv1, uv2);
    UVIslandEdge edge2(uv2, uv3);
    UVIslandEdge edge3(uv3, uv1);
    borders.append(edge1);
    borders.append(edge2);
    borders.append(edge3);
    BLI_assert(validate());
  }

  const bool validate() const
  {
    if (borders.size() == 0) {
      return true;
    }
    if (borders.size() == 1 || borders.size() == 2) {
      BLI_assert_msg(false, "Island with 1 or 2 border edges aren't allowed.");
      return false;
    }

    for (int index = 1; index < borders.size(); index++) {
      const UVIslandEdge &prev = borders[index - 1];
      const UVIslandEdge &curr = borders[index];
      BLI_assert_msg(prev.uv2 == curr.uv1, "Edges do not form a single border.");
    }
    BLI_assert_msg(borders.last().uv2 == borders[0].uv1, "Start and end are not connected.");

    return true;
  }
};

class UVIslands {
  Vector<UVIsland> islands;

 public:
  void add(const uint64_t prim_index, const MLoopTri &tri, const MLoopUV *mloopuv)
  {
    Vector<uint64_t> extended_islands;
    for (uint64_t index = 0; index < islands.size(); index++) {
      UVIsland &island = islands[index];
      if (island.extend_border(tri, mloopuv)) {
        extended_islands.append(index);
      }
    }

    if (extended_islands.size() > 0) {
      /* `extended_islands` can hold upto 3 islands that are connected with the given tri.
       * they can be joined to a single island, using the first as its target. */
      for (uint64_t index = 1; index < extended_islands.size(); index++) {
        islands[extended_islands[0]].join(islands[extended_islands[index]], tri, mloopuv);
      }

      /* remove the islands that have been joined, starting at the end. */
      for (uint64_t index = extended_islands.size() - 1; index > 0; index--) {
        islands.remove_and_reorder(index);
      }
      islands[extended_islands[0]].add_prim(prim_index);

      return;
    }

    /* if the tri has not been added we can create a new island. */
    UVIsland island;
    island.add(tri, mloopuv);
    island.add_prim(prim_index);
    islands.append(island);
  }
};

/** Build UV islands from PBVH primitives. */
UVIslands build_uv_islands(const PBVH &pbvh, const MLoopUV *mloopuv)
{
  UVIslands islands;
  for (int prim = 0; prim < pbvh.totprim; prim++) {
    const MLoopTri &tri = pbvh.looptri[prim];
    islands.add(prim, tri, mloopuv);
  }

  return islands;
}

/** \} */

/**
 * Calculate the delta of two neighbor UV coordinates in the given image buffer.
 */
static float2 calc_barycentric_delta(const float2 uvs[3],
                                     const float2 start_uv,
                                     const float2 end_uv)
{

  float3 start_barycentric;
  barycentric_weights_v2(uvs[0], uvs[1], uvs[2], start_uv, start_barycentric);
  float3 end_barycentric;
  barycentric_weights_v2(uvs[0], uvs[1], uvs[2], end_uv, end_barycentric);
  float3 barycentric = end_barycentric - start_barycentric;
  return float2(barycentric.x, barycentric.y);
}

static float2 calc_barycentric_delta_x(const ImBuf *image_buffer,
                                       const float2 uvs[3],
                                       const int x,
                                       const int y)
{
  const float2 start_uv(float(x) / image_buffer->x, float(y) / image_buffer->y);
  const float2 end_uv(float(x + 1) / image_buffer->x, float(y) / image_buffer->y);
  return calc_barycentric_delta(uvs, start_uv, end_uv);
}

static void extract_barycentric_pixels(UDIMTilePixels &tile_data,
                                       const ImBuf *image_buffer,
                                       const int triangle_index,
                                       const float2 uvs[3],
                                       const int minx,
                                       const int miny,
                                       const int maxx,
                                       const int maxy)
{
  for (int y = miny; y < maxy; y++) {
    bool start_detected = false;
    PackedPixelRow pixel_row;
    pixel_row.triangle_index = triangle_index;
    pixel_row.num_pixels = 0;
    int x;

    for (x = minx; x < maxx; x++) {
      float2 uv((float(x) + 0.5f) / image_buffer->x, (float(y) + 0.5f) / image_buffer->y);
      float3 barycentric_weights;
      barycentric_weights_v2(uvs[0], uvs[1], uvs[2], uv, barycentric_weights);

      const bool is_inside = barycentric_inside_triangle_v2(barycentric_weights);
      if (!start_detected && is_inside) {
        start_detected = true;
        pixel_row.start_image_coordinate = ushort2(x, y);
        pixel_row.start_barycentric_coord = float2(barycentric_weights.x, barycentric_weights.y);
      }
      else if (start_detected && !is_inside) {
        break;
      }
    }

    if (!start_detected) {
      continue;
    }
    pixel_row.num_pixels = x - pixel_row.start_image_coordinate.x;
    tile_data.pixel_rows.append(pixel_row);
  }
}

static void init_triangles(PBVH *pbvh, PBVHNode *node, NodeData *node_data, const MLoop *mloop)
{
  for (int i = 0; i < node->totprim; i++) {
    const MLoopTri *lt = &pbvh->looptri[node->prim_indices[i]];
    node_data->triangles.append(
        int3(mloop[lt->tri[0]].v, mloop[lt->tri[1]].v, mloop[lt->tri[2]].v));
  }
}

struct EncodePixelsUserData {
  Image *image;
  ImageUser *image_user;
  PBVH *pbvh;
  Vector<PBVHNode *> *nodes;
  const MLoopUV *ldata_uv;
};

static void do_encode_pixels(void *__restrict userdata,
                             const int n,
                             const TaskParallelTLS *__restrict UNUSED(tls))
{
  EncodePixelsUserData *data = static_cast<EncodePixelsUserData *>(userdata);
  Image *image = data->image;
  ImageUser image_user = *data->image_user;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = (*data->nodes)[n];
  NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
  LISTBASE_FOREACH (ImageTile *, tile, &data->image->tiles) {
    image::ImageTileWrapper image_tile(tile);
    image_user.tile = image_tile.get_tile_number();
    ImBuf *image_buffer = BKE_image_acquire_ibuf(image, &image_user, nullptr);
    if (image_buffer == nullptr) {
      continue;
    }

    float2 tile_offset = float2(image_tile.get_tile_offset());
    UDIMTilePixels tile_data;

    Triangles &triangles = node_data->triangles;
    for (int triangle_index = 0; triangle_index < triangles.size(); triangle_index++) {
      const MLoopTri *lt = &pbvh->looptri[node->prim_indices[triangle_index]];
      float2 uvs[3] = {
          float2(data->ldata_uv[lt->tri[0]].uv) - tile_offset,
          float2(data->ldata_uv[lt->tri[1]].uv) - tile_offset,
          float2(data->ldata_uv[lt->tri[2]].uv) - tile_offset,
      };

      const float minv = clamp_f(min_fff(uvs[0].y, uvs[1].y, uvs[2].y), 0.0f, 1.0f);
      const int miny = floor(minv * image_buffer->y);
      const float maxv = clamp_f(max_fff(uvs[0].y, uvs[1].y, uvs[2].y), 0.0f, 1.0f);
      const int maxy = min_ii(ceil(maxv * image_buffer->y), image_buffer->y);
      const float minu = clamp_f(min_fff(uvs[0].x, uvs[1].x, uvs[2].x), 0.0f, 1.0f);
      const int minx = floor(minu * image_buffer->x);
      const float maxu = clamp_f(max_fff(uvs[0].x, uvs[1].x, uvs[2].x), 0.0f, 1.0f);
      const int maxx = min_ii(ceil(maxu * image_buffer->x), image_buffer->x);

      TrianglePaintInput &triangle = triangles.get_paint_input(triangle_index);
      triangle.delta_barycentric_coord_u = calc_barycentric_delta_x(image_buffer, uvs, minx, miny);
      extract_barycentric_pixels(
          tile_data, image_buffer, triangle_index, uvs, minx, miny, maxx, maxy);
    }

    BKE_image_release_ibuf(image, image_buffer, nullptr);

    if (tile_data.pixel_rows.is_empty()) {
      continue;
    }

    tile_data.tile_number = image_tile.get_tile_number();
    node_data->tiles.append(tile_data);
  }
}

static bool should_pixels_be_updated(PBVHNode *node)
{
  if ((node->flag & PBVH_Leaf) == 0) {
    return false;
  }
  if ((node->flag & PBVH_RebuildPixels) != 0) {
    return true;
  }
  NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
  if (node_data != nullptr) {
    return false;
  }
  return true;
}

static int64_t count_nodes_to_update(PBVH *pbvh)
{
  int64_t result = 0;
  for (int n = 0; n < pbvh->totnode; n++) {
    PBVHNode *node = &pbvh->nodes[n];
    if (should_pixels_be_updated(node)) {
      result++;
    }
  }
  return result;
}

/**
 * Find the nodes that needs to be updated.
 *
 * The nodes that require updated are added to the r_nodes_to_update parameter.
 * Will fill in r_visited_polygons with polygons that are owned by nodes that do not require
 * updates.
 *
 * returns if there were any nodes found (true).
 */
static bool find_nodes_to_update(PBVH *pbvh, Vector<PBVHNode *> &r_nodes_to_update)
{
  int64_t nodes_to_update_len = count_nodes_to_update(pbvh);
  if (nodes_to_update_len == 0) {
    return false;
  }

  r_nodes_to_update.reserve(nodes_to_update_len);

  for (int n = 0; n < pbvh->totnode; n++) {
    PBVHNode *node = &pbvh->nodes[n];
    if (!should_pixels_be_updated(node)) {
      continue;
    }
    r_nodes_to_update.append(node);
    node->flag = static_cast<PBVHNodeFlags>(node->flag | PBVH_RebuildPixels);

    if (node->pixels.node_data == nullptr) {
      NodeData *node_data = MEM_new<NodeData>(__func__);
      node->pixels.node_data = node_data;
    }
    else {
      NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
      node_data->clear_data();
    }
  }

  return true;
}

static void apply_watertight_check(PBVH *pbvh, Image *image, ImageUser *image_user)
{
  ImageUser watertight = *image_user;
  LISTBASE_FOREACH (ImageTile *, tile_data, &image->tiles) {
    image::ImageTileWrapper image_tile(tile_data);
    watertight.tile = image_tile.get_tile_number();
    ImBuf *image_buffer = BKE_image_acquire_ibuf(image, &watertight, nullptr);
    if (image_buffer == nullptr) {
      continue;
    }
    for (int n = 0; n < pbvh->totnode; n++) {
      PBVHNode *node = &pbvh->nodes[n];
      if ((node->flag & PBVH_Leaf) == 0) {
        continue;
      }
      NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
      UDIMTilePixels *tile_node_data = node_data->find_tile_data(image_tile);
      if (tile_node_data == nullptr) {
        continue;
      }

      for (PackedPixelRow &pixel_row : tile_node_data->pixel_rows) {
        int pixel_offset = pixel_row.start_image_coordinate.y * image_buffer->x +
                           pixel_row.start_image_coordinate.x;
        for (int x = 0; x < pixel_row.num_pixels; x++) {
          if (image_buffer->rect_float) {
            copy_v4_fl(&image_buffer->rect_float[pixel_offset * 4], 1.0);
          }
          if (image_buffer->rect) {
            uint8_t *dest = static_cast<uint8_t *>(
                static_cast<void *>(&image_buffer->rect[pixel_offset]));
            copy_v4_uchar(dest, 255);
          }
          pixel_offset += 1;
        }
      }
    }
    BKE_image_release_ibuf(image, image_buffer, nullptr);
  }
  BKE_image_partial_update_mark_full_update(image);
}

static void update_pixels(PBVH *pbvh, Mesh *mesh, Image *image, ImageUser *image_user)
{
  Vector<PBVHNode *> nodes_to_update;

  if (!find_nodes_to_update(pbvh, nodes_to_update)) {
    return;
  }

  const MLoopUV *ldata_uv = static_cast<const MLoopUV *>(
      CustomData_get_layer(&mesh->ldata, CD_MLOOPUV));
  if (ldata_uv == nullptr) {
    return;
  }

  for (PBVHNode *node : nodes_to_update) {
    NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
    init_triangles(pbvh, node, node_data, mesh->mloop);
  }

  UVIslands islands = build_uv_islands(*pbvh, ldata_uv);

  EncodePixelsUserData user_data;
  user_data.pbvh = pbvh;
  user_data.image = image;
  user_data.image_user = image_user;
  user_data.ldata_uv = ldata_uv;
  user_data.nodes = &nodes_to_update;

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, nodes_to_update.size());
  BLI_task_parallel_range(0, nodes_to_update.size(), &user_data, do_encode_pixels, &settings);
  if (USE_WATERTIGHT_CHECK) {
    apply_watertight_check(pbvh, image, image_user);
  }

  /* Rebuild the undo regions. */
  for (PBVHNode *node : nodes_to_update) {
    NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
    node_data->rebuild_undo_regions();
  }

  /* Clear the UpdatePixels flag. */
  for (PBVHNode *node : nodes_to_update) {
    node->flag = static_cast<PBVHNodeFlags>(node->flag & ~PBVH_RebuildPixels);
  }

//#define DO_PRINT_STATISTICS
#ifdef DO_PRINT_STATISTICS
  /* Print some statistics about compression ratio. */
  {
    int64_t compressed_data_len = 0;
    int64_t num_pixels = 0;
    for (int n = 0; n < pbvh->totnode; n++) {
      PBVHNode *node = &pbvh->nodes[n];
      if ((node->flag & PBVH_Leaf) == 0) {
        continue;
      }
      NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
      compressed_data_len += node_data->triangles.mem_size();
      for (const UDIMTilePixels &tile_data : node_data->tiles) {
        compressed_data_len += tile_data.encoded_pixels.size() * sizeof(PackedPixelRow);
        for (const PackedPixelRow &encoded_pixels : tile_data.encoded_pixels) {
          num_pixels += encoded_pixels.num_pixels;
        }
      }
    }
    printf("Encoded %lld pixels in %lld bytes (%f bytes per pixel)\n",
           num_pixels,
           compressed_data_len,
           float(compressed_data_len) / num_pixels);
  }
#endif
}

NodeData &BKE_pbvh_pixels_node_data_get(PBVHNode &node)
{
  BLI_assert(node.pixels.node_data != nullptr);
  NodeData *node_data = static_cast<NodeData *>(node.pixels.node_data);
  return *node_data;
}

void BKE_pbvh_pixels_mark_image_dirty(PBVHNode &node, Image &image, ImageUser &image_user)
{
  BLI_assert(node.pixels.node_data != nullptr);
  NodeData *node_data = static_cast<NodeData *>(node.pixels.node_data);
  if (node_data->flags.dirty) {
    ImageUser local_image_user = image_user;
    LISTBASE_FOREACH (ImageTile *, tile, &image.tiles) {
      image::ImageTileWrapper image_tile(tile);
      local_image_user.tile = image_tile.get_tile_number();
      ImBuf *image_buffer = BKE_image_acquire_ibuf(&image, &local_image_user, nullptr);
      if (image_buffer == nullptr) {
        continue;
      }

      node_data->mark_region(image, image_tile, *image_buffer);
      BKE_image_release_ibuf(&image, image_buffer, nullptr);
    }
    node_data->flags.dirty = false;
  }
}

}  // namespace blender::bke::pbvh::pixels

extern "C" {
using namespace blender::bke::pbvh::pixels;

void BKE_pbvh_build_pixels(PBVH *pbvh, Mesh *mesh, Image *image, ImageUser *image_user)
{
  update_pixels(pbvh, mesh, image, image_user);
}

void pbvh_pixels_free(PBVHNode *node)
{
  NodeData *node_data = static_cast<NodeData *>(node->pixels.node_data);
  MEM_delete(node_data);
  node->pixels.node_data = nullptr;
}
}
