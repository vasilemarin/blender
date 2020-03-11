/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "render/colorspace.h"
#include "render/mesh.h"
#include "render/object.h"

#include "blender/blender_sync.h"
#include "blender/blender_util.h"

CCL_NAMESPACE_BEGIN

static void sync_smoke_volume(Scene *scene, BL::Object &b_ob, Mesh *mesh, float frame)
{
  BL::FluidDomainSettings b_domain = object_fluid_gas_domain_find(b_ob);
  if (!b_domain) {
    return;
  }

  ImageManager *image_manager = scene->image_manager;
  AttributeStandard attributes[] = {ATTR_STD_VOLUME_DENSITY,
                                    ATTR_STD_VOLUME_COLOR,
                                    ATTR_STD_VOLUME_FLAME,
                                    ATTR_STD_VOLUME_HEAT,
                                    ATTR_STD_VOLUME_TEMPERATURE,
                                    ATTR_STD_VOLUME_VELOCITY,
                                    ATTR_STD_NONE};

  for (int i = 0; attributes[i] != ATTR_STD_NONE; i++) {
    AttributeStandard std = attributes[i];
    if (!mesh->need_attribute(scene, std)) {
      continue;
    }

    mesh->volume_isovalue = b_domain.clipping();

    Attribute *attr = mesh->attributes.add(std);
    VoxelAttribute *volume_data = attr->data_voxel();
    ImageMetaData metadata;

    ImageKey key;
    key.filename = Attribute::standard_name(std);
    key.builtin_data = b_ob.ptr.data;

    volume_data->manager = image_manager;
    volume_data->slot = image_manager->add_image(key, frame, metadata);
  }
}

static void sync_volume_object(BL::BlendData &b_data, BL::Object &b_ob, Scene *scene, Mesh *mesh)
{
  BL::Volume b_volume(b_ob.data());
  b_volume.grids.load(b_data.ptr.data);

  mesh->volume_isovalue = 1e-3f; /* TODO: make user setting. */

  /* Find grid with matching name. */
  BL::Volume::grids_iterator b_grid_iter;
  for (b_volume.grids.begin(b_grid_iter); b_grid_iter != b_volume.grids.end(); ++b_grid_iter) {
    BL::VolumeGrid b_grid = *b_grid_iter;
    ustring name = ustring(b_grid.name());
    AttributeStandard std = ATTR_STD_NONE;

    if (name == Attribute::standard_name(ATTR_STD_VOLUME_DENSITY)) {
      std = ATTR_STD_VOLUME_DENSITY;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_COLOR)) {
      std = ATTR_STD_VOLUME_COLOR;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_FLAME)) {
      std = ATTR_STD_VOLUME_FLAME;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_HEAT)) {
      std = ATTR_STD_VOLUME_HEAT;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_TEMPERATURE)) {
      std = ATTR_STD_VOLUME_TEMPERATURE;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_VELOCITY)) {
      std = ATTR_STD_VOLUME_VELOCITY;
    }

    if ((std != ATTR_STD_NONE && mesh->need_attribute(scene, std)) ||
        mesh->need_attribute(scene, name)) {
      Attribute *attr = (std != ATTR_STD_NONE) ?
                            mesh->attributes.add(std) :
                            mesh->attributes.add(name, TypeDesc::TypeFloat, ATTR_ELEMENT_VOXEL);
      VoxelAttribute *volume_data = attr->data_voxel();
      ImageMetaData metadata;
      const float frame = b_volume.grids.frame();

      ImageKey key;
      key.filename = name.c_str();
      key.builtin_data = b_volume.ptr.data;

      volume_data->manager = scene->image_manager;
      volume_data->slot = scene->image_manager->add_image(key, frame, metadata);
    }
  }
}

void BlenderSync::sync_volume(BL::Object &b_ob, Mesh *mesh, const vector<Shader *> &used_shaders)
{
  bool old_has_voxel_attributes = mesh->has_voxel_attributes();

  mesh->clear();
  mesh->used_shaders = used_shaders;

  if (view_layer.use_volumes) {
    if (b_ob.type() == BL::Object::type_VOLUME) {
      /* Volume object. Create only attributes, bounding mesh will then
       * be automatically generated later. */
      sync_volume_object(b_data, b_ob, scene, mesh);
    }
    else {
      /* Smoke domain. */
      sync_smoke_volume(scene, b_ob, mesh, b_scene.frame_current());
    }
  }

  /* Tag update. */
  bool rebuild = (old_has_voxel_attributes != mesh->has_voxel_attributes());
  mesh->tag_update(scene, rebuild);
}

CCL_NAMESPACE_END
