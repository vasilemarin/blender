/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#pragma once

#include "BLI_hash.hh"
#include "BLI_map.hh"

struct bNode;
struct bNodeTree;
struct Object;
struct ModifierData;

struct NodeUIStorageContextModifier {
  const struct Object *object;
  const struct ModifierData *modifier;

  uint64_t hash() const
  {
    const uint64_t hash1 = blender::DefaultHash<const Object *>{}(object);
    const uint64_t hash2 = blender::DefaultHash<const ModifierData *>{}(modifier);
    return hash1 ^ (hash2 * 33);
  }

  bool operator==(const NodeUIStorageContextModifier &other) const
  {
    return other.object == object && other.modifier == modifier;
  }
};

enum class NodeWarningType {
  Error,
  Warning,
  Info,
};

struct NodeWarning {
  NodeWarningType type;
  std::string message;
};

struct NodeUIStorage {
  blender::Vector<NodeWarning> warnings;
};

struct NodeTreeUIStorage {
  blender::Map<const struct bNode *, blender::Map<NodeUIStorageContextModifier, NodeUIStorage>>
      node_map;

  NodeTreeUIStorage() = default;
};

void BKE_nodetree_ui_storage_clear(struct bNodeTree &ntree);

void BKE_nodetree_ui_storage_ensure(bNodeTree &ntree);

void BKE_nodetree_error_message_add(struct bNodeTree &ntree,
                                    const NodeUIStorageContextModifier &context,
                                    const struct bNode &node,
                                    const NodeWarningType type,
                                    const std::string &message);
