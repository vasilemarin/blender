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
#include "BLI_string_ref.hh"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_cryptomatte.hh"
#include "BKE_global.h"
#include "BKE_lib_id.h"
#include "BKE_library.h"
#include "BKE_main.h"

static CryptomatteSession *cryptomatte_init_from_node(const bNode &node, int frame_number)
{
  if (node.type != CMP_NODE_CRYPTOMATTE) {
    return nullptr;
  }

  NodeCryptomatte *node_cryptomatte = static_cast<NodeCryptomatte *>(node.storage);
  CryptomatteSession *session = nullptr;
  switch (node.custom1) {
    case CMP_CRYPTOMATTE_SRC_RENDER: {
      Scene *scene = (Scene *)node.id;
      BLI_assert(GS(scene->id.name) == ID_SCE);
      Render *render = (scene) ? RE_GetSceneRender(scene) : nullptr;
      RenderResult *render_result = render ? RE_AcquireResultRead(render) : nullptr;
      if (render_result) {
        session = BKE_cryptomatte_init_from_render_result(render_result);
      }
      if (render) {
        RE_ReleaseResult(render);
      }
      break;
    }

    case CMP_CRYPTOMATTE_SRC_IMAGE: {
      Image *image = (Image *)node.id;
      BLI_assert(!image || GS(image->id.name) == ID_IM);
      if (!image || image->type != IMA_TYPE_MULTILAYER) {
        break;
      }

      ImageUser *iuser = &node_cryptomatte->iuser;
      BKE_image_user_frame_calc(image, iuser, frame_number);
      ImBuf *ibuf = BKE_image_acquire_ibuf(image, iuser, NULL);
      RenderResult *render_result = image->rr;
      if (render_result) {
        session = BKE_cryptomatte_init_from_render_result(render_result);
      }
      BKE_image_release_ibuf(image, ibuf, NULL);
      break;
    }
  }
  return session;
}

extern "C" {
static CryptomatteEntry *cryptomatte_find(const NodeCryptomatte &n, float encoded_hash)
{
  LISTBASE_FOREACH (CryptomatteEntry *, entry, &n.entries) {
    if (entry->encoded_hash == encoded_hash) {
      return entry;
    }
  }
  return nullptr;
}

static void cryptomatte_add(bNode &node, NodeCryptomatte &node_cryptomatte, float encoded_hash)
{
  /* Check if entry already exist. */
  if (cryptomatte_find(node_cryptomatte, encoded_hash)) {
    return;
  }

  CryptomatteEntry *entry = static_cast<CryptomatteEntry *>(
      MEM_callocN(sizeof(CryptomatteEntry), __func__));
  entry->encoded_hash = encoded_hash;
  /* TODO(jbakker): Get current frame from scene. */
  CryptomatteSession *session = cryptomatte_init_from_node(node, 0);
  if (session) {
    BKE_cryptomatte_find_name(session, encoded_hash, entry->name, sizeof(entry->name));
    BKE_cryptomatte_free(session);
  }

  BLI_addtail(&node_cryptomatte.entries, entry);
}

static void cryptomatte_remove(NodeCryptomatte &n, float encoded_hash)
{
  CryptomatteEntry *entry = cryptomatte_find(n, encoded_hash);
  if (!entry) {
    return;
  }
  BLI_remlink(&n.entries, entry);
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

void ntreeCompositCryptomatteSyncFromAdd(bNode *node)
{
  NodeCryptomatte *n = static_cast<NodeCryptomatte *>(node->storage);
  if (n->runtime.add[0] != 0.0f) {
    cryptomatte_add(*node, *n, n->runtime.add[0]);
    zero_v3(n->runtime.add);
  }
}

void ntreeCompositCryptomatteSyncFromRemove(bNode *node)
{
  NodeCryptomatte *n = static_cast<NodeCryptomatte *>(node->storage);
  if (n->runtime.remove[0] != 0.0f) {
    cryptomatte_remove(*n, n->runtime.remove[0]);
    zero_v3(n->runtime.remove);
  }
}
void ntreeCompositCryptomatteUpdateLayerNames(bNode *node)
{
  NodeCryptomatte *n = static_cast<NodeCryptomatte *>(node->storage);
  BLI_freelistN(&n->runtime.layers);

  CryptomatteSession *session = cryptomatte_init_from_node(*node, 0);

  if (session) {
    for (blender::StringRef layer_name :
         blender::bke::cryptomatte::BKE_cryptomatte_layer_names_get(*session)) {
      CryptomatteLayer *layer = static_cast<CryptomatteLayer *>(
          MEM_callocN(sizeof(CryptomatteLayer), __func__));
      layer_name.copy(layer->name);
      BLI_addtail(&n->runtime.layers, layer);
    }

    BKE_cryptomatte_free(session);
  }
}

const char *ntreeCompositCryptomatteLayerPrefix(const bNode *node)
{
  NodeCryptomatte *node_cryptomatte = (NodeCryptomatte *)node->storage;
  return node_cryptomatte->layer_name;
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
