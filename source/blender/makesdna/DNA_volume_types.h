/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file DNA_volume_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_VOLUME_TYPES_H__
#define __DNA_VOLUME_TYPES_H__

#include "DNA_ID.h"

struct PackedFile;

typedef struct Volume {
  ID id;
  struct AnimData *adt; /* animation data (must be immediately after id) */

  char filepath[1024]; /* FILE_MAX */

  struct PackedFile *packedfile;

  int flag;

  /* OpenVDB Voxel Grids */
  int active_grid;
  void *vdb_grids;

  /* Material */
  struct Material **mat;
  short totcol;
  short _pad2[3];

  /* Draw Cache */
  void *batch_cache;
} Volume;

/* Volume.flag */
enum {
  VO_DS_EXPAND = (1 << 0),
};

#endif /* __DNA_VOLUME_TYPES_H__ */
