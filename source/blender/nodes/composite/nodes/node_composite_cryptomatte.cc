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
 *
 * The Original Code is Copyright (C) 2018 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup cmpnodes
 */

#include "node_composite_util.h"

#include "BLI_assert.h"
#include "BLI_dynstr.h"
#include "BLI_hash_mm3.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_cryptomatte.h"
#include "BKE_global.h"
#include "BKE_lib_id.h"
#include "BKE_library.h"
#include "BKE_main.h"

#include <optional>

extern "C" {
static std::optional<CryptomatteEntry *> cryptomatte_find(const NodeCryptomatte &n,
                                                          float encoded_hash)
{
  LISTBASE_FOREACH (CryptomatteEntry *, entry, &n.entries) {
    if (entry->encoded_hash == encoded_hash) {
      return std::make_optional(entry);
    }
  }
  return std::nullopt;
}

static void cryptomatte_add(Main *bmain, NodeCryptomatte &n, float encoded_hash)
{
  /* Check if entry already exist. */
  if (cryptomatte_find(n, encoded_hash)) {
    return;
  }

  CryptomatteEntry *entry = static_cast<CryptomatteEntry *>(
      MEM_callocN(sizeof(CryptomatteEntry), __func__));
  entry->encoded_hash = encoded_hash;
  BKE_cryptomatte_find_name(bmain, encoded_hash, entry->name, sizeof(entry->name));

  BLI_addtail(&n.entries, entry);
}

static void cryptomatte_remove(NodeCryptomatte &n, float encoded_hash)
{
  std::optional<CryptomatteEntry *> entry = cryptomatte_find(n, encoded_hash);
  if (!entry) {
    return;
  }
  BLI_remlink(&n.entries, entry.value());
  MEM_freeN(entry.value());
}

static bNodeSocketTemplate cmp_node_cryptomatte_in[] = {
    {SOCK_RGBA, N_("Image"), 0.0f, 0.0f, 0.0f, 1.0f}, {-1, ""}};

static bNodeSocketTemplate cmp_node_cryptomatte_out[] = {
    {SOCK_RGBA, N_("Image")},
    {SOCK_FLOAT, N_("Matte")},
    {SOCK_RGBA, N_("Pick")},
    {-1, ""},
};

void ntreeCompositCryptomatteSyncFromAdd(Main *bmain, bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeCryptomatte *n = static_cast<NodeCryptomatte *>(node->storage);
  if (n->add[0] != 0.0f) {
    cryptomatte_add(bmain, *n, n->add[0]);
    zero_v3(n->add);
  }
}

void ntreeCompositCryptomatteSyncFromRemove(Main *UNUSED(bmain),
                                            bNodeTree *UNUSED(ntree),
                                            bNode *node)
{
  NodeCryptomatte *n = static_cast<NodeCryptomatte *>(node->storage);
  if (n->remove[0] != 0.0f) {
    cryptomatte_remove(*n, n->remove[0]);
    zero_v3(n->remove);
  }
}

const char *CRYPTOMATTE_LAYER_PREFIX_OBJECT = "CryptoObject";
const char *CRYPTOMATTE_LAYER_PREFIX_MATERIAL = "CryptoMaterial";
const char *CRYPTOMATTE_LAYER_PREFIX_ASSET = "CryptoAsset";
const char *CRYPTOMATTE_LAYER_PREFIX_UNKNOWN = "";

const char *ntreeCompositCryptomatteLayerPrefix(const bNode *node)
{
  NodeCryptomatte *cryptoMatteSettings = (NodeCryptomatte *)node->storage;

  switch (cryptoMatteSettings->type) {
    case CMP_CRYPTOMATTE_TYPE_OBJECT:
      return CRYPTOMATTE_LAYER_PREFIX_OBJECT;

    case CMP_CRYPTOMATTE_TYPE_MATERIAL:
      return CRYPTOMATTE_LAYER_PREFIX_MATERIAL;

    case CMP_CRYPTOMATTE_TYPE_ASSET:
      return CRYPTOMATTE_LAYER_PREFIX_ASSET;
  }
  BLI_assert(false && "Invalid Cryptomatte layer.");
  return CRYPTOMATTE_LAYER_PREFIX_UNKNOWN;
}

static void node_init_cryptomatte(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeCryptomatte *user = static_cast<NodeCryptomatte *>(
      MEM_callocN(sizeof(NodeCryptomatte), __func__));
  node->storage = user;
}

static void node_init_api_cryptomatte(const bContext *C, PointerRNA *ptr)
{
  Scene *scene = CTX_data_scene(C);
  bNode *node = static_cast<bNode *>(ptr->data);
  node->id = &scene->id;
  id_us_plus(node->id);
}

static void node_free_cryptomatte(bNode *node)
{
  NodeCryptomatte *nc = static_cast<NodeCryptomatte *>(node->storage);

  if (nc) {
    BLI_freelistN(&nc->entries);
    MEM_freeN(nc);
  }
}

static void node_copy_cryptomatte(bNodeTree *UNUSED(dest_ntree),
                                  bNode *dest_node,
                                  const bNode *src_node)
{
  NodeCryptomatte *src_nc = static_cast<NodeCryptomatte *>(src_node->storage);
  NodeCryptomatte *dest_nc = static_cast<NodeCryptomatte *>(MEM_dupallocN(src_nc));

  BLI_duplicatelist(&dest_nc->entries, &src_nc->entries);
  dest_node->storage = dest_nc;
}

static bool node_poll_cryptomatte(bNodeType *UNUSED(ntype), bNodeTree *ntree)
{
  if (STREQ(ntree->idname, "CompositorNodeTree")) {
    Scene *scene;

    /* See node_composit_poll_rlayers. */
    for (scene = static_cast<Scene *>(G.main->scenes.first); scene;
         scene = static_cast<Scene *>(scene->id.next))
      if (scene->nodetree == ntree)
        break;

    return (scene != NULL);
  }
  return false;
}

void register_node_type_cmp_cryptomatte(void)
{
  static bNodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_CRYPTOMATTE, "Cryptomatte", NODE_CLASS_CONVERTOR, 0);
  node_type_socket_templates(&ntype, cmp_node_cryptomatte_in, cmp_node_cryptomatte_out);
  node_type_init(&ntype, node_init_cryptomatte);
  ntype.initfunc_api = node_init_api_cryptomatte;
  ntype.poll = node_poll_cryptomatte;
  node_type_storage(&ntype, "NodeCryptomatte", node_free_cryptomatte, node_copy_cryptomatte);
  nodeRegisterType(&ntype);
}
}
