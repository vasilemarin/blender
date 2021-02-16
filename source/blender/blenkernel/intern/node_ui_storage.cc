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

#include "CLG_log.h"

#include "BLI_map.hh"
#include "BLI_string_ref.hh"
#include "BLI_vector.hh"

#include "DNA_node_types.h"
#include "DNA_object_types.h"

#include "BKE_node_ui_storage.hh"

static CLG_LogRef LOG = {"bke.node_ui_storage"};

using blender::Map;
using blender::StringRef;
using blender::Vector;

void BKE_nodetree_ui_storage_ensure(bNodeTree &ntree)
{
  if (ntree.runtime == nullptr) {
    ntree.runtime = new NodeTreeUIStorage();
  }
}

void BKE_nodetree_ui_storage_clear(bNodeTree &ntree)
{
  NodeTreeUIStorage *ui_storage = ntree.runtime;
  if (ui_storage != nullptr) {
    ui_storage->node_map.clear();
    delete ui_storage;
    ntree.runtime = nullptr;
  }
}

static void node_error_message_log(bNodeTree &ntree,
                                   const bNode &node,
                                   const StringRef message,
                                   const NodeWarningType type)
{
  switch (type) {
    case NodeWarningType::Error:
      CLOG_ERROR(&LOG,
                 "Node Tree: \"%s\", Node: \"%s\", %s",
                 ntree.id.name + 2,
                 node.name,
                 message.data());
      break;
    case NodeWarningType::Warning:
      CLOG_WARN(&LOG,
                "Node Tree: \"%s\", Node: \"%s\", %s",
                ntree.id.name + 2,
                node.name,
                message.data());
      break;
    case NodeWarningType::Info:
      CLOG_INFO(&LOG,
                2,
                "Node Tree: \"%s\", Node: \"%s\", %s",
                ntree.id.name + 2,
                node.name,
                message.data());
      break;
  }
}

void BKE_nodetree_error_message_add(bNodeTree &ntree,
                                    const NodeUIStorageContextModifier &context,
                                    const bNode &node,
                                    const NodeWarningType type,
                                    const std::string &message)
{
  BLI_assert(ntree.runtime != nullptr);
  NodeTreeUIStorage &node_tree_ui_storage = *ntree.runtime;

  node_error_message_log(ntree, node, message, type);

  Map<NodeUIStorageContextModifier, NodeUIStorage> &context_to_storage_map =
      node_tree_ui_storage.node_map.lookup_or_add_default(node.name);

  NodeUIStorage &node_ui_storage = context_to_storage_map.lookup_or_add_default(context);

  node_ui_storage.warnings.append({type, message});
}
