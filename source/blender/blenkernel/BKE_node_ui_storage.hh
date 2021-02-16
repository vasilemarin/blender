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
#include "BLI_session_uuid.h"

#include "DNA_ID.h"
#include "DNA_modifier_types.h"
#include "DNA_session_uuid_types.h"

struct bNode;
struct bNodeTree;
struct Object;
struct ModifierData;

struct NodeUIStorageContextModifier {
  std::string object_name;
  SessionUUID modifier_session_uuid;
  /* TODO: The same node tree can be used multiple times in a parent node tree,
   * so the tree path should be added to the context here. */

  NodeUIStorageContextModifier(const Object &object, const ModifierData &modifier)
  {
    object_name = reinterpret_cast<const ID &>(object).name;
    modifier_session_uuid = modifier.session_uuid;
  }

  uint64_t hash() const
  {
    const uint64_t hash1 = blender::DefaultHash<std::string>{}(object_name);
    const uint64_t hash2 = BLI_session_uuid_hash_uint64(&modifier_session_uuid);
    return hash1 ^ (hash2 * 33); /* Copied from DefaultHash pair hash function. */
  }

  bool operator==(const NodeUIStorageContextModifier &other) const
  {
    return other.object_name == object_name &&
           BLI_session_uuid_is_equal(&other.modifier_session_uuid, &modifier_session_uuid);
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
  blender::Map<std::string, blender::Map<NodeUIStorageContextModifier, NodeUIStorage>> node_map;
};

void BKE_nodetree_ui_storage_clear(struct bNodeTree &ntree);

void BKE_nodetree_ui_storage_ensure(bNodeTree &ntree);

void BKE_nodetree_error_message_add(struct bNodeTree &ntree,
                                    const NodeUIStorageContextModifier &context,
                                    const struct bNode &node,
                                    const NodeWarningType type,
                                    std::string message);
