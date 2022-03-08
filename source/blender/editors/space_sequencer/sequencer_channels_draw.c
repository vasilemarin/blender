/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup sequencer
 */

#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BKE_context.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "ED_screen.h"

#include "GPU_framebuffer.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_matrix.h"
#include "GPU_state.h"
#include "GPU_vertex_buffer.h"
#include "GPU_viewport.h"

#include "RNA_access.h"

#include "SEQ_channels.h"
#include "SEQ_sequencer.h"
#include "SEQ_time.h"

#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "WM_api.h"

/* Own include. */
#include "sequencer_intern.h"

#define ICON_WIDTH (U.widget_unit * 0.8)

/* Similar to `UI_view2d_sync()` but converts values to pixelspace. */
static void sync_channel_header_area(SeqChannelDrawContext *context)
{
  LISTBASE_FOREACH (ARegion *, region, &context->area->regionbase) {
    View2D *v2d_other = &region->v2d;

    /* don't operate on self */
    if (context->v2d != v2d_other && region->regiontype == RGN_TYPE_WINDOW) {
      context->v2d->cur.ymin = v2d_other->cur.ymin * context->channel_height;
      context->v2d->cur.ymax = v2d_other->cur.ymax * context->channel_height;
      /* region possibly changed, so refresh */
      ED_region_tag_redraw_no_rebuild(region);
    }
  }
}

static float channel_height_pixelspace_get(ScrArea *area)
{
  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    if (region->regiontype == RGN_TYPE_WINDOW) {
      return UI_view2d_view_to_region_y(&region->v2d, 1.0f) -
             UI_view2d_view_to_region_y(&region->v2d, 0.0f);
    }
  }

  BLI_assert_unreachable();
  return 1.0f;
}

static float frame_width_pixelspace_get(ScrArea *area)
{
  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    if (region->regiontype == RGN_TYPE_WINDOW) {
      return UI_view2d_view_to_region_x(&region->v2d, 1.0f) -
             UI_view2d_view_to_region_x(&region->v2d, 0.0f);
    }
  }

  BLI_assert_unreachable();
  return 1.0f;
}

static float widget_y_offset(SeqChannelDrawContext *context)
{
  return context->channel_height / 2 - ICON_WIDTH / 2;
}

static float channel_index_y_min(SeqChannelDrawContext *context, const int index)
{
  return index * context->channel_height;
}

static void displayed_channel_range_get(SeqChannelDrawContext *context, int channel_range[2])
{
  /* Channel 0 is not usable, so should never be drawn. */
  channel_range[0] = max_ii(1, floor(context->timeline_region_v2d->cur.ymin));
  channel_range[1] = ceil(context->timeline_region_v2d->cur.ymax);

  rctf strip_boundbox;
  BLI_rctf_init(&strip_boundbox, 0.0f, 0.0f, 1.0f, MAXSEQ);
  SEQ_timeline_expand_boundbox(context->seqbase, &strip_boundbox);
  CLAMP(channel_range[0], strip_boundbox.ymin, strip_boundbox.ymax);
  CLAMP(channel_range[1], strip_boundbox.ymin, strip_boundbox.ymax);
}

static float draw_channel_widget_hide(SeqChannelDrawContext *context,
                                      uiBlock *block,
                                      int channel_index,
                                      float offset)
{
  const float y = channel_index_y_min(context, channel_index) + widget_y_offset(context);
  const float width = ICON_WIDTH;
  SeqTimelineChannel *channel = SEQ_channel_get_by_index(context->channels, channel_index);
  const int icon = SEQ_channel_is_muted(channel) ? ICON_CHECKBOX_DEHLT : ICON_CHECKBOX_HLT;
  const char *tooltip = SEQ_channel_is_muted(channel) ? "Unmute channel" : "Mute channel";

  PointerRNA ptr;
  RNA_pointer_create(&context->scene->id, &RNA_SequenceTimelineChannel, channel, &ptr);
  PropertyRNA *hide_prop = RNA_struct_type_find_property(&RNA_SequenceTimelineChannel, "mute");

  UI_block_emboss_set(block, UI_EMBOSS_NONE);
  uiDefIconButR_prop(block,
                     UI_BTYPE_TOGGLE,
                     1,
                     icon,
                     context->v2d->cur.xmax - offset,
                     y,
                     ICON_WIDTH,
                     ICON_WIDTH,
                     &ptr,
                     hide_prop,
                     0,
                     0,
                     0,
                     0,
                     0,
                     tooltip);

  return width;
}

