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
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup Alembic
 */

#pragma once

#include <Alembic/Abc/OArrayProperty.h>
#include <Alembic/Abc/OCompoundProperty.h>

#include "BLI_map.hh"

#include <memory>

struct IDProperty;

namespace blender::io::alembic {

/* Write values of Custom Properties (a.k.a. ID Properties) to Alembic.
 * Create the appropriate Alembic objects for the property types. */
class CustomPropertiesExporter {
 private:
  /* The Compound Property that will contain the exported custom properties.
   *
   * Typically this the return value of Abc::OSchema::getArbGeomParams() or
   * Abc::OSchema::getUserProperties(). */
  Alembic::Abc::OCompoundProperty abc_compound_prop_;

  /* Mapping from property name in Blender to property in Alembic.
   * Here Blender does the same as other software (Maya, Houdini), and writes
   * scalar properties as single-element arrays. */
  Map<std::string, Alembic::Abc::OArrayProperty> abc_properties_;

  uint32_t timesample_index_;

 public:
  CustomPropertiesExporter(Alembic::Abc::OCompoundProperty abc_compound_prop,
                           uint32_t timesample_index);
  virtual ~CustomPropertiesExporter();

  void write_all(IDProperty *group);
  void write(IDProperty *id_property);

 private:
  /* Write a single scalar (i.e. non-array) property as single-value array. */
  template<typename ABCPropertyType, typename BlenderValueType>
  void set_scalar_property(const StringRef property_name, const BlenderValueType property_value);
};

}  // namespace blender::io::alembic
