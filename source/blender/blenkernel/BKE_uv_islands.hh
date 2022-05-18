
/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_math_vec_types.hh"
#include "BLI_vector.hh"

#include "DNA_meshdata_types.h"

namespace blender::bke::uv_islands {
// TODO: primitives can be added twice
// TODO: Joining uv island should check where the borders could be merged.
// TODO: this isn't optimized for performance.

struct UVIslandEdge {
  float2 uv1;
  float2 uv2;

  UVIslandEdge() : uv1(float2(0.0f, 0.0f)), uv2(float2(0.0f, 0.0f))
  {
  }

  UVIslandEdge(const float2 &uv1, const float2 &uv2) : uv1(uv1), uv2(uv2)
  {
  }

  bool operator==(const UVIslandEdge &other) const
  {
    return (uv1 == other.uv1 && uv2 == other.uv2) || (uv1 == other.uv2 && uv2 == other.uv1);
  }

  void print() const
  {
    printf("UVIslandEdge(float2(%f, %f), float2(%f, %f))\n", uv1.x, uv1.y, uv2.x, uv2.y);
  }
};

struct Primitive {
  uint64_t index;
  UVIslandEdge edge[3];

  Primitive()
  {
  }

  Primitive(uint64_t index,
            const UVIslandEdge &edge1,
            const UVIslandEdge &edge2,
            const UVIslandEdge &edge3)
      : index(index), edge({edge1, edge2, edge3})
  {
  }

  Primitive(uint64_t index, const MLoopTri &tri, const MLoopUV *mloopuv) : index(index)
  {
    const float2 uv1(mloopuv[tri.tri[0]].uv);
    const float2 uv2(mloopuv[tri.tri[1]].uv);
    const float2 uv3(mloopuv[tri.tri[2]].uv);
    edge[0] = UVIslandEdge(uv1, uv2);
    edge[1] = UVIslandEdge(uv2, uv3);
    edge[2] = UVIslandEdge(uv3, uv1);
  }

  void print() const
  {
    printf(">>>> Primitive(start)\n");
    for (int i = 0; i < 3; i++) {
      edge[i].print();
    }
    printf("<<<< Primitive(end)\n");
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
  // We might want to use a linked list as there are more edits then reads.
  Vector<UVIslandEdge> borders;
  Vector<UVIslandPrimitive> primitives;

 public:
  void print() const
  {
    printf(">>>> UVIsland(start)\n");
    for (int i = 0; i < borders.size(); i++) {
      const UVIslandEdge &border = borders[i];
      printf("island.add(");
      border.print();
      printf("); // %d\n", i);
    }
    printf("<<<< UVIsland(end)\n");
  }
  /* Join a given UVIsland into self by using the given tri as the edges that needs to be merged.
   */
  void join(const UVIsland &other, const Primitive &primitive)
  {
    printf("Before joining");
    print();
    other.print();
    primitive.print();

    int64_t a_edge_index[3];
    int64_t b_edge_index[3];
    for (int i = 0; i < 3; i++) {
      a_edge_index[i] = borders.first_index_of_try(primitive.edge[i]);
      b_edge_index[i] = other.borders.first_index_of_try(primitive.edge[i]);
    }

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
      printf("1-1 join\n");
      BLI_assert_unreachable();
    }
    if (a_border_len == 1 && b_border_len == 2) {
      printf("1-2 join\n");
      BLI_assert_unreachable();
    }
    if (a_border_len == 2 && b_border_len == 1) {
      printf("2-1 join\n");
      BLI_assert_unreachable();
    }
    if (a_border_len == 2 && b_border_len == 2) {
      printf("2-2 join\n");
      int common_edge_len = 0;
      for (int i = 0; i < 3; i++) {
        if (a_edge_index[i] != -1 && b_edge_index[i] != -1) {
          common_edge_len++;
        }
      }

      Vector<uint64_t> edges_to_remove_from_dst;
      uint64_t insert;
      uint64_t start;
      uint64_t end;

      switch (common_edge_len) {
        case 0:
          BLI_assert_unreachable();
          break;

        case 1: {
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
          end = b_edge_index[common_edge];
          start = b_edge_index[other_b_edge];

          int other_a_edge = a_edge_index[next_edge] != -1 ? next_edge : prev_edge;
          insert = a_edge_index[common_edge];
          if (other_a_edge == common_edge - 1) {
            edges_to_remove_from_dst.append(insert);
            insert--;
          }
          break;
        }

        case 2: {
          // find edge that isn't common.
          int unshared_edge = -1;
          for (int i = 0; i < 3; i++) {
            if (a_edge_index[i] == -1 || b_edge_index[i] == -1) {
              unshared_edge = i;
            }
          }
          int next_edge = (unshared_edge + 1) % 3;
          int prev_edge = 3 - unshared_edge - next_edge;
          edges_to_remove_from_dst.append(a_edge_index[next_edge]);
          edges_to_remove_from_dst.append(a_edge_index[prev_edge]);
          insert = prev_edge;

          break;
        }
      }

      // TODO: It could be that different edge should be removed and copies start at other
      // locations depending on next_edge/prev_edge selection.
      // TODO: sort_reversed?
      for (uint64_t index_to_remove : edges_to_remove_from_dst) {
        borders.remove(index_to_remove);
        printf("removed %d: ", index_to_remove);
        print();
      }

      printf("i:%d s:%d e:%d\n", insert, start, end);

      if (end < start) {
        // TODO: these loops can be done with a single call and an iterator. For debugging and need
        // to look how to use the iterators this hasn't been done.
        for (int i = end - 1; i >= 0; i--) {
          borders.insert(insert, other.borders[i]);
          printf("insert: %d->%d", i, insert);
          print();
          BLI_assert(borders[insert].uv2 == borders[insert + 1].uv1);
        }
        for (int i = other.borders.size() - 1; i > start; i--) {
          borders.insert(insert, other.borders[i]);
          printf("insert: %d->%d", i, insert);
          print();
          BLI_assert(borders[insert].uv2 == borders[insert + 1].uv1);
        }
      }
      else {
        for (int i = end; i >= start; i--) {
          borders.insert(insert, other.borders[i]);
          printf("insert: %d->%d", i, insert);
          print();
          BLI_assert(borders[insert].uv2 == borders[insert + 1].uv1);
        }
      }
    }

    printf("After joining");
    print();

    BLI_assert(validate());
  }

