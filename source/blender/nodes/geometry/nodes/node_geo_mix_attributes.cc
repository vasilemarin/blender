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
 */

#include "BKE_material.h"

#include "node_geometry_util.hh"

static bNodeSocketTemplate geo_node_mix_attributes_in[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {SOCK_STRING, N_("Factor")},
    {SOCK_STRING, N_("Attribute A")},
    {SOCK_STRING, N_("Attribute B")},
    {SOCK_STRING, N_("Result")},
    {-1, ""},
};

static bNodeSocketTemplate geo_node_mix_attribute_out[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {-1, ""},
};

namespace blender::nodes {

static void do_mix_operation_float(const int blend_mode,
                                   const FloatReadAttribute &factors,
                                   const FloatReadAttribute &inputs_a,
                                   const FloatReadAttribute &inputs_b,
                                   FloatWriteAttribute &results)
{
  const int size = results.size();
  for (const int i : IndexRange(size)) {
    const float factor = factors[i];
    float3 a{inputs_a[i]};
    const float3 b{inputs_b[i]};
    ramp_blend(blend_mode, a, factor, b);
    const float result = a.length();
    results.set(i, result);
  }
}

static void do_mix_operation_float3(const int blend_mode,
                                    const FloatReadAttribute &factors,
                                    const Float3ReadAttribute &inputs_a,
                                    const Float3ReadAttribute &inputs_b,
                                    Float3WriteAttribute &results)
{
  const int size = results.size();
  for (const int i : IndexRange(size)) {
    const float factor = factors[i];
    float3 a = inputs_a[i];
    const float3 b = inputs_b[i];
    ramp_blend(blend_mode, a, factor, b);
    results.set(i, a);
  }
}

static void do_mix_operation_color4f(const int blend_mode,
                                     const FloatReadAttribute &factors,
                                     const Color4fReadAttribute &inputs_a,
                                     const Color4fReadAttribute &inputs_b,
                                     Color4fWriteAttribute &results)
{
  const int size = results.size();
  for (const int i : IndexRange(size)) {
    const float factor = factors[i];
    Color4f a = inputs_a[i];
    const Color4f b = inputs_b[i];
    ramp_blend(blend_mode, a, factor, b);
    results.set(i, a);
  }
}

static void mix_attributes_calc(GeometryComponent &component, const GeoNodeExecParams &params)
{
  const bNode &node = params.node();
  const int blend_mode = node.custom1;

  const std::string factor_name = params.get_input<std::string>("Factor");
  const std::string attribute_a_name = params.get_input<std::string>("Attribute A");
  const std::string attribute_b_name = params.get_input<std::string>("Attribute B");
  const std::string result_name = params.get_input<std::string>("Result");

  CustomDataType result_type = CD_PROP_COLOR;
  AttributeDomain result_domain = ATTR_DOMAIN_POINT;

  /* Use type and domain from the result attribute, if it exists already. */
  const ReadAttributePtr result_attribute_read = component.attribute_try_get_for_read(result_name);
  if (result_attribute_read) {
    result_type = result_attribute_read->custom_data_type();
    result_domain = result_attribute_read->domain();
  }

  WriteAttributePtr attribute_result = component.attribute_try_ensure_for_write(
      result_name, result_domain, result_type);
  if (!attribute_result) {
    return;
  }

  FloatReadAttribute attribute_factor = component.attribute_get_for_read<float>(
      factor_name, result_domain, 0.5f);
  ReadAttributePtr attribute_a = component.attribute_get_for_read(
      attribute_a_name, result_domain, result_type, nullptr);
  ReadAttributePtr attribute_b = component.attribute_get_for_read(
      attribute_b_name, result_domain, result_type, nullptr);

  if (result_type == CD_PROP_FLOAT) {
    FloatReadAttribute attribute_a_float = std::move(attribute_a);
    FloatReadAttribute attribute_b_float = std::move(attribute_b);
    FloatWriteAttribute attribute_result_float = std::move(attribute_result);
    do_mix_operation_float(blend_mode,
                           attribute_factor,
                           attribute_a_float,
                           attribute_b_float,
                           attribute_result_float);
  }
  else if (result_type == CD_PROP_FLOAT3) {
    Float3ReadAttribute attribute_a_float3 = std::move(attribute_a);
    Float3ReadAttribute attribute_b_float3 = std::move(attribute_b);
    Float3WriteAttribute attribute_result_float3 = std::move(attribute_result);
    do_mix_operation_float3(blend_mode,
                            attribute_factor,
                            attribute_a_float3,
                            attribute_b_float3,
                            attribute_result_float3);
  }
  else if (result_type == CD_PROP_COLOR) {
    Color4fReadAttribute attribute_a_color4f = std::move(attribute_a);
    Color4fReadAttribute attribute_b_color4f = std::move(attribute_b);
    Color4fWriteAttribute attribute_result_color4f = std::move(attribute_result);
    do_mix_operation_color4f(blend_mode,
                             attribute_factor,
                             attribute_a_color4f,
                             attribute_b_color4f,
                             attribute_result_color4f);
  }
}

static void geo_node_mix_attributes_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");

  if (geometry_set.has<MeshComponent>()) {
    mix_attributes_calc(geometry_set.get_component_for_write<MeshComponent>(), params);
  }
  if (geometry_set.has<PointCloudComponent>()) {
    mix_attributes_calc(geometry_set.get_component_for_write<PointCloudComponent>(), params);
  }

  params.set_output("Geometry", geometry_set);
}

}  // namespace blender::nodes

void register_node_type_geo_mix_attributes()
{
  static bNodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_MIX_ATTRIBUTES, "Mix Attributes", 0, 0);
  node_type_socket_templates(&ntype, geo_node_mix_attributes_in, geo_node_mix_attribute_out);
  ntype.geometry_node_execute = blender::nodes::geo_node_mix_attributes_exec;
  nodeRegisterType(&ntype);
}
