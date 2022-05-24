
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_uv_islands.hh"

namespace blender::bke::uv_islands {
/* -------------------------------------------------------------------- */
/** \name UVIsland
 * \{ */

void UVIsland::extract_border()
{
  Vector<UVBorderEdge> edges;

  for (UVPrimitive &primitive : primitives) {
    for (UVEdge &edge : primitive.edges) {
      if (edge.is_border_edge()) {
        edges.append(UVBorderEdge(&edge));
      }
    }
  }

  while (true) {
    UVBorder border;
    /* Find a part of the border that haven't been extracted yet. */
    UVBorderEdge *starting_border_edge = nullptr;
    for (UVBorderEdge &edge : edges) {
      if (edge.tag == false) {
        starting_border_edge = &edge;
        break;
      }
    }
    if (starting_border_edge == nullptr) {
      break;
    }

    starting_border_edge->tag = true;
    border.verts.append(UVBorderVert(starting_border_edge->edge->vertices[0].uv));
    border.verts.append(UVBorderVert(starting_border_edge->edge->vertices[1].uv));
    UVBorderVert &first = border.verts.first();
    UVBorderVert &current = border.verts.last();
    while (current.uv != first.uv) {
      for (UVBorderEdge &border_edge : edges) {
        if (border_edge.tag == true) {
          continue;
        }
        int i;
        for (i = 0; i < 2; i++) {
          if (border_edge.edge->vertices[i].uv == current.uv) {
            border.verts.append(UVBorderVert(border_edge.edge->vertices[1 - i].uv));
            border_edge.tag = true;

            /* Update the references as they could be changed. */
            current = border.verts.last();
            first = border.verts.first();
            break;
          }
        }
        if (i != 2) {
          break;
        }
      }
    }
    // Remove the last item as it is the same as the first.
    border.verts.pop_last();
    borders.append(border);
  }
}

static UVBorderVert *sharpest_border_vert(UVBorder &border, float *r_radius)
{
  UVBorderVert *result = nullptr;
  for (UVBorderVert &vert : border.verts) {
    if (vert.flags.extendable == false) {
      continue;
    }
    float new_radius = border.outside_angle(vert);
    if (new_radius < *r_radius) {
      *r_radius = new_radius;
      result = &vert;
    }
  }
  return result;
}

static UVBorderVert *sharpest_border_vert(UVIsland &island)
{
  UVBorderVert *result;
  float sharpest_radius = std::numeric_limits<float>::max();
  for (UVBorder &border : island.borders) {
    float new_radius;
    UVBorderVert *new_result = sharpest_border_vert(border, &new_radius);
    if (new_radius < sharpest_radius) {
      sharpest_radius = new_radius;
      result = new_result;
    }
  }
  return result;
}

void UVIsland::extend_border(const UVIslandsMask &mask, const short island_index)
{
  // Find sharpest corner that still inside the island mask and can be extended.
  // exit when no corner could be found.
  while (true) {
    UVBorderVert *extension_vert = sharpest_border_vert(*this);
    if (extension_vert == nullptr) {
      break;
    }

    /* When outside the mask, the uv should not be considered for extension. */
    if (!mask.is_masked(island_index, extension_vert->uv)) {
      extension_vert->flags.extendable = false;
      continue;
    }

    // TODO: extend

    /* Mark that the vert is extended. Unable to extend twice. */
    extension_vert->flags.extendable = false;
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name UVBorder
 * \{ */
void flip_order()
{
  // TODO implement.
}

static const UVBorderVert *previous_vert(const UVBorder &border, const UVBorderVert &vert)
{
  const UVBorderVert *result = &border.verts.last();
  for (const UVBorderVert &other : border.verts) {
    if (other.uv == vert.uv) {
      return result;
    }
    result = &other;
  }
  BLI_assert_unreachable();
  return nullptr;
}

static const UVBorderVert *next_vert(const UVBorder &border, const UVBorderVert &vert)
{
  const UVBorderVert *result = &border.verts.first();
  // TODO: tried to use reversed_iterator, but wasn't able to solve its compiler issues ATM. will
  // look at it later on.
  for (int i = border.verts.size() - 1; i >= 0; i--) {
    const UVBorderVert &other = border.verts[i];
    if (other.uv == vert.uv) {
      return result;
    }
    result = &other;
  }
  BLI_assert_unreachable();
  return nullptr;
}

float UVBorder::outside_angle(const UVBorderVert &vert) const
{
  // find previous vert.
  const UVBorderVert &prev = *previous_vert(*this, vert);
  const UVBorderVert &next = *next_vert(*this, vert);
  // find next vert.
  const float2 v1 = vert.uv - prev.uv;
  const float2 v2 = next.uv - vert.uv;

  const float angle_signed = M_PI - angle_signed_v2v2(v1, v2);
  return angle_signed;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name UVIslandsMask
 * \{ */

static bool dilate_x(UVIslandsMask &islands_mask)
{
  bool changed = false;
  const Array<uint16_t> prev_mask = islands_mask.mask;
  for (int y = 0; y < islands_mask.resolution.y; y++) {
    for (int x = 0; x < islands_mask.resolution.x; x++) {
      uint64_t offset = y * islands_mask.resolution.x + x;
      if (prev_mask[offset] != 0xffff) {
        continue;
      }
      if (x != 0 && prev_mask[offset - 1] != 0xffff) {
        islands_mask.mask[offset] = prev_mask[offset - 1];
        changed = true;
      }
      else if (x < islands_mask.resolution.x - 1 && prev_mask[offset + 1] != 0xffff) {
        islands_mask.mask[offset] = prev_mask[offset + 1];
        changed = true;
      }
    }
  }
  return changed;
}

static bool dilate_y(UVIslandsMask &islands_mask)
{
  bool changed = false;
  const Array<uint16_t> prev_mask = islands_mask.mask;
  for (int y = 0; y < islands_mask.resolution.y; y++) {
    for (int x = 0; x < islands_mask.resolution.x; x++) {
      uint64_t offset = y * islands_mask.resolution.x + x;
      if (prev_mask[offset] != 0xffff) {
        continue;
      }
      if (y != 0 && prev_mask[offset - islands_mask.resolution.x] != 0xffff) {
        islands_mask.mask[offset] = prev_mask[offset - islands_mask.resolution.x];
        changed = true;
      }
      else if (y < islands_mask.resolution.y - 1 &&
               prev_mask[offset + islands_mask.resolution.x] != 0xffff) {
        islands_mask.mask[offset] = prev_mask[offset + islands_mask.resolution.x];
        changed = true;
      }
    }
  }
  return changed;
}

void UVIslandsMask::dilate()
{
#ifdef DEBUG_SVG
  std::ofstream of;
  of.open("/tmp/dilate.svg");
  svg_header(of);
  int index = 0;
#endif
  while (true) {
    bool changed = dilate_x(*this);
    changed |= dilate_y(*this);
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

bool UVIslandsMask::is_masked(const short island_index, const float2 uv) const
{
  float2 local_uv = uv - udim_offset;
  if (local_uv.x < 0.0f || local_uv.y < 0.0f || local_uv.x >= 1.0f || local_uv.y >= 1.0f) {
    return false;
  }
  float2 pixel_pos_f = local_uv * float2(resolution.x, resolution.y);
  ushort2 pixel_pos = ushort2(pixel_pos_f.x, pixel_pos_f.y);
  uint64_t offset = pixel_pos.y * resolution.x + pixel_pos.x;
  return mask[offset] == island_index;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name SVG export
 *
 * Debugging functions to export UV islands to SVG files.
 * \{ */

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

void svg(std::ostream &ss, const UVBorder &border)
{
  ss << "<g>\n";

  ss << " <g stroke=\"lightgrey\">\n";
  for (const UVBorderVert &vert : border.verts) {
    print_v2_id(vert.uv);
  }
  for (const UVBorderVert &vert : border.verts) {
    const UVBorderVert &prev = *previous_vert(border, vert);
    float2 v1 = prev.uv * float2(1024, 1024);
    float2 v2 = vert.uv * float2(1024, 1024);
    ss << "       <line x1=\"" << v1.x << "\" y1=\"" << v1.y << "\" x2=\"" << v2.x << "\" y2=\""
       << v2.y << "\"/>\n";
  }
  ss << " </g>\n";

  ss << " <g fill=\"red\">\n";
  for (const UVBorderVert &vert : border.verts) {
    float2 v1 = vert.uv * float2(1024, 1024);
    ss << "       <text x=\"" << v1.x << "\" y=\"" << v1.y << "\">"
       << (border.outside_angle(vert) / M_PI * 360) << "</text>\n";
  }
  ss << " </g>\n";

  ss << "</g>\n";
}

/** \} */

}  // namespace blender::bke::uv_islands
