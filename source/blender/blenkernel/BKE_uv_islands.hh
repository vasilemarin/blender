
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

  std::optional<std::pair<UVEdge &, UVEdge &>> shared_edge(UVPrimitive &other)
  {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        if (edges[i].has_shared_edge(other.edges[j])) {
          return std::pair<UVEdge &, UVEdge &>(edges[i], other.edges[j]);
        }
      }
    }
    return std::nullopt;
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

struct UVIsland {
  Vector<UVPrimitive> primitives;

  UVIsland(const UVPrimitive &primitive)
  {
    primitives.append(primitive);
  }

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
      std::optional<std::pair<UVEdge &, UVEdge &>> shared_edge = prim.shared_edge(new_prim);
      if (!shared_edge.has_value()) {
        continue;
      }
      // TODO: eventually this should be supported. Skipped for now as it isn't the most important
      // this to add. */
      std::pair<UVEdge &, UVEdge &> &edges = *shared_edge;
      BLI_assert(edges.first.adjacent_uv_primitive == -1);
      BLI_assert(edges.second.adjacent_uv_primitive == -1);
      edges.first.adjacent_uv_primitive = new_prim.index;
      edges.second.adjacent_uv_primitive = prim.index;
      shared_edges_len++;
    }
    BLI_assert_msg(shared_edges_len != 0,
                   "Cannot extend as primitive has no shared edges with UV island.");
    BLI_assert_msg(shared_edges_len < 3,
                   "Cannot extend as primitive has to many shared edges with UV island. "
                   "Inconsistent UVIsland?");

    primitives.append(new_prim);
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
    for (const UVPrimitive &other_prim : other.primitives) {
      if (primitive.has_shared_edge(other_prim)) {
        extend_border(other_prim);
        continue;
      }
      primitives.append(other_prim);
    }
  }
};

struct UVIslands;
void svg_header(std::ostream &ss);
void svg(std::ostream &ss, const UVIslands &islands, int step);
void svg(std::ostream &ss, const UVPrimitive &primitive, int step);
void svg_footer(std::ostream &ss);

struct UVIslands {
  Vector<UVIsland> islands;

  explicit UVIslands(const MLoopTri *primitives, uint64_t primitives_len, const MLoopUV *mloopuv)
  {
    std::ofstream of;
    of.open("/tmp/islands.svg");
    svg_header(of);
    int step = 0;
    for (int prim = 0; prim < primitives_len; prim++) {
      UVPrimitive primitive(prim, primitives[prim], mloopuv);
      if (prim == 12) {
        printf("BREAK");
      }
      if (prim < 14) {
        svg(of, primitive, step);
        svg(of, *this, step);
        of.flush();
        step++;
      }
      BLI_assert(validate());
      add(primitive);
      if (!validate()) {
        svg(of, *this, step);
        svg_footer(of);
        of.close();
        BLI_assert(false);
        return;
      }
    }
    svg(of, *this, step);
    svg_footer(of);
    of.close();
    // TODO: extract border.
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
      islands[extended_islands[0]].extend_border(primitive);
      /* `extended_islands` can hold upto 3 islands that are connected with the given tri.
       * they can be joined to a single island, using the first as its target. */
      for (uint64_t index = 1; index < extended_islands.size(); index++) {
        islands[extended_islands[0]].join(islands[extended_islands[index]], primitive);
      }

      /* remove the islands that have been joined, starting at the end. */
      for (uint64_t index = extended_islands.size() - 1; index > 0; index--) {
        islands.remove_and_reorder(index);
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
  ss << "  <g fill=\"lightred\">\n";
  svg(ss, primitive);
  ss << "  </g>";
  ss << "</g>\n";
}

/*std::string as_svg(const UVIslands &islands)
{
  std::stringstream ss;
  svg_header(ss);
  svg(ss, islands);
  svg_footer(ss);

  return ss.str();
}*/

}  // namespace blender::bke::uv_islands