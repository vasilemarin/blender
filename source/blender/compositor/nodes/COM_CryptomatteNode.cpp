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
 * Copyright 2018, Blender Foundation.
 */

#include "COM_CryptomatteNode.h"
#include "BKE_node.h"
#include "BLI_assert.h"
#include "BLI_hash_mm3.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "COM_ConvertOperation.h"
#include "COM_CryptomatteOperation.h"
#include "COM_MultilayerImageOperation.h"
#include "COM_RenderLayersProg.h"
#include "COM_SetAlphaMultiplyOperation.h"
#include "COM_SetColorOperation.h"
#include <iterator>
#include <string>

constexpr blender::StringRef CRYPTOMATTE_LAYER_PREFIX_OBJECT("CryptoObject");
constexpr blender::StringRef CRYPTOMATTE_LAYER_PREFIX_MATERIAL("CryptoMaterial");
constexpr blender::StringRef CRYPTOMATTE_LAYER_PREFIX_ASSET("CryptoAsset");

CryptomatteNode::CryptomatteNode(bNode *editorNode) : Node(editorNode)
{
  /* pass */
}

blender::StringRef CryptomatteNode::getCryptomatteLayerPrefix(const bNode &node) const
{
  NodeCryptomatte *cryptoMatteSettings = (NodeCryptomatte *)node.storage;

  switch (cryptoMatteSettings->type) {
    case CMP_CRYPTOMATTE_TYPE_OBJECT:
      return CRYPTOMATTE_LAYER_PREFIX_OBJECT;

    case CMP_CRYPTOMATTE_TYPE_MATERIAL:
      return CRYPTOMATTE_LAYER_PREFIX_MATERIAL;

    case CMP_CRYPTOMATTE_TYPE_ASSET:
      return CRYPTOMATTE_LAYER_PREFIX_ASSET;
  }
  BLI_assert(false && "Invalid Cryptomatte layer.");
  return "";
}

void CryptomatteNode::buildInputOperationsFromRenderSource(
    const CompositorContext &context,
    const bNode &node,
    blender::Vector<NodeOperation *> &r_input_operations) const
{
  Scene *scene = (Scene *)node.id;
  BLI_assert(GS(scene->id.name) == ID_SCE);
  Render *render = (scene) ? RE_GetSceneRender(scene) : nullptr;
  RenderResult *render_result = render ? RE_AcquireResultRead(render) : nullptr;

  if (!render_result) {
    return;
  }

  const short cryptomatte_layer_id = node.custom2;
  ViewLayer *view_layer = (ViewLayer *)BLI_findlink(&scene->view_layers, cryptomatte_layer_id);
  if (view_layer) {
    RenderLayer *render_layer = RE_GetRenderLayer(render_result, view_layer->name);
    if (render_layer) {
      std::string prefix = getCryptomatteLayerPrefix(node);
      LISTBASE_FOREACH (RenderPass *, rpass, &render_layer->passes) {
        if (blender::StringRef(rpass->name, sizeof(rpass->name)).startswith(prefix)) {
          RenderLayersProg *op = new RenderLayersProg(rpass->name, COM_DT_COLOR, rpass->channels);
          op->setScene(scene);
          op->setLayerId(cryptomatte_layer_id);
          op->setRenderData(context.getRenderData());
          op->setViewName(context.getViewName());
          r_input_operations.append(op);
        }
      }
    }
  }
  RE_ReleaseResult(render);
}

