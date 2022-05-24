
/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <fstream>

#include "BLI_math_vec_types.hh"
#include "BLI_vector.hh"

#include "DNA_meshdata_types.h"

namespace blender::bke::uv_islands {
// TODO: primitives can be added twice
// TODO: Joining uv island should check where the borders could be merged.
// TODO: this isn't optimized for performance.

//#define DEBUG_SVG

struct UVVertex {
  /* Loop index of the vertex in the original mesh. */
  uint64_t loop;
  /* Position in uv space. */
  float2 uv;
};

struct UVEdge {
  UVVertex vertices[2];
  int64_t adjacent_uv_primitive = -1;

  bool has_shared_edge(const UVEdge &other) const
  {
    return (vertices[0].uv == other.vertices[0].uv && vertices[1].uv == other.vertices[1].uv) ||
           (vertices[0].uv == other.vertices[1].uv && vertices[1].uv == other.vertices[0].uv);
  }

  bool is_border_edge() const
  {
    return adjacent_uv_primitive == -1;
  }
};

struct UVPrimitive {
  /**
   * Index of the primitive in the original mesh.
   */
  uint64_t index;
  UVEdge edges[3];

  explicit UVPrimitive(uint64_t prim_index, const MLoopTri &tri, const MLoopUV *mloopuv)
      : index(prim_index)
  {
    for (int i = 0; i < 3; i++) {
      edges[i].vertices[0].uv = mloopuv[tri.tri[i]].uv;
      edges[i].vertices[1].uv = mloopuv[tri.tri[(i + 1) % 3]].uv;
      edges[i].vertices[0].loop = tri.tri[i];
      edges[i].vertices[1].loop = tri.tri[(i + 1) % 3];
    }
  }

  Vector<std::pair<UVEdge &, UVEdge &>> shared_edges(UVPrimitive &other)
  {
    Vector<std::pair<UVEdge &, UVEdge &>> result;
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        if (edges[i].has_shared_edge(other.edges[j])) {
          result.append(std::pair<UVEdge &, UVEdge &>(edges[i], other.edges[j]));
        }
      }
    }
    return result;
  }

  bool has_shared_edge(const UVPrimitive &other) const
  {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        if (edges[i].has_shared_edge(other.edges[j])) {
          return true;
        }
      }
    }
    return false;
  }
};

struct UVBorderVert {
  float2 uv;
  explicit UVBorderVert(float2 &uv) : uv(uv)
  {
  }
};

struct UVBorderEdge {
  UVEdge *edge;
  bool tag = false;

  explicit UVBorderEdge(UVEdge *edge) : edge(edge)
  {
  }
};

struct UVIsland {
  Vector<UVPrimitive> primitives;
  Vector<UVBorderVert> border;

  UVIsland(const UVPrimitive &primitive)
  {
    append(primitive);
  }

  void extract_border()
  {
    Vector<UVBorderEdge> edges;

    for (UVPrimitive &primitive : primitives) {
      for (UVEdge &edge : primitive.edges) {
        if (edge.is_border_edge()) {
          edges.append(UVBorderEdge(&edge));
        }
      }
    }

    edges.first().tag = true;
    UVEdge *starting_edge = edges.first().edge;

    border.append(UVBorderVert(starting_edge->vertices[0].uv));
    border.append(UVBorderVert(starting_edge->vertices[1].uv));
    UVBorderVert &first = border.first();
    UVBorderVert &current = border.last();

    while (current.uv != first.uv) {
      for (UVBorderEdge &border_edge : edges) {
        if (border_edge.tag == true) {
          continue;
        }
        int i;
        for (i = 0; i < 2; i++) {
          if (border_edge.edge->vertices[i].uv == current.uv) {
            border.append(UVBorderVert(border_edge.edge->vertices[1 - i].uv));
            border_edge.tag = true;
            current = border.last();
            break;
          }
        }
        if (i != 2) {
          break;
        }
      }
    }
  }

 private:
  void append(const UVPrimitive &primitive)
  {
    primitives.append(primitive);
  }

 public:
  bool has_shared_edge(const UVPrimitive &primitive) const
  {
    for (const UVPrimitive &prim : primitives) {
      if (prim.has_shared_edge(primitive)) {
        return true;
      }
    }
    return false;
  }

