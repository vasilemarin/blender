// Copyright 2020 Blender Foundation. All rights reserved.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// Author: Sergey Sharybin

#ifndef OPENSUBDIV_MESH_TOPOLOGY_H_
#define OPENSUBDIV_MESH_TOPOLOGY_H_

#include <cstring>

#include "internal/base/memory.h"
#include "internal/base/type.h"

struct OpenSubdiv_Converter;

namespace blender {
namespace opensubdiv {

class VertexTopologyTag {
 public:
  float sharpness = 0.0f;
};

class EdgeTopology {
 public:
  bool isValid() const
  {
    return v1 >= 0 && v2 >= 0;
  }

  int v1 = -1;
  int v2 = -1;
};

class FaceTopology {
 public:
  FaceTopology()
  {
  }

  ~FaceTopology()
  {
    delete[] vertex_indices;
  }

  void setNumVertices(int new_num_vertices)
  {
    num_vertices = new_num_vertices;

    delete[] vertex_indices;
    vertex_indices = new int[num_vertices];
  }

  void setVertexIndices(int *face_vertex_indices)
  {
    memcpy(vertex_indices, face_vertex_indices, sizeof(int) * getNumVertices());
  }

  bool isVertexIndicesEqual(const vector<int> &other_vertex_indices) const
  {
    if (other_vertex_indices.size() != getNumVertices()) {
      return false;
    }

    return memcmp(vertex_indices, other_vertex_indices.data(), sizeof(int) * num_vertices);
  }

  bool isValid() const
  {
    for (int i = 0; i < getNumVertices(); ++i) {
      if (vertex_indices[i] < 0) {
        return false;
      }
    }

    return true;
  }

  int getNumVertices() const
  {
    return num_vertices;
  }

  // NOTE: Use bare pointers to avoid object's size overhed. For example, when
  // using managed vector<int> it is 24 bytes on GCC-10 (to store an internal
  // state of the vector). Here is is only 8 bytes (on 64bit machine).
  //
  // TODO(sergey): Consider using packed structure to lower the memory footprint
  // since, for some reason, due to padding the sizeof(FaceTopology) is 16 bytes
  // which could be packed down to 12.
  int *vertex_indices = nullptr;

  int num_vertices = 0;
};

class EdgeTopologyTag {
 public:
  float sharpness = 0.0f;
};

// Simplified representation of mesh topology.
// Only includes parts of actual mesh topology which is needed to perform
// comparison between Application side and OpenSubddiv side.
class MeshTopology {
 public:
  MeshTopology();
  MeshTopology(const MeshTopology &other) = default;
  MeshTopology(MeshTopology &&other) noexcept = default;
  ~MeshTopology();

  MeshTopology &operator=(const MeshTopology &other) = default;
  MeshTopology &operator=(MeshTopology &&other) = default;

  //////////////////////////////////////////////////////////////////////////////
  // Vertices.

  void setNumVertices(int num_vertices);
  int getNumVertices() const;

  void setVertexSharpness(int vertex_index, float sharpness);
  float getVertexSharpness(int vertex_index) const;

  //////////////////////////////////////////////////////////////////////////////
  // Edges.

  void setNumEdges(int num_edges);

  // NOTE: Unless full topology was specified will return number of edges based
  // on last edge index for which topology tag was specified.
  int getNumEdges() const;

  void setEdgevertexIndices(int edge_index, int v1, int v2);

  EdgeTopology &getEdge(int edge_index);
  const EdgeTopology &getEdge(int edge_index) const;

  void setEdgeSharpness(int edge_index, float sharpness);
  float getEdgeSharpness(int edge_index) const;

  //////////////////////////////////////////////////////////////////////////////
  // Faces.

  void setNumFaces(int num_faces);

  int getNumFaces() const;

  FaceTopology &getFace(int face_index);
  const FaceTopology &getFace(int face_index) const;

  void setNumFaceVertices(int face_index, int num_face_vertices);
  void setFaceVertexIndices(int face_index, int *face_vertex_indices);

  //////////////////////////////////////////////////////////////////////////////
  // Comparison.

  // Check whether this topology refiner defines same topology as the given
  // converter.
  bool isEqualToConverter(const OpenSubdiv_Converter *converter) const;

 protected:
  // Unless full topology was specified the number of edges is not know ahead
  // of a time.
  void ensureNumEdgesAtLeast(int num_edges);

  // Geometry tags are stored sparsly.
  //
  // These functions ensures that the storage can be addressed by an index which
  // corresponds to the given size.
  void ensureVertexTagsSize(int num_vertices);
  void ensureEdgeTagsSize(int num_edges);

  int num_vertices_;
  vector<VertexTopologyTag> vertex_tags_;

  int num_edges_;
  vector<EdgeTopology> edges_;
  vector<EdgeTopologyTag> edge_tags_;

  int num_faces_;
  vector<FaceTopology> faces_;

  MEM_CXX_CLASS_ALLOC_FUNCS("MeshTopology");
};

}  // namespace opensubdiv
}  // namespace blender

#endif  // OPENSUBDIV_MESH_TOPOLOGY_H_
