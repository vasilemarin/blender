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

#include "BKE_context.h"
#include "BKE_node_ui_storage.hh"
#include "BKE_object.h"

#include "UI_interface.h"
#include "UI_resources.h"

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
  Set<std::string> attributes;
  bNodeSocket &socket;
};

static void attribute_search_update_fn(const bContext *UNUSED(C),
                                       void *arg,
                                       const char *str,
                                       uiSearchItems *items)
{
  const NodeUIStorage &storage = *static_cast<const NodeUIStorage *>(arg);
  Set<std::string> attribute_name_hints = {"HELLO!", "DOES", "THIS", "WORK???"};

  StringSearch *search = BLI_string_search_new();

  for (const std::string attribute_name : attribute_name_hints) {
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

  MEM_freeN(filtered_items);
  BLI_string_search_free(search);
}

static void attribute_search_exec_fn(struct bContext *C, void *arg1, void *arg2)
{
  AttributeSearchData *data = static_cast<AttributeSearchData *>(arg1);
  const std::string *attribute_name = static_cast<std::string *>(arg2);

  bNodeSocketValueString *string_value = static_cast<bNodeSocketValueString *>(
      data->socket.default_value);

  BLI_strncpy(string_value->value, attribute_name->c_str(), 1024);
}

void button_add_attribute_search(uiBut *but, const NodeUIStorage &ui_storage, bNodeSocket *socket)
{
  AttributeSearchData data = {ui_storage.attribute_name_hints, *socket};
  UI_but_func_search_set(but,
                         nullptr,
                         attribute_search_update_fn,
                         (void *)&data,
                         nullptr,
                         attribute_search_exec_fn,
                         nullptr);
}
