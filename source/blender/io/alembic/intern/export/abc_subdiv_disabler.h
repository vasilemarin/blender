#pragma once

#include <set>

struct Depsgraph;
struct ModifierData;
struct Object;
struct Scene;

namespace ABC {

/**
 * Temporarily all subdivision modifiers on mesh objects.
 * The destructor restores all disabled modifiers.
 *
 * This is used to export unsubdivided meshes to Alembic. It is done in a separate step before the
 * exporter starts iterating over all the frames, so that it only has to happen once per export.
 */
class SubdivModifierDisabler {
 private:
  Depsgraph *depsgraph_;
  std::set<ModifierData *> disabled_modifiers_;

 public:
  SubdivModifierDisabler(Depsgraph *depsgraph);
  ~SubdivModifierDisabler();

  void disable_modifiers();

  static ModifierData *get_subdiv_modifier(Scene *scene, Object *ob);
};

}  // namespace ABC