  const void extend_border(const UVPrimitive &primitive)
  {
    UVPrimitive new_prim = primitive;
    uint64_t shared_edges_len = 0;
    for (UVPrimitive &prim : primitives) {
      for (std::pair<UVEdge &, UVEdge &> &shared_edge : prim.shared_edges(new_prim)) {
        // TODO: eventually this should be supported. Skipped for now as it isn't the most
        // important this to add. */
        BLI_assert(shared_edge.first.adjacent_uv_primitive == -1);
        BLI_assert(shared_edge.second.adjacent_uv_primitive == -1);
        shared_edge.first.adjacent_uv_primitive = new_prim.index;
        shared_edge.second.adjacent_uv_primitive = prim.index;
        shared_edges_len++;
      }
    }
    BLI_assert_msg(shared_edges_len != 0,
                   "Cannot extend as primitive has no shared edges with UV island.");
    BLI_assert_msg(shared_edges_len < 4,
                   "Cannot extend as primitive has to many shared edges with UV island. "
                   "Inconsistent UVIsland?");

    append(new_prim);
  }

  /**
   * Join 2 uv islands together where the primitive gives the location that joins the two islands
   * together.
   *
   * NOTE: this cannot be used to join two islands that have multiple shared primitives, or
   * connecting via multiple primitives.
   * */
  void join(const UVIsland &other, const UVPrimitive &primitive)
  {
    Vector<const UVPrimitive *> prims_to_extend;
    Vector<const UVPrimitive *> prims_to_append;
    for (const UVPrimitive &other_prim : other.primitives) {
      if (primitive.has_shared_edge(other_prim)) {
        prims_to_extend.append(&other_prim);
      }
      else {
        prims_to_append.append(&other_prim);
      }
    }

    for (const UVPrimitive *other_prim : prims_to_extend) {
      extend_border(*other_prim);
    }
    for (const UVPrimitive *other_prim : prims_to_append) {
      append(*other_prim);
    }
  }
};

struct UVIslands;
struct UVIslandsMask;
void svg_header(std::ostream &ss);
void svg(std::ostream &ss, const UVIslands &islands, int step);
void svg(std::ostream &ss, const UVPrimitive &primitive, int step);
void svg(std::ostream &ss, const UVIslandsMask &mask, int step);
void svg_footer(std::ostream &ss);

struct UVIslands {
  Vector<UVIsland> islands;

  explicit UVIslands(const MLoopTri *primitives, uint64_t primitives_len, const MLoopUV *mloopuv)
  {
    for (int prim = 0; prim < primitives_len; prim++) {
      UVPrimitive primitive(prim, primitives[prim], mloopuv);
      add(primitive);
    }

#ifdef DEBUG_SVG
    std::ofstream of;
    of.open("/tmp/islands.svg");
    svg_header(of);
    svg(of, *this, 0);
    svg_footer(of);
    of.close();
#endif
  }

  void extract_borders()
  {
    for (UVIsland &island : islands) {
      island.extract_border();
    }
  }

 private:
  void add(const UVPrimitive &primitive)
  {
    Vector<uint64_t> extended_islands;
    for (uint64_t index = 0; index < islands.size(); index++) {
      UVIsland &island = islands[index];
      if (island.has_shared_edge(primitive)) {
        extended_islands.append(index);
      }
    }

    if (extended_islands.size() > 0) {
      UVIsland &island = islands[extended_islands[0]];
      island.extend_border(primitive);
      /* `extended_islands` can hold upto 3 islands that are connected with the given tri.
       * they can be joined to a single island, using the first as its target. */
      for (uint64_t index = 1; index < extended_islands.size(); index++) {
        island.join(islands[extended_islands[index]], primitive);
      }

      /* remove the islands that have been joined, starting at the end. */
      for (uint64_t index = extended_islands.size() - 1; index > 0; index--) {
        islands.remove(extended_islands[index]);
      }

      return;
    }

    /* if the tri has not been added we can create a new island. */
    UVIsland island(primitive);
    islands.append(island);
  }

  bool validate() const
  {
    /* After operations it is not allowed that islands share any edges. In that case it should
     * already be merged. */
    for (int i = 0; i < islands.size() - 1; i++) {
      for (int j = i + 1; j < islands.size(); j++) {
        for (const UVPrimitive &prim : islands[j].primitives) {
          if (islands[i].has_shared_edge(prim)) {
            return false;
          }
        }
      }
    }
    return true;
  }
};

/* Bitmask containing the num of the nearest Island. */
// TODO: this is a really quick implementation.
struct UVIslandsMask {
  float2 udim_offset;
  ushort2 resolution;
  Array<uint16_t> mask;

