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

static CryptomatteEntry *cryptomatte_find(NodeCryptomatte *n, float encoded_hash)
{
  LISTBASE_FOREACH (CryptomatteEntry *, entry, &n->entries) {
    if (entry->encoded_hash == encoded_hash) {
      return entry;
    }
  }
  return NULL;
}

static void cryptomatte_add(Main *bmain, NodeCryptomatte *n, float encoded_hash)
{
  /* Check if entry already exist. */
  if (cryptomatte_find(n, encoded_hash) != NULL) {
    return;
  }

  const char *name = NULL;
  const ID *matching_id = BKE_cryptomatte_find_id(bmain, encoded_hash);
  if (matching_id != NULL) {
    name = matching_id->name + 2;
  }

  CryptomatteEntry *entry = MEM_callocN(sizeof(CryptomatteEntry), __func__);
  entry->encoded_hash = encoded_hash;
  if (name != NULL) {
    BLI_strncpy(entry->name, name, sizeof(entry->name));
  }
  BLI_addtail(&n->entries, entry);
}

static void cryptomatte_remove(NodeCryptomatte *n, float encoded_hash)
{
  CryptomatteEntry *entry = cryptomatte_find(n, encoded_hash);
  if (entry == NULL) {
    return;
  }

  BLI_remlink(&n->entries, entry);
  MEM_freeN(entry);
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
  NodeCryptomatte *n = node->storage;
  if (n->add[0] != 0.0f) {
    cryptomatte_add(bmain, n, n->add[0]);
    n->add[0] = 0.0f;
    n->add[1] = 0.0f;
    n->add[2] = 0.0f;
  }
}

void ntreeCompositCryptomatteSyncFromRemove(Main *UNUSED(bmain),
                                            bNodeTree *UNUSED(ntree),
                                            bNode *node)
{
  NodeCryptomatte *n = node->storage;
  if (n->remove[0] != 0.0f) {
    cryptomatte_remove(n, n->remove[0]);
    n->remove[0] = 0.0f;
    n->remove[1] = 0.0f;
    n->remove[2] = 0.0f;
  }
}

static void node_init_cryptomatte(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeCryptomatte *user = MEM_callocN(sizeof(NodeCryptomatte), "cryptomatte user");
  node->storage = user;
}

static void node_init_api_cryptomatte(const bContext *C, PointerRNA *ptr)
{
  Scene *scene = CTX_data_scene(C);
  bNode *node = ptr->data;
  node->id = &scene->id;
  id_us_plus(node->id);
}

static void node_free_cryptomatte(bNode *node)
{
  NodeCryptomatte *nc = node->storage;

  if (nc) {
    BLI_freelistN(&nc->entries);
    MEM_freeN(nc);
  }
}

static void node_copy_cryptomatte(bNodeTree *UNUSED(dest_ntree),
                                  bNode *dest_node,
                                  const bNode *src_node)
{
  NodeCryptomatte *src_nc = src_node->storage;
  NodeCryptomatte *dest_nc = MEM_dupallocN(src_nc);
  BLI_duplicatelist(&dest_nc->entries, &src_nc->entries);
  dest_node->storage = dest_nc;
}

static bool node_poll_cryptomatte(bNodeType *UNUSED(ntype), bNodeTree *ntree)
{
  if (STREQ(ntree->idname, "CompositorNodeTree")) {
    Scene *scene;

    /* See node_composit_poll_rlayers. */
    for (scene = G.main->scenes.first; scene; scene = scene->id.next)
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