  void add(const UVIslandEdge &border)
  {
    borders.append(border);
  }

  void extend_border(const int64_t edge_to_remove,
                     const UVIslandEdge &border1,
                     const UVIslandEdge &border2)
  {
    BLI_assert_msg(border1.uv2 == border2.uv1,
                   "Winding order of replacement borders is not correct.");
    borders[edge_to_remove] = border2;
    borders.insert(edge_to_remove, border1);
    BLI_assert(validate());
  }

  void extend_border(const int64_t edge1_to_remove,
                     const int64_t edge2_to_remove,
                     const UVIslandEdge &border)
  {
    borders[edge1_to_remove] = border;
    borders.remove(edge2_to_remove);
    BLI_assert(validate());
  }

  /** Try to extend the border of the uv island by adding the given tri. Returns false when the
   * border couldn't be extended. This happens when there is no common edge in uv space. */
  bool extend_border(const Primitive &primitive)
  {
    const int64_t edge1_index = borders.first_index_of_try(primitive.edge[0]);
    const int64_t edge2_index = borders.first_index_of_try(primitive.edge[1]);
    const int64_t edge3_index = borders.first_index_of_try(primitive.edge[2]);
    const bool has_edge1 = edge1_index != -1;
    const bool has_edge2 = edge2_index != -1;
    const bool has_edge3 = edge3_index != -1;

    if (has_edge1 == false && has_edge2 == false && has_edge3 == false) {
      /* Cannot extend as there is no common edge with a border. */
      return false;
    }
    if (has_edge1 == false && has_edge2 == false && has_edge3 == true) {
      extend_border(edge3_index, primitive.edge[0], primitive.edge[1]);
      return true;
    }
    if (has_edge1 == false && has_edge2 == true && has_edge3 == false) {
      extend_border(edge2_index, primitive.edge[2], primitive.edge[0]);
      return true;
    }
    if (has_edge1 == false && has_edge2 == true && has_edge3 == true) {
      extend_border(edge2_index, edge3_index, primitive.edge[0]);
      return true;
    }
    if (has_edge1 == true && has_edge2 == false && has_edge3 == false) {
      extend_border(edge1_index, primitive.edge[1], primitive.edge[2]);
      return true;
    }
    if (has_edge1 == true && has_edge2 == false && has_edge3 == true) {
      extend_border(edge3_index, edge1_index, primitive.edge[1]);
      return true;
    }
    if (has_edge1 == true && has_edge2 == true && has_edge3 == false) {
      extend_border(edge1_index, edge2_index, primitive.edge[2]);
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

  void add(const Primitive &primitive)
  {
    for (int i = 0; i < 3; i++) {
      borders.append(primitive.edge[i]);
    }
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
  void add(const Primitive &primitive)
  {
    Vector<uint64_t> extended_islands;
    for (uint64_t index = 0; index < islands.size(); index++) {
      UVIsland &island = islands[index];
      if (island.extend_border(primitive)) {
        extended_islands.append(index);
      }
    }

    if (extended_islands.size() > 0) {
      /* `extended_islands` can hold upto 3 islands that are connected with the given tri.
       * they can be joined to a single island, using the first as its target. */
      for (uint64_t index = 1; index < extended_islands.size(); index++) {
        islands[extended_islands[0]].join(islands[extended_islands[index]], primitive);
      }

      /* remove the islands that have been joined, starting at the end. */
      for (uint64_t index = extended_islands.size() - 1; index > 0; index--) {
        islands.remove_and_reorder(index);
      }
      islands[extended_islands[0]].add_prim(primitive.index);

      return;
    }

    /* if the tri has not been added we can create a new island. */
    UVIsland island;
    island.add(primitive);
    island.add_prim(primitive.index);
    islands.append(island);
  }
};

}  // namespace blender::bke::uv_islands