  UVIslandsMask(float2 udim_offset, ushort2 resolution)
      : udim_offset(udim_offset), resolution(resolution), mask(resolution.x * resolution.y)
  {
    clear();
  }

  void clear()
  {
    mask.fill(0xffff);
  }

  void add(const UVIslands &islands)
  {
    for (int index = 0; index < islands.islands.size(); index++) {
      add(index, islands.islands[index]);
    }
  }

  void add(short island_index, const UVIsland &island)
  {
    for (const UVPrimitive &prim : island.primitives) {
      add(island_index, prim);
    }
  }

  void add(short island_index, const UVPrimitive &primitive)
  {
    for (int i = 0; i < 3; i++) {
      add(island_index, primitive.edges[i]);
    }
  }

  void add(short island_index, const UVEdge &edge)
  {
    float2 p;
    for (int i = 0; i < 10; i++) {
      float f = i / 10.0f;
      interp_v2_v2v2(p, edge.vertices[0].uv, edge.vertices[1].uv, f);
      add(island_index, p);
    }
  }

  void add(short island_index, const float2 uv)
  {
    float2 udim_corrected_uv = uv - udim_offset;
    ushort2 mask_uv(udim_corrected_uv.x * resolution.x, udim_corrected_uv.y * resolution.y);
    if (mask_uv.x < 0 || mask_uv.y < 0 || mask_uv.x >= resolution.x || mask_uv.y >= resolution.y) {
      return;
    }
    uint64_t offset = resolution.x * mask_uv.y + mask_uv.x;
    mask[offset] = island_index;
  }

  void dilate()
  {
#ifdef DEBUG_SVG
    std::ofstream of;
    of.open("/tmp/dilate.svg");
    svg_header(of);
    int index = 0;
#endif
    while (true) {
      bool changed = dilate_x();
      changed |= dilate_y();
      if (!changed) {
        break;
      }
#ifdef DEBUG_SVG
      svg(of, *this, index++);
#endif
    }
#ifdef DEBUG_SVG
    svg(of, *this, index);
    svg_footer(of);
    of.close();
#endif
  }

  bool dilate_x()
  {
    bool changed = false;
    const Array<uint16_t> prev_mask = mask;
    for (int y = 0; y < resolution.y; y++) {
      for (int x = 0; x < resolution.x; x++) {
        uint64_t offset = y * resolution.x + x;
        if (prev_mask[offset] != 0xffff) {
          continue;
        }
        if (x != 0 && prev_mask[offset - 1] != 0xffff) {
          mask[offset] = prev_mask[offset - 1];
          changed = true;
        }
        else if (x < resolution.x - 1 && prev_mask[offset + 1] != 0xffff) {
          mask[offset] = prev_mask[offset + 1];
          changed = true;
        }
      }
    }
    return changed;
  }

  bool dilate_y()
  {
    bool changed = false;
    const Array<uint16_t> prev_mask = mask;
    for (int y = 0; y < resolution.y; y++) {
      for (int x = 0; x < resolution.x; x++) {
        uint64_t offset = y * resolution.x + x;
        if (prev_mask[offset] != 0xffff) {
          continue;
        }
        if (y != 0 && prev_mask[offset - resolution.x] != 0xffff) {
          mask[offset] = prev_mask[offset - resolution.x];
          changed = true;
        }
        else if (y < resolution.y - 1 && prev_mask[offset + resolution.x] != 0xffff) {
          mask[offset] = prev_mask[offset + resolution.x];
          changed = true;
        }
      }
    }
    return changed;
  }

  void print() const
  {
    int offset = 0;
    for (int y = 0; y < resolution.y; y++) {
      for (int x = 0; x < resolution.x; x++) {
        uint16_t value = mask[offset++];
        if (value == 0xffff) {
          printf(" ");
        }
        else if (value == 0) {
          printf("0");
        }
        else if (value == 1) {
          printf("1");
        }
        else if (value == 2) {
          printf("2");
        }
        else if (value == 3) {
          printf("3");
        }
        else if (value == 4) {
          printf("4");
        }
        else if (value == 5) {
          printf("5");
        }
        else if (value == 6) {
          printf("6");
        }
      }
      printf("\n");
    }
  }
};

