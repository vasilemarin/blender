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

#include "BLI_listbase.h"
#include "BLI_map.hh"
#include "BLI_string_ref.hh"
#include "BLI_string_search.h"
#include "BLI_vector.hh"

#include "DNA_modifier_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"

#include "BKE_context.h"
#include "BKE_node_ui_storage.hh"
#include "BKE_object.h"

#include "UI_interface.h"

using blender::StringRef;
using blender::Vector;

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
  Vector<std::string> attributes;
};

static void menu_search_update_fn(const bContext *UNUSED(C),
                                  void *arg,
                                  const char *str,
                                  uiSearchItems *items)
{
  struct MenuSearch_Data *data = arg;

  StringSearch *search = BLI_string_search_new();

  LISTBASE_FOREACH (struct MenuSearch_Item *, item, &data->items) {
    BLI_string_search_add(search, item->drawwstr_full, item);
  }

  struct MenuSearch_Item **filtered_items;
  const int filtered_amount = BLI_string_search_query(search, str, (void ***)&filtered_items);

  for (int i = 0; i < filtered_amount; i++) {
    struct MenuSearch_Item *item = filtered_items[i];
    if (!UI_search_item_add(items, item->drawwstr_full, item, item->icon, item->state, 0)) {
      break;
    }
  }

  MEM_freeN(filtered_items);
  BLI_string_search_free(search);
}

void button_add_attribute_search(const bContext *C,
                                 uiBut *but,
                                 const bNodeTree &ntree,
                                 const bNode &node)
{
}
