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

#include "BLI_index_range.hh"
#include "BLI_listbase.h"
#include "BLI_map.hh"
#include "BLI_set.hh"
#include "BLI_string_ref.hh"
#include "BLI_string_search.h"

#include "DNA_modifier_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h"

#include "BKE_context.h"
#include "BKE_node_ui_storage.hh"
#include "BKE_object.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "node_intern.h"

using blender::IndexRange;
using blender::Map;
using blender::Set;
using blender::StringRef;

static const NodeUIStorage *node_ui_storage_get_from_context(const bContext *C,
                                                             const bNodeTree &ntree,
                                                             const bNode &node)
{
  const NodeTreeUIStorage *ui_storage = ntree.ui_storage;
  if (ui_storage == nullptr) {
    return nullptr;
  }

  const Object *active_object = CTX_data_active_object(C);
  const ModifierData *active_modifier = BKE_object_active_modifier(active_object);
  if (active_object == nullptr || active_modifier == nullptr) {
    return nullptr;
  }

  const NodeTreeEvaluationContext context(*active_object, *active_modifier);
  const Map<std::string, NodeUIStorage> *storage = ui_storage->context_map.lookup_ptr(context);
  if (storage == nullptr) {
    return nullptr;
  }

  return storage->lookup_ptr_as(StringRef(node.name));
}

struct AttributeSearchData {
  const bNodeTree &node_tree;
  const bNode &node;
  bNodeSocket &socket;
  std::string current_value;

  /* Needed for proper interaction with the search button. Otherwise the interface code can't keep
   * track of which button is active by comparing pointers to this struct, because it is newly
   * allocated for every redraw. */
  uiBut *search_button;
  uiButStore *button_store;
  uiBlock *button_store_block;
};

static void attribute_search_update_fn(const bContext *C,
                                       void *arg,
                                       const char *str,
                                       uiSearchItems *items)
{
  AttributeSearchData *data = static_cast<AttributeSearchData *>(arg);
  const NodeUIStorage *ui_storage = node_ui_storage_get_from_context(
      C, data->node_tree, data->node);
  if (ui_storage == nullptr) {
    // return;
  }

  // const Set<std::string> &attribute_name_hints = ui_storage->attribute_name_hints;
  const Set<std::string> attribute_name_hints = {"HELLO!", "DOES", "THIS", "WORK???"};

  StringSearch *search = BLI_string_search_new();
  for (const std::string &attribute_name : attribute_name_hints) {
    BLI_string_search_add(search, attribute_name.c_str(), (void *)&attribute_name);
  }

  std::string **filtered_items;
  const int filtered_amount = BLI_string_search_query(search, str, (void ***)&filtered_items);

  for (const int i : IndexRange(filtered_amount)) {
    std::string *item = filtered_items[i];
    if (!UI_search_item_add(items, item->c_str(), item, ICON_NONE, 0, 0)) {
      break;
    }
  }

  /* Always add the current value. */
  UI_search_item_add(items, data->current_value.c_str(), &data->current_value, ICON_NONE, 0, 0);

  MEM_freeN(filtered_items);
  BLI_string_search_free(search);
}

static void attribute_search_exec_fn(struct bContext *UNUSED(C), void *arg1, void *arg2)
{
  AttributeSearchData *data = static_cast<AttributeSearchData *>(arg1);
  const std::string *attribute_name = static_cast<std::string *>(arg2);

  bNodeSocketValueString *string_value = static_cast<bNodeSocketValueString *>(
      data->socket.default_value);

  BLI_strncpy(string_value->value, attribute_name->c_str(), 1024);
}

static void attribute_search_free_fn(void *arg)
{
  AttributeSearchData *data = static_cast<AttributeSearchData *>(arg);

  UI_butstore_free(data->button_store_block, data->button_store);
  delete data;
}

void button_add_attribute_search(
    const bContext *C, bNode *node, bNodeSocket *socket, uiBlock *block, uiBut *but)
{
  BLI_assert(socket->type == SOCK_STRING);

  /* Always adding the button default value is valid because this search menu
   * shouldn't display when the socket is connected with an input link anyway. */
  const bNodeSocketValueString *socket_value = static_cast<bNodeSocketValueString *>(
      socket->default_value);
  const char *current_value = socket_value->value;

  /* TODO: This could just get a node tree argument. */
  SpaceNode *space_node = CTX_wm_space_node(C);
  if (space_node == nullptr) {
    return;
  }
  if (space_node->edittree == nullptr) {
    return;
  }

  AttributeSearchData *data = new AttributeSearchData{
      *space_node->edittree,
      *node,
      *socket,
      current_value,
      but,
      UI_butstore_create(block),
      block,
  };

  UI_butstore_register(data->button_store, &data->search_button);

  UI_but_func_search_set(but,
                         nullptr,
                         attribute_search_update_fn,
                         static_cast<void *>(data),
                         attribute_search_free_fn,
                         attribute_search_exec_fn,
                         nullptr);
}
