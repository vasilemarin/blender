

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
 * The Original Code is Copyright (C) 2020 Blender Foundation
 * All rights reserved.
 */

/** \file
 * \ingroup bgpencil
 */
#include <algorithm>
#include <string>

#include "DNA_vec_types.h"

#include "BKE_camera.h"
#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_layer.h"
#include "BKE_main.h"
#include "BKE_material.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_span.hh"

#include "DNA_gpencil_types.h"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "UI_view2d.h"

#include "ED_view3d.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "gpencil_io_base.h"

using blender::Span;

namespace blender::io::gpencil {

/* Constructor. */
GpencilIO::GpencilIO(const GpencilIOParams *iparams)
{
  params_.frame_start = iparams->frame_start;
  params_.frame_end = iparams->frame_end;
  params_.frame_cur = iparams->frame_cur;
  params_.ob = iparams->ob;
  params_.region = iparams->region;
  params_.v3d = iparams->v3d;
  params_.C = iparams->C;
  params_.mode = iparams->mode;
  params_.flag = iparams->flag;
  params_.select_mode = iparams->select_mode;
  params_.frame_mode = iparams->frame_mode;
  params_.stroke_sample = iparams->stroke_sample;
  params_.resolution = iparams->resolution;
  params_.scale = iparams->scale;

  /* Easy access data. */
  bmain_ = CTX_data_main(params_.C);
  depsgraph_ = CTX_data_depsgraph_pointer(params_.C);
  scene_ = CTX_data_scene(params_.C);
  rv3d_ = (RegionView3D *)params_.region->regiondata;
  gpd_ = (params_.ob != NULL) ? (bGPdata *)params_.ob->data : nullptr;
  cfra_ = iparams->frame_cur;

  /* Calculate camera matrix. */
  Object *cam_ob = params_.v3d->camera;
  if (cam_ob != NULL) {
    RenderData *rd = &scene_->r;
    CameraParams params;

    /* Setup parameters. */
    BKE_camera_params_init(&params);
    BKE_camera_params_from_object(&params, cam_ob);

    /* Compute matrix, viewplane, .. */
    BKE_camera_params_compute_viewplane(&params, rd->xsch, rd->ysch, rd->xasp, rd->yasp);
    BKE_camera_params_compute_matrix(&params);

    float viewmat[4][4];
    invert_m4_m4(viewmat, cam_ob->obmat);

    mul_m4_m4m4(persmat_, params.winmat, viewmat);
  }
  else {
    unit_m4(persmat_);
  }

  /* Load list of selected objects. */
  create_object_list();

  winx_ = params_.region->winx;
  winy_ = params_.region->winy;

  /* Camera rectangle. */
  if (rv3d_->persp == RV3D_CAMOB) {
    render_x_ = (scene_->r.xsch * scene_->r.size) / 100;
    render_y_ = (scene_->r.ysch * scene_->r.size) / 100;

    ED_view3d_calc_camera_border(CTX_data_scene(params_.C),
                                 depsgraph_,
                                 params_.region,
                                 params_.v3d,
                                 rv3d_,
                                 &camera_rect_,
                                 true);
    is_camera_ = true;
    camera_ratio_ = render_x_ / (camera_rect_.xmax - camera_rect_.xmin);
    offset_.x = camera_rect_.xmin;
    offset_.y = camera_rect_.ymin;
  }
  else {
    is_camera_ = false;
    /* Calc selected object boundbox. Need set initial value to some variables. */
    camera_ratio_ = 1.0f;
    offset_.x = 0.0f;
    offset_.y = 0.0f;

    selected_objects_boundbox_calc();
    rctf boundbox;
    selected_objects_boundbox_get(&boundbox);

    render_x_ = boundbox.xmax - boundbox.xmin;
    render_y_ = boundbox.ymax - boundbox.ymin;
    offset_.x = boundbox.xmin;
    offset_.y = boundbox.ymin;
  }
}

/** Create a list of selected objects sorted from back to front */
void GpencilIO::create_object_list()
{
  ViewLayer *view_layer = CTX_data_view_layer(params_.C);

  float camera_z_axis[3];
  copy_v3_v3(camera_z_axis, rv3d_->viewinv[2]);
  ob_list_.clear();

  LISTBASE_FOREACH (Base *, base, &view_layer->object_bases) {
    Object *object = base->object;

    if (object->type != OB_GPENCIL) {
      continue;
    }
    if ((params_.select_mode == GP_EXPORT_ACTIVE) && (params_.ob != object)) {
      continue;
    }

    if ((params_.select_mode == GP_EXPORT_SELECTED) && ((base->flag & BASE_SELECTED) == 0)) {
      continue;
    }

    /* Save z-depth from view to sort from back to front. */
    if (is_camera_) {
      float camera_z = dot_v3v3(camera_z_axis, object->obmat[3]);
      ObjectZ obz = {camera_z, object};
      ob_list_.append(obz);
    }
    else {
      float zdepth = 0;
      if (rv3d_) {
        if (rv3d_->is_persp) {
          zdepth = ED_view3d_calc_zfac(rv3d_, object->obmat[3], nullptr);
        }
        else {
          zdepth = -dot_v3v3(rv3d_->viewinv[2], object->obmat[3]);
        }
        ObjectZ obz = {zdepth * -1.0f, object};
        ob_list_.append(obz);
      }
    }
  }
  /* Sort list of objects from point of view. */
  std::sort(ob_list_.begin(), ob_list_.end(), [](const ObjectZ &obz1, const ObjectZ &obz2) {
    return obz1.zdepth < obz2.zdepth;
  });
}

/**
 * Set file input_text full path.
 * \param filename: Path of the file provided by save dialog.
 */
void GpencilIO::filename_set(const char *filename)
{
  BLI_strncpy(filename_, filename, FILE_MAX);
  BLI_path_abs(filename_, BKE_main_blendfile_path(bmain_));
}

/** Convert to screenspace. */
bool GpencilIO::gpencil_3d_point_to_screen_space(const float co[3], float r_co[2])
{
  float parent_co[3];
  mul_v3_m4v3(parent_co, diff_mat_, co);
  float screen_co[2];
  eV3DProjTest test = (eV3DProjTest)(V3D_PROJ_RET_OK);
  if (ED_view3d_project_float_global(params_.region, parent_co, screen_co, test) ==
      V3D_PROJ_RET_OK) {
    if (!ELEM(V2D_IS_CLIPPED, screen_co[0], screen_co[1])) {
      copy_v2_v2(r_co, screen_co);
      /* Invert X axis. */
      if (invert_axis_[0]) {
        r_co[0] = winx_ - r_co[0];
      }
      /* Invert Y axis. */
      if (invert_axis_[1]) {
        r_co[1] = winy_ - r_co[1];
      }
      /* Apply offset and scale. */
      sub_v2_v2(r_co, &offset_.x);
      mul_v2_fl(r_co, camera_ratio_);

      return true;
    }
  }
  r_co[0] = V2D_IS_CLIPPED;
  r_co[1] = V2D_IS_CLIPPED;

  /* Invert X axis. */
  if (invert_axis_[0]) {
    r_co[0] = winx_ - r_co[0];
  }
  /* Invert Y axis. */
  if (invert_axis_[1]) {
    r_co[1] = winy_ - r_co[1];
  }

  return false;
}

/** Convert to render space. */
void GpencilIO::gpencil_3d_point_to_render_space(const float co[3], float r_co[2])
{
  float parent_co[3];
  mul_v3_m4v3(parent_co, diff_mat_, co);
  mul_m4_v3(persmat_, parent_co);

  parent_co[0] = parent_co[0] / max_ff(FLT_MIN, parent_co[2]);
  parent_co[1] = parent_co[1] / max_ff(FLT_MIN, parent_co[2]);

  r_co[0] = (parent_co[0] + 1.0f) / 2.0f * (float)render_x_;
  r_co[1] = (parent_co[1] + 1.0f) / 2.0f * (float)render_y_;

  /* Invert X axis. */
  if (invert_axis_[0]) {
    r_co[0] = (float)render_x_ - r_co[0];
  }
  /* Invert Y axis. */
  if (invert_axis_[1]) {
    r_co[1] = (float)render_y_ - r_co[1];
  }
}

/** Convert to 2D. */
void GpencilIO::gpencil_3d_point_to_2D(const float co[3], float r_co[2])
{
  const bool is_camera = (bool)(rv3d_->persp == RV3D_CAMOB);
  if (is_camera) {
    gpencil_3d_point_to_render_space(co, r_co);
  }
  else {
    gpencil_3d_point_to_screen_space(co, r_co);
  }
}

/** Get radius of point. */
float GpencilIO::stroke_point_radius_get(bGPDlayer *gpl, struct bGPDstroke *gps)
{
  float v1[2], screen_co[2], screen_ex[2];

  bGPDspoint *pt = &gps->points[0];
  gpencil_3d_point_to_2D(&pt->x, screen_co);

  /* Radius. */
  bGPDstroke *gps_perimeter = BKE_gpencil_stroke_perimeter_from_view(
      rv3d_, gpd_, gpl, gps, 3, diff_mat_);

  pt = &gps_perimeter->points[0];
  gpencil_3d_point_to_2D(&pt->x, screen_ex);

  sub_v2_v2v2(v1, screen_co, screen_ex);
  float radius = len_v2(v1);
  BKE_gpencil_free_stroke(gps_perimeter);

  return MAX2(radius, 1.0f);
}

void GpencilIO::gpl_prepare_export_matrix(struct Object *ob, struct bGPDlayer *gpl)
{
  BKE_gpencil_layer_transform_matrix_get(depsgraph_, ob, gpl, diff_mat_);
  mul_m4_m4m4(diff_mat_, diff_mat_, gpl->layer_invmat);
}

void GpencilIO::gps_prepare_export_colors(struct Object *ob, struct bGPDstroke *gps)
{
  MaterialGPencilStyle *gp_style = BKE_gpencil_material_settings(ob, gps->mat_nr + 1);

  /* Stroke color. */
  copy_v4_v4(stroke_color_, gp_style->stroke_rgba);
  avg_opacity_ = 0;
  /* Get average vertex color and apply. */
  float avg_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  for (const bGPDspoint &pt : Span(gps->points, gps->totpoints)) {
    add_v4_v4(avg_color, pt.vert_color);
    avg_opacity_ += pt.strength;
  }

  mul_v4_v4fl(avg_color, avg_color, 1.0f / (float)gps->totpoints);
  interp_v3_v3v3(stroke_color_, stroke_color_, avg_color, avg_color[3]);
  avg_opacity_ /= (float)gps->totpoints;

  /* Fill color. */
  copy_v4_v4(fill_color_, gp_style->fill_rgba);
  /* Apply vertex color for fill. */
  interp_v3_v3v3(fill_color_, fill_color_, gps->vert_color_fill, gps->vert_color_fill[3]);
}

float GpencilIO::stroke_average_opacity_get()
{
  return avg_opacity_;
}

bool GpencilIO::is_camera_mode()
{
  return is_camera_;
}

/* Calc selected strokes boundbox. */
void GpencilIO::selected_objects_boundbox_calc()
{
  const float gap = 10.0f;
  const bGPDspoint *pt;
  int32_t i;

  float screen_co[2];
  float r_min[2], r_max[2];
  INIT_MINMAX2(r_min, r_max);

  for (ObjectZ &obz : ob_list_) {
    Object *ob = obz.ob;
    /* Use evaluated version to get strokes with modifiers. */
    Object *ob_eval = (Object *)DEG_get_evaluated_id(depsgraph_, &ob->id);
    bGPdata *gpd_eval = (bGPdata *)ob_eval->data;

    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd_eval->layers) {
      if (gpl->flag & GP_LAYER_HIDE) {
        continue;
      }
      BKE_gpencil_layer_transform_matrix_get(depsgraph_, ob_eval, gpl, diff_mat_);

      bGPDframe *gpf = gpl->actframe;
      if (gpf == nullptr) {
        continue;
      }

      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        if (gps->totpoints == 0) {
          continue;
        }
        for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
          /* Convert to 2D. */
          gpencil_3d_point_to_2D(&pt->x, screen_co);
          minmax_v2v2_v2(r_min, r_max, screen_co);
        }
      }
    }
  }
  /* Add small gap. */
  add_v2_fl(r_min, gap * -1.0f);
  add_v2_fl(r_max, gap);

  select_boundbox_.xmin = r_min[0];
  select_boundbox_.ymin = r_min[1];
  select_boundbox_.xmax = r_max[0];
  select_boundbox_.ymax = r_max[1];
}

void GpencilIO::selected_objects_boundbox_get(rctf *boundbox)
{
  boundbox->xmin = select_boundbox_.xmin;
  boundbox->xmax = select_boundbox_.xmax;
  boundbox->ymin = select_boundbox_.ymin;
  boundbox->ymax = select_boundbox_.ymax;
}

void GpencilIO::frame_number_set(const int value)
{
  cfra_ = value;
}

}  // namespace blender::io::gpencil
