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
 */

#ifdef WITH_PYTHON

#include "BKE_image.h"
#include "BKE_node.h"

#include "BLI_math_vector.h"
#include "BLI_string.h"

#include "DNA_material_types.h"

#include "usd.h"
#include "usd_umm.h"

#include <iostream>
#include <vector>

// The following is additional example code for invoking Python and
// a Blender Python operator from C++:

//#include "BPY_extern_python.h"
//#include "BPY_extern_run.h"

//const char *foo[] = { "bpy", 0 };
//BPY_run_string_eval(C, nullptr, "print('hi!!')");
//BPY_run_string_eval(C, foo, "bpy.ops.universalmaterialmap.instance_to_data_converter()");
//BPY_run_string_eval(C, nullptr, "print('test')");

namespace usdtokens {

// Render context names.
static const pxr::TfToken mdl("mdl", pxr::TfToken::Immortal);

} // end namespace usdtokens


// Some of the static functions are duplicates of code in usd_reader_material.cc.  These
// should be consolidated once code is stabilized.
static bNode *umm_add_node(const bContext *C, bNodeTree *ntree, int type, float locx, float locy)
{
  bNode *new_node = nodeAddStaticNode(C, ntree, type);

  if (new_node) {
    new_node->locx = locx;
    new_node->locy = locy;
  }

  return new_node;
}

static void umm_link_nodes(
  bNodeTree *ntree, bNode *source, const char *sock_out, bNode *dest, const char *sock_in)
{
  bNodeSocket *source_socket = nodeFindSocket(source, SOCK_OUT, sock_out);

  if (!source_socket) {
    std::cerr << "PROGRAMMER ERROR: Couldn't find output socket " << sock_out << std::endl;
    return;
  }

  bNodeSocket *dest_socket = nodeFindSocket(dest, SOCK_IN, sock_in);

  if (!dest_socket) {
    std::cerr << "PROGRAMMER ERROR: Couldn't find input socket " << sock_in << std::endl;
    return;
  }

  nodeAddLink(ntree, source, source_socket, dest, dest_socket);
}

static void print_obj(PyObject *obj) {
  if (!obj) {
    return;
  }

  PyObject *str = PyObject_Str(obj);
  if (str && PyUnicode_Check(str)) {
    std::cout << PyUnicode_AsUTF8(str) << std::endl;
    Py_DECREF(str);
  }
}

