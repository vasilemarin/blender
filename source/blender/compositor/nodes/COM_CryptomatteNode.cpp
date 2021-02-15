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

CryptomatteNode::CryptomatteNode(bNode *editorNode) : Node(editorNode)
{
  /* pass */
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

  const char *prefix = "";
  switch (cryptoMatteSettings->type) {
    case CMP_CRYPTOMATTE_TYPE_OBJECT:
      prefix = "CryptoObject";
      break;
    case CMP_CRYPTOMATTE_TYPE_MATERIAL:
      prefix = "CryptoMaterial";
      break;
    case CMP_CRYPTOMATTE_TYPE_ASSET:
      prefix = "CryptoAsset";
      break;
    default:
      BLI_assert(false);
      break;
  }

  vector<NodeOperation *> input_operations;
  if (node->custom1 == CMP_CRYPTOMATTE_SRC_RENDER) {
    Scene *scene = (Scene *)node->id;
    BLI_assert(GS(scene->id.name) == ID_SCE);
    Render *re = (scene) ? RE_GetSceneRender(scene) : NULL;

    if (re) {
      const short layerId = node->custom2;
      RenderResult *rr = RE_AcquireResultRead(re);
      if (rr) {
        ViewLayer *view_layer = (ViewLayer *)BLI_findlink(&scene->view_layers, layerId);
        if (view_layer) {
          RenderLayer *rl = RE_GetRenderLayer(rr, view_layer->name);
          if (rl) {
            LISTBASE_FOREACH (RenderPass *, rpass, &rl->passes) {
              if (STRPREFIX(rpass->name, prefix)) {
                RenderLayersProg *op = new RenderLayersProg(
                    rpass->name, COM_DT_COLOR, rpass->channels);
                op->setScene(scene);
                op->setLayerId(layerId);
                op->setRenderData(context.getRenderData());
                op->setViewName(context.getViewName());
                input_operations.push_back(op);
              }
            }
          }
        }
      }
      RE_ReleaseResult(re);
    }
  }
  else if (node->custom1 == CMP_CRYPTOMATTE_SRC_IMAGE) {
    Image *image = (Image *)node->id;
    BLI_assert(!image || GS(image->id.name) == ID_IM);
    ImageUser *iuser = &cryptoMatteSettings->iuser;
    BKE_image_user_frame_calc(image, iuser, context.getFramenumber());

    if (image && image->type == IMA_TYPE_MULTILAYER) {
      ImBuf *ibuf = BKE_image_acquire_ibuf(image, iuser, NULL);
      if (image->rr) {
        int view = 0;
        if (BLI_listbase_count_at_most(&image->rr->views, 2) > 1) {
          if (iuser->view == 0) {
            /* heuristic to match image name with scene names, check if the view name exists in the
             * image */
            view = BLI_findstringindex(
                &image->rr->views, context.getViewName(), offsetof(RenderView, name));
            if (view == -1)
              view = 0;
          }
          else {
            view = iuser->view - 1;
          }
        }

        RenderLayer *rl = (RenderLayer *)BLI_findlink(&image->rr->layers, iuser->layer);
        if (rl) {
          int passindex = 0;
          for (RenderPass *rpass = (RenderPass *)rl->passes.first; rpass;
               rpass = rpass->next, passindex++) {
            if (STRPREFIX(rpass->name, prefix)) {
              MultilayerColorOperation *op = new MultilayerColorOperation(passindex, view);
              op->setImage(image);
              op->setRenderLayer(rl);
              op->setImageUser(iuser);
              op->setFramenumber(context.getFramenumber());
              input_operations.push_back(op);
            }
          }
        }
      }
      BKE_image_release_ibuf(image, ibuf, NULL);
    }
  }

  if (input_operations.empty()) {
    SetColorOperation *op = new SetColorOperation();
    op->setChannel1(0.0f);
    op->setChannel2(1.0f);
    op->setChannel3(0.0f);
    op->setChannel4(0.0f);
    input_operations.push_back(op);
  }

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
