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

#include "abc_custom_props.h"

#include <functional>
#include <iostream>
#include <string>

#include <Alembic/Abc/OTypedArrayProperty.h>
#include <Alembic/Abc/OTypedScalarProperty.h>

#include "BKE_idprop.h"
#include "DNA_ID.h"

using Alembic::Abc::ArraySample;
using Alembic::Abc::OArrayProperty;
using Alembic::Abc::OBoolArrayProperty;
using Alembic::Abc::ODoubleArrayProperty;
using Alembic::Abc::OFloatArrayProperty;
using Alembic::Abc::OInt32ArrayProperty;
using Alembic::Abc::OStringArrayProperty;

namespace blender::io::alembic {

CustomPropertiesExporter::CustomPropertiesExporter(
    Alembic::Abc::OCompoundProperty abc_compound_prop, uint32_t timesample_index)
    : abc_compound_prop_(abc_compound_prop), timesample_index_(timesample_index)
{
}

CustomPropertiesExporter::~CustomPropertiesExporter()
{
}

namespace {

/* Callback for IDP_foreach_property() that just calls CustomPropertiesExporter::write(). */
void customPropertiesExporter_write(IDProperty *id_property, void *user_data)
{
  CustomPropertiesExporter *exporter = reinterpret_cast<CustomPropertiesExporter *>(user_data);
  exporter->write(id_property);
};

}  // namespace

void CustomPropertiesExporter::write_all(IDProperty *group)
{
  if (group == nullptr) {
    std::cout << "CustomPropertiesExporter::write_all(nullptr)\n";
    return;
  }
  std::cout << "CustomPropertiesExporter::write_all(" << group->name << ")\n";
  IDP_foreach_property(group,
                       IDP_TYPE_FILTER_STRING | IDP_TYPE_FILTER_INT | IDP_TYPE_FILTER_FLOAT |
                           IDP_TYPE_FILTER_ARRAY | IDP_TYPE_FILTER_DOUBLE,
                       customPropertiesExporter_write,
                       this);
  std::cout << "\n";
}

void CustomPropertiesExporter::write(IDProperty *id_property)
{
  std::cout << "    CustomPropertiesExporter::write(" << id_property->name << " ";
  switch (id_property->type) {
    case IDP_STRING: {
      std::cout << "string";
      /* The Alembic library doesn't accept NULL-terminated character arrays. */
      const std::string prop_value = IDP_String(id_property);
      set_scalar_property<OStringArrayProperty, std::string>(id_property->name, prop_value);
      break;
    }
    case IDP_INT:
      std::cout << "int";
      set_scalar_property<OInt32ArrayProperty, int32_t>(id_property->name, IDP_Int(id_property));
      break;
    case IDP_FLOAT:
      std::cout << "float";
      set_scalar_property<OFloatArrayProperty, float>(id_property->name, IDP_Float(id_property));
      break;
    case IDP_DOUBLE:
      std::cout << "double (" << IDP_Double(id_property) << ")";
      set_scalar_property<ODoubleArrayProperty, double>(id_property->name,
                                                        IDP_Double(id_property));
      break;
    case IDP_ARRAY:
      switch (id_property->subtype) {
        case IDP_INT: {
          std::cout << "int[]";
          const int *array = (int *)IDP_Array(id_property);
          set_array_property<OInt32ArrayProperty, int32_t>(
              id_property->name, array, id_property->len);
          break;
        }
        case IDP_FLOAT: {
          std::cout << "float[]";
          const float *array = (float *)IDP_Array(id_property);
          set_array_property<OFloatArrayProperty, float>(
              id_property->name, array, id_property->len);
          break;
        }
        case IDP_DOUBLE: {
          std::cout << "double[]";
          const double *array = (double *)IDP_Array(id_property);
          set_array_property<ODoubleArrayProperty, double>(
              id_property->name, array, id_property->len);
          break;
        }
        case IDP_ARRAY:
        case IDP_GROUP:
        case IDP_ID:
        case IDP_IDPARRAY:
          std::cout << "\033[91munsupported[]\033[0m)\n";
          /* Unsupported. */
          break;
      }
      break;
    case IDP_GROUP:
    case IDP_ID:
    case IDP_IDPARRAY:
      std::cout << "\033[91munsupported\033[0m)\n";
      /* Unsupported. */
      break;
  }
}

template<typename ABCPropertyType, typename BlenderValueType>
void CustomPropertiesExporter::set_scalar_property(const StringRef property_name,
                                                   const BlenderValueType property_value)
{
  set_array_property<ABCPropertyType, BlenderValueType>(property_name, &property_value, 1);
}

template<typename ABCPropertyType, typename BlenderValueType>
void CustomPropertiesExporter::set_array_property(const StringRef property_name,
                                                  const BlenderValueType *array_values,
                                                  const size_t num_array_items)
{
  std::cout << " " << array_values[0] << ")\n";

  /* Create an Alembic property if it doesn't exist yet. */
  auto create_callback = [this, property_name]() -> OArrayProperty {
    ABCPropertyType abc_property(abc_compound_prop_, property_name);
    abc_property.setTimeSampling(timesample_index_);
    return abc_property;
  };
  OArrayProperty array_prop = abc_properties_.lookup_or_add_cb(property_name, create_callback);

  const Alembic::Util::Dimensions array_dimensions(num_array_items);
  ArraySample sample(array_values, array_prop.getDataType(), array_dimensions);
  array_prop.set(sample);
}

}  // namespace blender::io::alembic