void svg_header(std::ostream &ss)
{
  ss << "<svg viewBox=\"0 0 1024 1024\" width=\"1024\" height=\"1024\" "
        "xmlns=\"http://www.w3.org/2000/svg\">\n";
}
void svg_footer(std::ostream &ss)
{
  ss << "</svg>\n";
}
void svg(std::ostream &ss, const UVEdge &edge)
{
  ss << "       <line x1=\"" << edge.vertices[0].uv.x * 1024 << "\" y1=\""
     << edge.vertices[0].uv.y * 1024 << "\" x2=\"" << edge.vertices[1].uv.x * 1024 << "\" y2=\""
     << edge.vertices[1].uv.y * 1024 << "\"/>\n";
}

void svg(std::ostream &ss, const UVIslands &islands, int step)
{
  ss << "<g transform=\"translate(" << step * 1024 << " 0)\">\n";
  int island_index = 0;
  for (const UVIsland &island : islands.islands) {
    ss << "  <g fill=\"yellow\">\n";

    /* Inner edges */
    ss << "    <g stroke=\"grey\" stroke-dasharray=\"5 5\">\n";
    for (const UVPrimitive &primitive : island.primitives) {
      for (int i = 0; i < 3; i++) {
        const UVEdge &edge = primitive.edges[i];
        if (edge.adjacent_uv_primitive == -1) {
          continue;
        }
        svg(ss, edge);
      }
    }
    ss << "     </g>\n";

    /* Border */
    ss << "    <g stroke=\"black\" stroke-width=\"2\">\n";
    for (const UVPrimitive &primitive : island.primitives) {
      for (int i = 0; i < 3; i++) {
        const UVEdge &edge = primitive.edges[i];
        if (edge.adjacent_uv_primitive != -1) {
          continue;
        }
        svg(ss, edge);
      }
    }
    ss << "     </g>\n";

    ss << "   </g>\n";
    island_index++;
  }

  ss << "</g>\n";
}

void svg(std::ostream &ss, const UVIslandsMask &mask, int step)
{
  ss << "<g transform=\"translate(" << step * 1024 << " 0)\">\n";
  ss << " <g fill=\"none\" stroke=\"black\">\n";

  float2 resolution = float2(mask.resolution.x, mask.resolution.y);
  for (int x = 0; x < mask.resolution.x; x++) {
    for (int y = 0; y < mask.resolution.y; y++) {
      int offset = y * mask.resolution.x + x;
      int offset2 = offset - 1;
      if (y == 0 && mask.mask[offset] == 0xffff) {
        continue;
      }
      if (x > 0 && mask.mask[offset] == mask.mask[offset2]) {
        continue;
      }
      float2 start = float2(float(x), float(y)) / resolution * float2(1024, 1024);
      float2 end = float2(float(x), float(y + 1)) / resolution * float2(1024, 1024);
      ss << "       <line x1=\"" << start.x << "\" y1=\"" << start.y << "\" x2=\"" << end.x
         << "\" y2=\"" << end.y << "\"/>\n";
    }
  }

  for (int x = 0; x < mask.resolution.x; x++) {
    for (int y = 0; y < mask.resolution.y; y++) {
      int offset = y * mask.resolution.x + x;
      int offset2 = offset - mask.resolution.x;
      if (x == 0 && mask.mask[offset] == 0xffff) {
        continue;
      }
      if (y > 0 && mask.mask[offset] == mask.mask[offset2]) {
        continue;
      }
      float2 start = float2(float(x), float(y)) / resolution * float2(1024, 1024);
      float2 end = float2(float(x + 1), float(y)) / resolution * float2(1024, 1024);
      ss << "       <line x1=\"" << start.x << "\" y1=\"" << start.y << "\" x2=\"" << end.x
         << "\" y2=\"" << end.y << "\"/>\n";
    }
  }
  ss << " </g>\n";
  ss << "</g>\n";
}

void svg_coords(std::ostream &ss, const float2 &coords)
{
  ss << coords.x * 1024 << "," << coords.y * 1024;
}

void svg(std::ostream &ss, const UVPrimitive &primitive)
{
  ss << "       <polygon points=\"";
  for (int i = 0; i < 3; i++) {
    svg_coords(ss, primitive.edges[i].vertices[0].uv);
    ss << " ";
  }
  ss << "\"/>\n";
}

void svg(std::ostream &ss, const UVPrimitive &primitive, int step)
{
  ss << "<g transform=\"translate(" << step * 1024 << " 0)\">\n";
  ss << "  <g fill=\"red\">\n";
  svg(ss, primitive);
  ss << "  </g>";
  ss << "</g>\n";
}

}  // namespace blender::bke::uv_islands