static float draw_channel_widget_lock(SeqChannelDrawContext *context,
                                      uiBlock *block,
                                      int channel_index,
                                      float offset)
{

  const float y = channel_index_y_min(context, channel_index) + widget_y_offset(context);
  const float width = ICON_WIDTH;

  SeqTimelineChannel *channel = SEQ_channel_get_by_index(context->channels, channel_index);
  const int icon = SEQ_channel_is_locked(channel) ? ICON_LOCKED : ICON_UNLOCKED;
  const char *tooltip = SEQ_channel_is_muted(channel) ? "Unlock channel" : "lock channel";

  PointerRNA ptr;
  RNA_pointer_create(&context->scene->id, &RNA_SequenceTimelineChannel, channel, &ptr);
  PropertyRNA *hide_prop = RNA_struct_type_find_property(&RNA_SequenceTimelineChannel, "lock");

  UI_block_emboss_set(block, UI_EMBOSS_NONE);
  uiDefIconButR_prop(block,
                     UI_BTYPE_TOGGLE,
                     1,
                     icon,
                     context->v2d->cur.xmax - offset,
                     y,
                     ICON_WIDTH,
                     ICON_WIDTH,
                     &ptr,
                     hide_prop,
                     0,
                     0,
                     0,
                     0,
                     0,
                     tooltip);

  return width;
}

static bool channel_is_being_renamed(SpaceSeq *sseq, int channel_index)
{
  return sseq->runtime.rename_channel_index == channel_index;
}

static float text_size_get(SeqChannelDrawContext *context)
{
  return 20 * U.dpi_fac;  // XXX
}

/* Todo: decide what gets priority - label or buttons */
static void label_rect_init(SeqChannelDrawContext *context,
                            int channel_index,
                            float used_width,
                            rctf *r_rect)
{
  float text_size = text_size_get(context);
  float margin = (context->channel_height - text_size) / 2.0f;
  float y = channel_index_y_min(context, channel_index) + margin;

  float margin_x = ICON_WIDTH * 0.65;
  float width = max_ff(0.0f, context->v2d->cur.xmax - used_width);

  /* Text input has own margin. Prevent text jumping around and use as much space as possible. */
  if (channel_is_being_renamed(CTX_wm_space_seq(context->C), channel_index)) {
    float input_box_margin = ICON_WIDTH * 0.5f;
    margin_x -= input_box_margin;
    width += input_box_margin;
  }

  BLI_rctf_init(r_rect, margin_x, margin_x + width, y, y + text_size);
}

static void draw_channel_labels(SeqChannelDrawContext *context,
                                uiBlock *block,
                                int channel_index,
                                float used_width)
{
  SpaceSeq *sseq = CTX_wm_space_seq(context->C);
  rctf rect;
  label_rect_init(context, channel_index, used_width, &rect);

  if (channel_is_being_renamed(sseq, channel_index)) {
    SeqTimelineChannel *channel = SEQ_channel_get_by_index(context->channels, channel_index);
    PointerRNA ptr = {NULL};
    RNA_pointer_create(&context->scene->id, &RNA_SequenceTimelineChannel, channel, &ptr);
    PropertyRNA *prop = RNA_struct_name_property(ptr.type);

    UI_block_emboss_set(block, UI_EMBOSS);
    uiBut *but = uiDefButR(block,
                           UI_BTYPE_TEXT,
                           1,
                           "",
                           rect.xmin,
                           rect.ymin,
                           rect.xmax - rect.xmin,
                           rect.ymax - rect.ymin,
                           &ptr,
                           RNA_property_identifier(prop),
                           -1,
                           0,
                           0,
                           -1,
                           -1,
                           NULL);
    UI_block_emboss_set(block, UI_EMBOSS_NONE);

    if (UI_but_active_only(context->C, context->region, block, but) == false) {
      sseq->runtime.rename_channel_index = 0;
    }

    WM_event_add_notifier(context->C, NC_SCENE | ND_SEQUENCER, context->scene);
  }
  else {
    uchar col[4] = {255, 255, 255, 255};
    char *label = SEQ_channel_name_get(context->channels, channel_index);
    UI_view2d_text_cache_add_rectf(context->v2d, &rect, label, strlen(label), col);
  }
}

/* Todo: different text/buttons alignment */
static void draw_channel_header(SeqChannelDrawContext *context, uiBlock *block, int channel_index)
{
  /* Not enough space to draw. Draw only background.*/
  if (ICON_WIDTH > context->channel_height) {
    return;
  }

  float offset = ICON_WIDTH * 1.5f;
  offset += draw_channel_widget_lock(context, block, channel_index, offset);
  offset += draw_channel_widget_hide(context, block, channel_index, offset);

  draw_channel_labels(context, block, channel_index, offset);
}

static void draw_channel_headers(SeqChannelDrawContext *context)
{
  uiBlock *block = UI_block_begin(context->C, context->region, __func__, UI_EMBOSS);

  int channel_range[2];
  displayed_channel_range_get(context, channel_range);

  for (int channel = channel_range[0]; channel <= channel_range[1]; channel++) {
    draw_channel_header(context, block, channel);
  }

  UI_view2d_text_cache_draw(context->region);
  UI_block_end(context->C, block);
  UI_block_draw(context->C, block);
}

