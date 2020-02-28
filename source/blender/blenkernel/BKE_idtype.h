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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

#ifndef __BKE_IDTYPE_H__
#define __BKE_IDTYPE_H__

/** \file
 * \ingroup bke
 *
 * ID type structure, helping to factorize common operations and data for all data-block types.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct ID;

enum {
  IDTYPE_FLAGS_IS_LINKABLE = 1 << 0,
};

typedef void (*IDTypeInitDataFunction)(struct ID *id);

typedef struct IDTypeInfo {
  /* Unique identifier of this type, either as a short or an array of two chars. */
  short id_code;
  /* Bitflag matching id_type, used for filtering (e.g. in file browser). */
  int id_filter;

  /* Define the position of this data-block type in the virtual list of all data in a Main that is
   * returned by `set_listbasepointers()`.
   * Very important, this has to be unique and below INDEX_ID_MAX, see DNA_ID.h. */
  short main_listbase_index;

  /* Memory size of a data-block of that type. */
  size_t struct_size;

  /* The user visible name for this data-block. */
  const char *name;
  /* Plural version of the user-visble name. */
  const char *name_plural;
  /* Translation context to use for UI messages related to that type of data-block. */
  const char *translation_context;

  /* Generic info flags about that data-block type. */
  int flags;

  /* ********** ID management callbacks ********** */
  IDTypeInitDataFunction init_data;
} IDTypeInfo;

/* Module initialization. */
void BKE_idtype_init(void);

const struct IDTypeInfo *BKE_idtype_get_info_from_idcode(short id_code);
const struct IDTypeInfo *BKE_idtype_get_info_from_id(struct ID *id);

extern IDTypeInfo IDType_ID_OB;

#ifdef __cplusplus
}
#endif

#endif /* __BKE_IDTYPE_H__ */