static bool is_none_value(PyObject *tup)
{
  if (!(tup && PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
    return false;
  }

  PyObject *second = PyTuple_GetItem(tup, 1);

  return second == Py_None;
}

/* Sets the source asset and source asset subidenifier properties on the given shader
 * with values parsed from the given target_class string. */
static bool set_source_asset(pxr::UsdShadeShader &usd_shader, const std::string &target_class)
{
  if (!usd_shader || target_class.empty()) {
    return false;
  }

  // Split the target_class string on the '|' separator.
  size_t sep = target_class.find_last_of("|");
  if (sep == 0 || sep == std::string::npos) {
    std::cout << "Couldn't parse target_class string " << target_class << std::endl;
    return false;
  }

  std::string source_asset = target_class.substr(0, sep);
  usd_shader.SetSourceAsset(pxr::SdfAssetPath(source_asset), usdtokens::mdl);

  std::string source_asset_subidentifier = target_class.substr(sep + 1);

  if (!source_asset_subidentifier.empty()) {
    usd_shader.SetSourceAssetSubIdentifier(pxr::TfToken(source_asset_subidentifier), usdtokens::mdl);
  }

  return true;
}


static bool get_data_name(PyObject *tup, std::string &r_name)
{
  if (!(tup && PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
    return false;
  }

  PyObject *first = PyTuple_GetItem(tup, 0);

  if (first && PyUnicode_Check(first)) {
    const char *name = PyUnicode_AsUTF8(first);
    if (name) {
      r_name = name;
      return true;
    }
  }

  return false;
}

static bool get_string_data(PyObject *tup, std::string &r_data)
{
  if (!(tup && PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
    return false;
  }

  PyObject *second = PyTuple_GetItem(tup, 1);

  if (second && PyUnicode_Check(second)) {
    const char *data = PyUnicode_AsUTF8(second);
    if (data) {
      r_data = data;
      return true;
    }
  }

  return false;
}

static bool get_float_data(PyObject *tup, float &r_data)
{
  if (!(tup && PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
    return false;
  }

  PyObject *second = PyTuple_GetItem(tup, 1);

  if (second && PyFloat_Check(second)) {
    r_data = static_cast<float>(PyFloat_AsDouble(second));
    return true;
  }

  return false;
}

static bool get_float3_data(PyObject *tup, float r_data[3])
{
  if (!(tup && PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
    return false;
  }

  PyObject *second = PyTuple_GetItem(tup, 1);

  if (second && PyTuple_Check(second) && PyTuple_Size(second) > 2) {
    for (int i = 0; i < 3; ++i) {
      PyObject *comp = PyTuple_GetItem(second, i);
      if (comp && PyFloat_Check(comp)) {
        r_data[i] = static_cast<float>(PyFloat_AsDouble(comp));
      }
      else {
        return false;
      }
    }
    return true;
  }

  return false;
}

static bool get_rgba_data(PyObject *tup, float r_data[4])
{
  if (!(tup && PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
    return false;
  }

  PyObject *second = PyTuple_GetItem(tup, 1);

  if (!(second && PyTuple_Check(second))) {
    return false;
  }

  Py_ssize_t size = PyTuple_Size(second);

  if (size > 2) {
    for (int i = 0; i < 3; ++i) {
      PyObject *comp = PyTuple_GetItem(second, i);
      if (comp && PyFloat_Check(comp)) {
        r_data[i] = static_cast<float>(PyFloat_AsDouble(comp));
      }
      else {
        return false;
      }
    }

    if (size > 3) {
      PyObject *alpha = PyTuple_GetItem(second, 3);
      if (alpha && PyFloat_Check(alpha)) {
        r_data[3] = static_cast<float>(PyFloat_AsDouble(alpha));
      }
      else {
        return false;
      }
    }
    else {
      r_data[3] = 1.0;
    }

    return true;
  }

  return false;
}

namespace blender::io::usd
{

// Compute the x- and y-coordinates for placing a new node in an unoccupied region of
// the column with the given index.  Returns the coordinates in r_locx and r_locy and
// updates the column-occupancy information in r_ctx.
void umm_compute_node_loc(
  int column, float node_height, float &r_locx, float &r_locy, UMMNodePlacementContext &r_ctx)
{
  r_locx = r_ctx.origx - column * r_ctx.horizontal_step;

  if (column >= r_ctx.column_offsets.size()) {
    r_ctx.column_offsets.push_back(0.0f);
  }

  r_locy = r_ctx.origy - r_ctx.column_offsets[column];

  // Record the y-offset of the occupied region in
  // the column, including padding.
  r_ctx.column_offsets[column] += node_height + 10.0f;
}


PyObject *USDUMM::s_umm_module = nullptr;

static const char *k_umm_module_name = "omni.universalmaterialmap.blender.material";
static const char *k_omni_pbr_mdl_name = "OmniPBR.mdl";
static const char *k_omni_pbr_name = "OmniPBR";


USDUMM::USDUMM(Main *bmain)
  : bmain_(bmain)
{
}

USDUMM::~USDUMM()
{
}

/* Be sure to call PyGILState_Ensure() before calling this function. */
bool USDUMM::ensure_module_loaded()
{

  if (!s_umm_module) {
    s_umm_module = PyImport_ImportModule(k_umm_module_name);
    if (!s_umm_module) {
      std::cout << "WARNING: couldn't load Python module " << k_umm_module_name << std::endl;
    }
  }

  return s_umm_module != nullptr;
}

void USDUMM::test_python()
{
  PyGILState_STATE gilstate = PyGILState_Ensure();

  PyObject *mod = PyImport_ImportModule("omni.universalmaterialmap.core.converter.util");

  if (mod) {
    const char *func_name = "get_conversion_manifest";

    if (PyObject_HasAttrString(mod, func_name)) {
      if (PyObject *func = PyObject_GetAttrString(mod, func_name)) {
        PyObject *ret = PyObject_CallObject(func, nullptr);
        Py_DECREF(func);

        if (ret) {
          print_obj(ret);
          Py_DECREF(ret);
        }
      }
    }
  }

  PyGILState_Release(gilstate);
}

bool USDUMM::map_material(Material *mtl, const pxr::UsdShadeMaterial &usd_material) const
{
  if (!(bmain_ && mtl && usd_material)) {
    return false;
  }

  /* Get the surface shader. */
  pxr::UsdShadeShader surf_shader = usd_material.ComputeSurfaceSource(usdtokens::mdl);

  if (surf_shader) {
    /* Check if we have an mdl source asset. */
    pxr::SdfAssetPath source_asset;
    if (!surf_shader.GetSourceAsset(&source_asset, usdtokens::mdl)) {
      std::cout << "No mdl source asset for shader " << surf_shader.GetPath() << std::endl;
    }
    pxr::TfToken source_asset_sub_identifier;
    if (!surf_shader.GetSourceAssetSubIdentifier(&source_asset_sub_identifier, usdtokens::mdl)) {
      std::cout << "No mdl source asset sub identifier for shader " << surf_shader.GetPath() << std::endl;
    }

    std::string path = source_asset.GetAssetPath();

    // Get the filename component of the path.
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
      path = path.substr(last_slash + 1);
    }

    std::string source_class = path + "|" + source_asset_sub_identifier.GetString();
    return map_material(mtl, surf_shader, source_class);
  }

  return false;
}

bool USDUMM::map_material(Material *mtl,
                          const pxr::UsdShadeShader &usd_shader,
                          const std::string &source_class) const
{
  if (!(bmain_ && usd_shader && mtl)) {
    return false;
  }

  PyGILState_STATE gilstate = PyGILState_Ensure();

  if (!ensure_module_loaded()) {
    PyGILState_Release(gilstate);
    return false;
  }

  const char *func_name = "apply_data_to_instance";

  if (!PyObject_HasAttrString(s_umm_module, func_name)) {
    std::cerr << "WARNING: UMM module has no attribute " << func_name << std::endl;
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *func = PyObject_GetAttrString(s_umm_module, func_name);

  if (!func) {
    std::cerr << "WARNING: Couldn't get UMM module attribute " << func_name << std::endl;
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *source_data = get_shader_source_data(usd_shader);

  if (!source_data) {
    std::cout << "WARNING:  Couldn't get source data for shader " << usd_shader.GetPath() << std::endl;
    PyGILState_Release(gilstate);
    return false;
  }

  //std::cout << "source_data:\n";
  //print_obj(source_data);

  // Create the kwargs dictionary.
  PyObject *kwargs = PyDict_New();

  if (!kwargs) {
    std::cout << "WARNING:  Couldn't create kwargs dicsionary." << std::endl;
    Py_DECREF(source_data);
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *instance_name = PyUnicode_FromString(mtl->id.name + 2);
  PyDict_SetItemString(kwargs, "instance_name", instance_name);
  Py_DECREF(instance_name);

  PyObject *source_class_obj = PyUnicode_FromString(source_class.c_str());
  PyDict_SetItemString(kwargs, "source_class", source_class_obj);
  Py_DECREF(source_class_obj);

  PyObject *render_context = PyUnicode_FromString("Blender");
  PyDict_SetItemString(kwargs, "render_context", render_context);
  Py_DECREF(render_context);

  PyDict_SetItemString(kwargs, "source_data", source_data);
  Py_DECREF(source_data);

  std::cout << func_name << " arguments:\n";
  print_obj(kwargs);

  PyObject *empty_args = PyTuple_New(0);
  PyObject *ret = PyObject_Call(func, empty_args, kwargs);
  Py_DECREF(empty_args);
  Py_DECREF(func);

  bool success = ret != nullptr;

  if (ret) {
    std::cout << "result:\n";
    print_obj(ret);
    Py_DECREF(ret);
  }

  Py_DECREF(kwargs);

  PyGILState_Release(gilstate);

  return success;
}

PyObject *USDUMM::get_shader_source_data(const pxr::UsdShadeShader &usd_shader) const
{
  if (!(bmain_ && usd_shader)) {
    return nullptr;
  }

  std::vector<PyObject *> tuple_items;

  std::vector<pxr::UsdShadeInput> inputs = usd_shader.GetInputs();

  for (auto input : inputs) {

    PyObject *tup = nullptr;

    std::string name = input.GetBaseName().GetString();

    if (name.empty()) {
      continue;
    }

    pxr::UsdAttribute usd_attr = input.GetAttr();

    if (input.HasConnectedSource()) {
      pxr::UsdShadeConnectableAPI source;
      pxr::TfToken source_name;
      pxr::UsdShadeAttributeType source_type;

      if (input.GetConnectedSource(&source, &source_name, &source_type)) {
        usd_attr = source.GetInput(source_name).GetAttr();
      }
      else {
        std::cerr << "ERROR: couldn't get connected source for usd shader input "
          << input.GetPrim().GetPath() << " " << input.GetFullName() << std::endl;
      }
    }

    pxr::VtValue val;
    if (!usd_attr.Get(&val)) {
      std::cerr << "ERROR: couldn't get value for usd shader input "
        << input.GetPrim().GetPath() << " " << input.GetFullName() << std::endl;
      continue;
    }

    if (val.IsHolding<float>()) {
      double dval = val.UncheckedGet<float>();
      tup = Py_BuildValue("sd", name.c_str(), dval);
    }
    else if (val.IsHolding<int>()) {
      int ival = val.UncheckedGet<int>();
      tup = Py_BuildValue("si", name.c_str(), ival);
    }
    else if (val.IsHolding<bool>()) {
      int ival = val.UncheckedGet<bool>();
      tup = Py_BuildValue("si", name.c_str(), ival);
    }
    else if (val.IsHolding<pxr::SdfAssetPath>()) {
      pxr::SdfAssetPath assetPath = val.Get<pxr::SdfAssetPath>();

      std::string resolved_path = assetPath.GetResolvedPath();

      pxr::TfToken color_space_tok = usd_attr.GetColorSpace();

      std::string color_space_str = !color_space_tok.IsEmpty() ? color_space_tok.GetString() :
                                                                 "sRGB";

      PyObject *tex_file_tup = Py_BuildValue("ss", resolved_path.c_str(), color_space_str.c_str());

      tup = Py_BuildValue("sN", name.c_str(), tex_file_tup);
    }
    else if (val.IsHolding<pxr::GfVec3f>()) {
      pxr::GfVec3f v3f = val.UncheckedGet<pxr::GfVec3f>();
      pxr::GfVec3d v3d(v3f);
      PyObject *v3_tup = Py_BuildValue("ddd", v3d[0], v3d[1], v3d[2]);
      if (v3_tup) {
        tup = Py_BuildValue("sN", name.c_str(), v3_tup);
      }
      else {
        std::cout << "Couldn't build v3f tuple for " << usd_shader.GetPath()
          << " input " << input.GetFullName() << std::endl;
      }
    }
    else if (val.IsHolding<pxr::GfVec2f>()) {
      pxr::GfVec2f v2f = val.UncheckedGet<pxr::GfVec2f>();
 /*     std::cout << "Have v2f input " << v2f << " for "
        << usd_shader.GetPath() << " " << input.GetFullName() << std::endl;*/
      pxr::GfVec2d v2d(v2f);
      PyObject *v2_tup = Py_BuildValue("dd", v2d[0], v2d[1]);
      if (v2_tup) {
        tup = Py_BuildValue("sN", name.c_str(), v2_tup);
      }
      else {
        std::cout << "Couldn't build v2f tuple for " << usd_shader.GetPath()
          << " input " << input.GetFullName() << std::endl;
      }
    }

    if (tup) {
      tuple_items.push_back(tup);
    }
  }

  PyObject *ret = PyTuple_New(tuple_items.size());

  if (!ret) {
    return nullptr;
  }

  for (int i = 0; i < tuple_items.size(); ++i) {
    if (PyTuple_SetItem(ret, i, tuple_items[i])) {
      std::cout << "error setting tuple item" << std::endl;
    }
  }

  return ret;
}

void USDUMM::create_blender_nodes(Material *mtl, PyObject *data_list) const
{
  if (!(mtl && data_list && PyList_Check(data_list))) {
    return;
  }

  int size = PyList_Size(data_list);

  if (size < 2) {
    return;
  }

  PyObject *first = PyList_GetItem(data_list, 0);

  std::string name;
  if (!get_data_name(first,name) || name != "umm_target_class") {
    std::cout << "couldn't get umm_target_class\n";
    return;
  }

  std::string str_data;
  if (!get_string_data(first, str_data)) {
    std::cout << "Couldn't get UMM target class value." << std::endl;
    return;
  }

  if (str_data != "bpy.types.ShaderNodeBsdfPrincipled") {
    std::cout << "Unsupported UMM target class " << str_data << std::endl;
    return;
  }

  std::cout << "target class " << str_data << std::endl;

  /* Create the Material's node tree containing the principled
   * and output shader. */

  bNodeTree *ntree = ntreeAddTree(NULL, "Shader Nodetree", "ShaderNodeTree");
  mtl->nodetree = ntree;
  mtl->use_nodes = true;

  bNode *principled = umm_add_node(NULL, ntree, SH_NODE_BSDF_PRINCIPLED, 0.0f, 300.0f);

  if (!principled) {
    std::cerr << "ERROR: Couldn't create SH_NODE_BSDF_PRINCIPLED node." << std::endl;
    return;
  }

  bNode *output = umm_add_node(NULL, ntree, SH_NODE_OUTPUT_MATERIAL, 300.0f, 300.0f);

  if (!output) {
    std::cerr << "ERROR: Couldn't create SH_NODE_OUTPUT_MATERIAL node." << std::endl;
    return;
  }

  umm_link_nodes(ntree, principled, "BSDF", output, "Surface");

  UMMNodePlacementContext context(0.0f, 300.0);
  int column = 0;

  /* Set up the principled shader inputs. */

  for (int i = 1; i < size; ++i) {
    PyObject *tup = PyList_GetItem(data_list, i);

    if (!tup) {
      continue;
    }

    if (!get_data_name(tup, name) || name.empty()) {
      std::cout << "Couldn't get data name\n";
      continue;
    }

    if (is_none_value(tup)) {
      /* Receiving None values is not an error. */
      continue;
    }

    bNodeSocket *sock = nodeFindSocket(principled, SOCK_IN, name.c_str());
    if (!sock) {
      std::cerr << "ERROR: couldn't get destination node socket " << name << std::endl;
      continue;
    }

    if (sock->type == SOCK_FLOAT || sock->type == SOCK_RGBA || sock->type == SOCK_VECTOR) {
      // Float and float vector sockets can take a texture node as input.  If UMM provided
      // the data as a string, we create a texture node that takes the given string as a
      // file path.

      str_data.clear();

      if (get_string_data(tup, str_data)) {
        add_texture_node(str_data.c_str(), principled, name.c_str(), ntree, column + 1, context);
        continue;
      }
    }

    switch (sock->type) {
    case SOCK_FLOAT: {
      float float_data = 0.0;

      if (get_float_data(tup, float_data)) {
        ((bNodeSocketValueFloat *)sock->default_value)->value = float_data;
      }
      else {
        std::cout << "Couldn't get float data for destination node socket " << name << std::endl;
      }
      break;
    }
    case SOCK_RGBA: {
      float rgba_data[4] = { 1.0, 1.0f, 1.0f, 1.0f };

      if (get_rgba_data(tup, rgba_data)) {
        copy_v4_v4(((bNodeSocketValueRGBA *)sock->default_value)->value, rgba_data);
      }
      else {
        std::cout << "Couldn't get rgba data for destination node socket " << name << std::endl;
      }
      break;
    }
    case SOCK_VECTOR: {
      float float3_data[3] = { 0.0f, 0.0f, 0.0f };

      if (get_float3_data(tup, float3_data)) {
        copy_v3_v3(((bNodeSocketValueVector *)sock->default_value)->value, float3_data);
      }
      else {
        std::cout << "Couldn't get float3 data for destination node socket " << name << std::endl;
      }
      break;
    }
    default:
      std::cerr << "WARNING: unexpected type " << sock->idname << " for destination node socket "
        << name << std::endl;
      break;
    }

  }
}

void USDUMM::add_texture_node(const char *tex_file,
                              bNode *dest_node,
                              const char *dest_socket_name,
                              bNodeTree *ntree,
                              int column,
                              UMMNodePlacementContext &r_ctx) const
{
  if (!tex_file || !dest_node || !ntree || !dest_socket_name || !bmain_) {
    return;
  }

  float locx = 0.0f;
  float locy = 0.0f;

  if (strcmp(dest_socket_name, "Normal") == 0) {

    // The normal texture input requires creating a normal map node.
    umm_compute_node_loc(column, 300.0, locx, locy, r_ctx);

    bNode *normal_map = umm_add_node(NULL, ntree, SH_NODE_NORMAL_MAP, locx, locy);

    // Currently, the Normal Map node has Tangent Space as the default,
    // which is what we need, so we don't need to explicitly set it.

    // Connect the Normal Map to the Normal input.
    umm_link_nodes(ntree, normal_map, "Normal", dest_node, "Normal");

    // Update the parameters so we create the Texture Image node input to
    // the Normal Map "Color" input.
    dest_node = normal_map;
    dest_socket_name = "Color";
    column += 1;
  }

  umm_compute_node_loc(column, 300.0f, locx, locy, r_ctx);

  // Create the Texture Image node.
  bNode *tex_image = umm_add_node(NULL, ntree, SH_NODE_TEX_IMAGE, locx, locy);

  if (!tex_image) {
    std::cerr << "ERROR: Couldn't create SH_NODE_TEX_IMAGE for node input " << dest_socket_name
      << std::endl;
    return;
  }

  Image *image = BKE_image_load_exists(bmain_, tex_file);
  if (image) {
    tex_image->id = &image->id;

    /* TODO(makowalsk): Figure out how to receive color space
     * information from UMM. For now, assume "raw" for any
     * input other than Base Color, which is not always correct.
     * We can probably query the origina USD shader input
     * for this file and call GetColorSpace() on that attribute. */
    if (strcmp(dest_socket_name, "Base Color") != 0) {
      STRNCPY(image->colorspace_settings.name, "Raw");
    }
  }

  umm_link_nodes(ntree, tex_image, "Color", dest_node, dest_socket_name);
}

bool USDUMM::map_material_to_usd(const USDExporterContext &usd_export_context,
                                 const Material *mtl,
                                 pxr::UsdShadeShader &usd_shader,
                                 const std::string &render_context) const
{
  if (!(usd_shader && mtl)) {
    return false;
  }

  PyGILState_STATE gilstate = PyGILState_Ensure();

  if (!ensure_module_loaded()) {
    PyGILState_Release(gilstate);
    return false;
  }

  const char *func_name = "convert_instance_to_data";

  if (!PyObject_HasAttrString(s_umm_module, func_name)) {
    std::cerr << "WARNING: UMM module has no attribute " << func_name << std::endl;
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *func = PyObject_GetAttrString(s_umm_module, func_name);

  if (!func) {
    std::cerr << "WARNING: Couldn't get UMM module attribute " << func_name << std::endl;
    PyGILState_Release(gilstate);
    return false;
  }

  // Create the kwargs dictionary.
  PyObject *kwargs = PyDict_New();

  if (!kwargs) {
    std::cout << "WARNING:  Couldn't create kwargs dicsionary." << std::endl;
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *instance_name = PyUnicode_FromString(mtl->id.name + 2);
  PyDict_SetItemString(kwargs, "instance_name", instance_name);
  Py_DECREF(instance_name);

  PyObject *render_context_arg = PyUnicode_FromString(render_context.c_str());
  PyDict_SetItemString(kwargs, "render_context", render_context_arg);
  Py_DECREF(render_context_arg);

  std::cout << func_name << " arguments:\n";
  print_obj(kwargs);

  PyObject *empty_args = PyTuple_New(0);
  PyObject *ret = PyObject_Call(func, empty_args, kwargs);
  Py_DECREF(empty_args);
  Py_DECREF(func);

  bool success = ret != nullptr;

  if (ret) {
    std::cout << "result:\n";
    print_obj(ret);
    set_shader_properties(usd_export_context, usd_shader, ret);
    Py_DECREF(ret);
  }

  Py_DECREF(kwargs);

  PyGILState_Release(gilstate);

  return success;
}

void USDUMM::set_shader_properties(const USDExporterContext &usd_export_context,
                                   pxr::UsdShadeShader &usd_shader,
                                   PyObject *data_list) const
{
  if (!(data_list && usd_shader)) {
    return;
  }

  if (!PyList_Check(data_list)) {
    return;
  }

  Py_ssize_t len = PyList_Size(data_list);

  for (Py_ssize_t i = 0; i < len; ++i) {
    PyObject *tup = PyList_GetItem(data_list, i);

    if (!tup) {
      continue;
    }

    std::string name;

    if (!get_data_name(tup, name) || name.empty()) {
      std::cout << "Couldn't get data name\n";
      continue;
    }

    if (is_none_value(tup)) {
      /* Receiving None values is not an error. */
      continue;
    }

    if (name == "umm_target_class") {
      std::string target_class;
      if (!get_string_data(tup, target_class) || target_class.empty()) {
        std::cout << "Couldn't get target class\n";
        continue;
      }
      set_source_asset(usd_shader, target_class);
    }
    else {
      if (!(PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
        std::cout << "Unexpected data item type or size:\n";
        print_obj(tup);
        continue;
      }

      PyObject *second = PyTuple_GetItem(tup, 1);
      if (!second) {
        std::cout << "Couldn't get second tuple value:\n";
        print_obj(tup);
        continue;
      }

      if (PyFloat_Check(second)) {
        float fval = static_cast<float>(PyFloat_AsDouble(second));
        usd_shader.CreateInput(pxr::TfToken(name), pxr::SdfValueTypeNames->Float).Set(fval);
      }
      else if (PyBool_Check(second)) {
        bool bval = static_cast<bool>(PyLong_AsLong(second));
        usd_shader.CreateInput(pxr::TfToken(name), pxr::SdfValueTypeNames->Bool).Set(bval);
      }
      else if (PyLong_Check(second)) {
        // For now, assume int values should be floats.
        float fval = static_cast<float>(PyLong_AsDouble(second));
        usd_shader.CreateInput(pxr::TfToken(name), pxr::SdfValueTypeNames->Float).Set(fval);
      }
      else if (PyList_Check(second) && PyList_Size(second) == 2) {
        PyObject *item0 = PyList_GetItem(second, 0);
        PyObject *item1 = PyList_GetItem(second, 1);

        if (PyUnicode_Check(item0) && PyUnicode_Check(item1)) {
          const char *asset = PyUnicode_AsUTF8(item0);
          const char *color_space = PyUnicode_AsUTF8(item1);
          pxr::UsdShadeInput asset_input = usd_shader.CreateInput(pxr::TfToken(name), pxr::SdfValueTypeNames->Asset);
          asset_input.Set(pxr::SdfAssetPath(asset));
          asset_input.GetAttr().SetColorSpace(pxr::TfToken(color_space));
        }
      }
      else if (PyTuple_Check(second) && PyTuple_Size(second) == 3) {
        pxr::GfVec3f f3val;
        for (int i = 0; i < 3; ++i) {
          PyObject *comp = PyTuple_GetItem(second, i);
          if (comp && PyFloat_Check(comp)) {
            f3val[i] = static_cast<float>(PyFloat_AsDouble(comp));
          }
          else {
            std::cout << "Couldn't parse color3f " << name << std::endl;
          }
        }
        usd_shader.CreateInput(pxr::TfToken(name), pxr::SdfValueTypeNames->Color3f).Set(f3val);
      }
      else {
        std::cout << "Can't handle value:\n";
        print_obj(second);
      }
    }
  }
}

}  // Namespace blender::io::usd

#endif // ifdef WITH_PYTHON