static void seq_draw_sfra_efra(SeqChannelDrawContext *context)
{
  Scene *scene = context->scene;
  const float channels_region_width = BLI_rctf_size_x(&context->v2d->cur);
  const float sfra_pixelspace_rel = (scene->r.sfra - context->timeline_region_v2d->cur.xmin) *
                                    context->frame_width;
  const float efra_pixelspace_rel = (scene->r.efra + 1 - context->timeline_region_v2d->cur.xmin) *
                                    context->frame_width;
  const float frame_sta = channels_region_width + sfra_pixelspace_rel;
  const float frame_end = channels_region_width + efra_pixelspace_rel;

  View2D *v2d = context->v2d;

  GPU_blend(GPU_BLEND_ALPHA);
  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

  /* Draw overlay outside of frame range. */
  immUniformThemeColorShadeAlpha(TH_BACK, -10, -100);

  if (frame_sta < frame_end) {
    immRectf(pos, v2d->cur.xmin, v2d->cur.ymin, (float)frame_sta, v2d->cur.ymax);
    immRectf(pos, (float)frame_end, v2d->cur.ymin, v2d->cur.xmax, v2d->cur.ymax);
  }
  else {
    immRectf(pos, v2d->cur.xmin, v2d->cur.ymin, v2d->cur.xmax, v2d->cur.ymax);
  }

  immUniformThemeColorShade(TH_BACK, -60);

  /* Draw frame range boundary. */
  immBegin(GPU_PRIM_LINES, 4);

  immVertex2f(pos, frame_sta, v2d->cur.ymin);
  immVertex2f(pos, frame_sta, v2d->cur.ymax);

  immVertex2f(pos, frame_end, v2d->cur.ymin);
  immVertex2f(pos, frame_end, v2d->cur.ymax);

  immEnd();

  immUnbindProgram();

  GPU_blend(GPU_BLEND_NONE);
}

static void draw_background_alternate_rows(SeqChannelDrawContext *context)
{
  int channel_range[2];
  displayed_channel_range_get(context, channel_range);

  for (int channel = channel_range[0]; channel <= channel_range[1]; channel++) {
    if ((channel & 1) == 0) {
      continue;
    }

    float y = channel_index_y_min(context, channel);
    uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    GPU_blend(GPU_BLEND_ALPHA);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    immUniformThemeColor(TH_ROW_ALTERNATE);
    immRectf(pos, 1, y, context->v2d->cur.xmax, y + context->channel_height);
    immUnbindProgram();
    GPU_blend(GPU_BLEND_NONE);
  }
}

static void draw_separator(SeqChannelDrawContext *context)
{
  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  GPU_blend(GPU_BLEND_ALPHA);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformThemeColorShade(TH_BACK, 30);
  immBegin(GPU_PRIM_LINES, 2);
  immVertex2f(pos, context->v2d->cur.xmax, context->v2d->cur.ymin);
  immVertex2f(pos, context->v2d->cur.xmax, context->v2d->cur.ymax);
  immEnd();
  immUnbindProgram();
  GPU_blend(GPU_BLEND_NONE);
}

static void draw_background(SeqChannelDrawContext *context)
{
  UI_ThemeClearColor(TH_BACK);
  draw_background_alternate_rows(context);
  seq_draw_sfra_efra(context);
  draw_separator(context);
}

void channel_draw_context_init(const bContext *C,
                               ARegion *region,
                               SeqChannelDrawContext *r_context)
{
  r_context->C = C;
  r_context->area = CTX_wm_area(C);
  r_context->region = region;
  r_context->v2d = &region->v2d;
  r_context->channel_height = channel_height_pixelspace_get(CTX_wm_area(C));
  r_context->frame_width = frame_width_pixelspace_get(CTX_wm_area(C));
  r_context->scene = CTX_data_scene(C);
  r_context->ed = SEQ_editing_get(r_context->scene);
  r_context->seqbase = SEQ_active_seqbase_get(r_context->ed);
  r_context->channels = SEQ_channels_active_get(r_context->ed);

  LISTBASE_FOREACH (ARegion *, region, &r_context->area->regionbase) {
    if (region->regiontype == RGN_TYPE_WINDOW) {
      r_context->timeline_region_v2d = &region->v2d;
      break;
    }
  }
}

void draw_channels(const bContext *C, ARegion *region)
{
  SeqChannelDrawContext context;
  channel_draw_context_init(C, region, &context);

  sync_channel_header_area(&context);
  UI_view2d_view_ortho(context.v2d);

  draw_background(&context);
  draw_channel_headers(&context);

  UI_view2d_view_restore(C);
}
