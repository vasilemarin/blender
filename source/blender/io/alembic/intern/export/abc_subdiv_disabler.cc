#include "abc_subdiv_disabler.h"

extern "C" {
#include <stdio.h>

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"

#include "BKE_modifier.h"
}

namespace ABC {

SubdivModifierDisabler::SubdivModifierDisabler(Depsgraph *depsgraph) : depsgraph_(depsgraph)
{
}

SubdivModifierDisabler::~SubdivModifierDisabler()
{
  for (ModifierData *modifier : disabled_modifiers_) {
    modifier->mode &= ~eModifierMode_DisableTemporary;
  }
}

void SubdivModifierDisabler::disable_modifiers()
{
  Scene *scene = DEG_get_input_scene(depsgraph_);

  // This is the same iteration as is used by
  // AbstractHierarchyIterator::export_graph_construct().
  DEG_OBJECT_ITER_BEGIN (depsgraph_,
                         object_eval,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY |
                             DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    if (object_eval->type != OB_MESH) {
      continue;
    }

    Object *object_orig = DEG_get_original_object(object_eval);
    ModifierData *subdiv = get_subdiv_modifier(scene, object_orig);
    if (subdiv == nullptr) {
      continue;
    }

    subdiv->mode |= eModifierMode_DisableTemporary;
    DEG_id_tag_update(&object_orig->id, ID_RECALC_GEOMETRY);
  }
  DEG_OBJECT_ITER_END;
}

/* Check if the mesh is a subsurf, ignoring disabled modifiers and
 * displace if it's after subsurf. */
ModifierData *SubdivModifierDisabler::get_subdiv_modifier(Scene *scene, Object *ob)
{
  ModifierData *md = static_cast<ModifierData *>(ob->modifiers.last);

  for (; md; md = md->prev) {
    if (!modifier_isEnabled(scene, md, eModifierMode_Render)) {
      continue;
    }

    if (md->type == eModifierType_Subsurf) {
      SubsurfModifierData *smd = reinterpret_cast<SubsurfModifierData *>(md);

      if (smd->subdivType == ME_CC_SUBSURF) {
        return md;
      }
    }

    /* mesh is not a subsurf. break */
    if ((md->type != eModifierType_Displace) && (md->type != eModifierType_ParticleSystem)) {
      return nullptr;
    }
  }

  return nullptr;
}

}  // namespace ABC