void CryptomatteNode::buildInputOperationsFromImageSource(
    const CompositorContext &context,
    const bNode &node,
    blender::Vector<NodeOperation *> &r_input_operations) const
{
  NodeCryptomatte *cryptoMatteSettings = (NodeCryptomatte *)node.storage;
  Image *image = (Image *)node.id;
  BLI_assert(!image || GS(image->id.name) == ID_IM);
  if (!image || image->type != IMA_TYPE_MULTILAYER) {
    return;
  }

  ImageUser *iuser = &cryptoMatteSettings->iuser;
  BKE_image_user_frame_calc(image, iuser, context.getFramenumber());
  ImBuf *ibuf = BKE_image_acquire_ibuf(image, iuser, NULL);

  if (image->rr) {
    int view = 0;
    if (BLI_listbase_count_at_most(&image->rr->views, 2) > 1) {
      if (iuser->view == 0) {
        /* Heuristic to match image name with scene names, check if the view name exists in the
         * image. */
        view = BLI_findstringindex(
            &image->rr->views, context.getViewName(), offsetof(RenderView, name));
        if (view == -1)
          view = 0;
      }
      else {
        view = iuser->view - 1;
      }
    }

    RenderLayer *render_layer = (RenderLayer *)BLI_findlink(&image->rr->layers, iuser->layer);
    if (render_layer) {
      int render_pass_index = 0;
      std::string prefix = getCryptomatteLayerPrefix(node);
      for (RenderPass *rpass = (RenderPass *)render_layer->passes.first; rpass;
           rpass = rpass->next, render_pass_index++) {
        if (blender::StringRef(rpass->name, sizeof(rpass->name)).startswith(prefix)) {
          MultilayerColorOperation *op = new MultilayerColorOperation(render_pass_index, view);
          op->setImage(image);
          op->setRenderLayer(render_layer);
          op->setImageUser(iuser);
          op->setFramenumber(context.getFramenumber());
          r_input_operations.append(op);
        }
      }
    }
  }
  BKE_image_release_ibuf(image, ibuf, NULL);
}

blender::Vector<NodeOperation *> CryptomatteNode::createInputOperations(
    const CompositorContext &context, const bNode &node) const
{
  blender::Vector<NodeOperation *> input_operations;
  switch (node.custom1) {
    case CMP_CRYPTOMATTE_SRC_RENDER:
      buildInputOperationsFromRenderSource(context, node, input_operations);
      break;
    case CMP_CRYPTOMATTE_SRC_IMAGE:
      buildInputOperationsFromImageSource(context, node, input_operations);
      break;
  }

  if (input_operations.is_empty()) {
    SetColorOperation *op = new SetColorOperation();
    op->setChannel1(0.0f);
    op->setChannel2(1.0f);
    op->setChannel3(0.0f);
    op->setChannel4(0.0f);
    input_operations.append(op);
  }
  return input_operations;
}

void CryptomatteNode::convertToOperations(NodeConverter &converter,
                                          const CompositorContext &context) const
{
  NodeInput *inputSocketImage = this->getInputSocket(0);
  NodeOutput *outputSocketImage = this->getOutputSocket(0);
  NodeOutput *outputSocketMatte = this->getOutputSocket(1);
  NodeOutput *outputSocketPick = this->getOutputSocket(2);

  bNode *node = this->getbNode();
  NodeCryptomatte *cryptoMatteSettings = (NodeCryptomatte *)node->storage;

  blender::Vector<NodeOperation *> input_operations = createInputOperations(context, *node);
  CryptomatteOperation *operation = new CryptomatteOperation(input_operations.size());
  LISTBASE_FOREACH (CryptomatteEntry *, cryptomatte_entry, &cryptoMatteSettings->entries) {
    operation->addObjectIndex(cryptomatte_entry->encoded_hash);
  }
  converter.addOperation(operation);
  for (int i = 0; i < input_operations.size(); ++i) {
    converter.addOperation(input_operations[i]);
    converter.addLink(input_operations[i]->getOutputSocket(), operation->getInputSocket(i));
  }

  SeparateChannelOperation *separateOperation = new SeparateChannelOperation;
  separateOperation->setChannel(3);
  converter.addOperation(separateOperation);

  SetAlphaMultiplyOperation *operationAlpha = new SetAlphaMultiplyOperation();
  converter.addOperation(operationAlpha);

  converter.addLink(operation->getOutputSocket(0), separateOperation->getInputSocket(0));
  converter.addLink(separateOperation->getOutputSocket(0), operationAlpha->getInputSocket(1));

  SetAlphaMultiplyOperation *clearAlphaOperation = new SetAlphaMultiplyOperation();
  converter.addOperation(clearAlphaOperation);
  converter.addInputValue(clearAlphaOperation->getInputSocket(1), 1.0f);

  converter.addLink(operation->getOutputSocket(0), clearAlphaOperation->getInputSocket(0));

  converter.mapInputSocket(inputSocketImage, operationAlpha->getInputSocket(0));
  converter.mapOutputSocket(outputSocketMatte, separateOperation->getOutputSocket(0));
  converter.mapOutputSocket(outputSocketImage, operationAlpha->getOutputSocket(0));
  converter.mapOutputSocket(outputSocketPick, clearAlphaOperation->getOutputSocket(0));
}
