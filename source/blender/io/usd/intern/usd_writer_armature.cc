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
 * The Original Code is Copyright (C) 2021 Blender Foundation.
 * All rights reserved.
 */
#include "usd_writer_armature.h"
#include "usd_hierarchy_iterator.h"

#include "BKE_armature.h"
#include "DNA_armature_types.h"

#include "ED_armature.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/usd/usdSkel/skeleton.h>

#include <string>
#include <vector>

namespace {


struct BoneVisitor  {
public:
  virtual void Visit(const Bone *bone) = 0;
};

struct BoneDataBuilder : public BoneVisitor {

  std::vector<std::string> paths;

  pxr::VtArray<pxr::GfMatrix4d> bind_xforms;

  pxr::VtArray<pxr::GfMatrix4d> rest_xforms;


  void Visit(const Bone *bone) override {
    if (!bone) {
      return;
    }
    paths.push_back(build_path(bone));

    //pxr::GfMatrix4f rest4f(bone->arm_mat);
    //rest_xforms.push_back(pxr::GfMatrix4d(rest4f));

  }

  std::string build_path(const Bone *bone) {
    std::string path(pxr::TfMakeValidIdentifier(bone->name));

    const Bone *parent = bone->parent;
    while (parent) {
      path = pxr::TfMakeValidIdentifier(parent->name) + std::string("/") + path;
      parent = parent->parent;
    }

    return path;
  }

};


} // End anonymous namespace


namespace blender::io::usd {

static void visit_bones(const Bone *bone, BoneVisitor *visitor)
{
  if (!(bone && visitor)) {
    return;
  }

  visitor->Visit(bone);

  for (Bone *child = (Bone *)bone->childbase.first; child; child = child->next) {
    visit_bones(child, visitor);
  }
}

static void visit_bones(const Object *ob_arm, BoneVisitor *visitor)
{
  bArmature *armature = (bArmature *)ob_arm->data;

  for (Bone *bone = (Bone *)armature->bonebase.first; bone; bone = bone->next) {
    visit_bones(bone, visitor);
  }
}


USDArmatureWriter::USDArmatureWriter(const USDExporterContext &ctx) : USDAbstractWriter(ctx)
{
}

void USDArmatureWriter::do_write(HierarchyContext &context)
{
  if (!context.object) {
    printf("WARNING in USDArmatureWriter::do_write: null object\n");
    return;
  }

  if (context.object->type != OB_ARMATURE) {
    printf("WARNING in USDArmatureWriter::do_write: object is not an armature\n");
    return;
  }

  if (context.object->data == nullptr) {
    printf("WARNING in USDArmatureWriter::do_write: null object data\n");
    return;
  }

  pxr::UsdStageRefPtr stage = usd_export_context_.stage;
  pxr::UsdTimeCode timecode = get_export_time_code();

  pxr::UsdSkelSkeleton usd_skel = (usd_export_context_.export_params.export_as_overs) ?
    pxr::UsdSkelSkeleton(usd_export_context_.stage->OverridePrim(
      usd_export_context_.usd_path)) :
    pxr::UsdSkelSkeleton::Define(usd_export_context_.stage,
      usd_export_context_.usd_path);

  if (!usd_skel) {
    printf("WARNING: Couldn't define Skeleton %s\n", usd_export_context_.usd_path.GetString().c_str());
  }

  printf("Defined Skeleton %s\n", usd_export_context_.usd_path.GetString().c_str());

  if (!this->frame_has_been_written_) {

    BoneDataBuilder bone_data;
    visit_bones(context.object, &bone_data);

    if (!bone_data.paths.empty()) {
      pxr::VtTokenArray joints(bone_data.paths.size());

      for (int i = 0; i < bone_data.paths.size(); ++i) {
        joints[i] = pxr::TfToken(bone_data.paths[i]);
      }

      usd_skel.GetJointsAttr().Set(joints);
    }


    //usd_skel.GetRestTransformsAttr().Set(bone_data.rest_xforms);
  }

  /* TODO: Right now there is a remote possibility that the SkelAnimation path will clash
   * with the USD path for another object in the scene.  Look into extending USDHierarchyIterator
   * with a function that will provide a USD path that's guranteed to be unique (e.g., by
   * examining paths of all the writers in the writer map.  The USDHierarchyIterator
   * can be accessed for such a query like this: 
   * this->usd_export_context_.hierarchy_iterator */

}

}  // namespace blender::io::usd
