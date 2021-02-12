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

/** \file
 * \ingroup bpygpu
 *
 * This file defines the texture functionalities of the 'gpu' module
 *
 * - Use ``bpygpu_`` for local API.
 * - Use ``BPyGPU`` for public API.
 */

#include <Python.h>

#include "GPU_context.h"
#include "GPU_texture.h"

#include "../generic/py_capi_utils.h"

#include "gpu_py.h"
#include "gpu_py_api.h"
#include "gpu_py_buffer.h"

#include "gpu_py_texture.h" /* own include */

/* -------------------------------------------------------------------- */
/** \name GPUTexture Common Utilities
 * \{ */

static const struct PyC_StringEnumItems pygpu_textureformat_items[] = {
    {GPU_RGBA8UI, "RGBA8UI"},
    {GPU_RGBA8I, "RGBA8I"},
    {GPU_RGBA8, "RGBA8"},
    {GPU_RGBA32UI, "RGBA32UI"},
    {GPU_RGBA32I, "RGBA32I"},
    {GPU_RGBA32F, "RGBA32F"},
    {GPU_RGBA16UI, "RGBA16UI"},
    {GPU_RGBA16I, "RGBA16I"},
    {GPU_RGBA16F, "RGBA16F"},
    {GPU_RGBA16, "RGBA16"},
    {GPU_RG8UI, "RG8UI"},
    {GPU_RG8I, "RG8I"},
    {GPU_RG8, "RG8"},
    {GPU_RG32UI, "RG32UI"},
    {GPU_RG32I, "RG32I"},
    {GPU_RG32F, "RG32F"},
    {GPU_RG16UI, "RG16UI"},
    {GPU_RG16I, "RG16I"},
    {GPU_RG16F, "RG16F"},
    {GPU_RG16, "RG16"},
    {GPU_R8UI, "R8UI"},
    {GPU_R8I, "R8I"},
    {GPU_R8, "R8"},
    {GPU_R32UI, "R32UI"},
    {GPU_R32I, "R32I"},
    {GPU_R32F, "R32F"},
    {GPU_R16UI, "R16UI"},
    {GPU_R16I, "R16I"},
    {GPU_R16F, "R16F"},
    {GPU_R16, "R16"},
    {GPU_R11F_G11F_B10F, "R11F_G11F_B10F"},
    {GPU_DEPTH32F_STENCIL8, "DEPTH32F_STENCIL8"},
    {GPU_DEPTH24_STENCIL8, "DEPTH24_STENCIL8"},
    {GPU_SRGB8_A8, "SRGB8_A8"},
    {GPU_RGB16F, "RGB16F"},
    {GPU_SRGB8_A8_DXT1, "SRGB8_A8_DXT1"},
    {GPU_SRGB8_A8_DXT3, "SRGB8_A8_DXT3"},
    {GPU_SRGB8_A8_DXT5, "SRGB8_A8_DXT5"},
    {GPU_RGBA8_DXT1, "RGBA8_DXT1"},
    {GPU_RGBA8_DXT3, "RGBA8_DXT3"},
    {GPU_RGBA8_DXT5, "RGBA8_DXT5"},
    {GPU_DEPTH_COMPONENT32F, "DEPTH_COMPONENT32F"},
    {GPU_DEPTH_COMPONENT24, "DEPTH_COMPONENT24"},
    {GPU_DEPTH_COMPONENT16, "DEPTH_COMPONENT16"},
    {0, NULL},
};

static int py_texture_valid_check(BPyGPUTexture *bpygpu_tex)
{
  if (UNLIKELY(bpygpu_tex->tex == NULL)) {
    PyErr_SetString(PyExc_ReferenceError,
#ifdef BPYGPU_USE_GPUOBJ_FREE_METHOD
                    "GPU texture was freed, no further access is valid"
#else
                    "GPU texture: internal error"
#endif
    );

    return -1;
  }
  return 0;
}

#define PY_TEXTURE_CHECK_OBJ(bpygpu) \
  { \
    if (UNLIKELY(py_texture_valid_check(bpygpu) == -1)) { \
      return NULL; \
    } \
  } \
  ((void)0)

/** \} */

/* -------------------------------------------------------------------- */
/** \name GPUTexture Type
 * \{ */

inline int py_texture_component_len(eGPUTextureFormat format)
{
  switch (format) {
    case GPU_RGBA8:
    case GPU_RGBA8UI:
    case GPU_RGBA16F:
    case GPU_RGBA16:
    case GPU_RGBA32F:
    case GPU_SRGB8_A8:
      return 4;
    case GPU_RGB16F:
    case GPU_R11F_G11F_B10F:
      return 3;
    case GPU_RG8:
    case GPU_RG16:
    case GPU_RG16F:
    case GPU_RG16I:
    case GPU_RG16UI:
    case GPU_RG32F:
      return 2;
    default:
      return 1;
  }
}

static PyObject *py_texture_new(PyTypeObject *UNUSED(self), PyObject *args, PyObject *kwds)
{
  BPYGPU_IS_INIT_OR_ERROR_OBJ;

  PyObject *py_size;
  int size[3] = {1, 1, 1};
  int layers = 0;
  int is_cubemap = false;
  struct PyC_StringEnum pygpu_textureformat = {pygpu_textureformat_items, GPU_RGBA8};
  PyBuffer *pybuffer_obj = NULL;
  char err_out[256] = "unknown error. See console";

  static const char *_keywords[] = {"size", "layers", "is_cubemap", "format", "data", NULL};
  static _PyArg_Parser _parser = {"O|$ipO&O!:GPUTexture.__new__", _keywords, 0};
  if (!_PyArg_ParseTupleAndKeywordsFast(args,
                                        kwds,
                                        &_parser,
                                        &py_size,
                                        &layers,
                                        &is_cubemap,
                                        PyC_ParseStringEnum,
                                        &pygpu_textureformat,
                                        &BPyGPU_BufferType,
                                        &pybuffer_obj)) {
    return NULL;
  }

  int len = 1;
  if (PySequence_Check(py_size)) {
    len = PySequence_Size(py_size);
    if (PyC_AsArray(size, py_size, len, &PyLong_Type, false, "GPUTexture.__new__") == -1) {
      return NULL;
    }
  }
  else if (PyLong_Check(py_size)) {
    size[0] = PyLong_AsLong(py_size);
  }
  else {
    PyErr_SetString(PyExc_ValueError, "GPUTexture.__new__: Expected an int or tuple as first arg");
    return NULL;
  }

  void *data = NULL;
  if (pybuffer_obj) {
    if (pybuffer_obj->format != GPU_DATA_FLOAT) {
      PyErr_SetString(PyExc_ValueError,
                      "GPUTexture.__new__: Only Buffer of format `FLOAT` is currently supported");
      return NULL;
    }

    int component_len = py_texture_component_len(pygpu_textureformat.value_found);
    int component_size_expected = sizeof(float);
    size_t data_space_expected = (size_t)size[0] * size[1] * size[2] * component_len *
                                 component_size_expected;

    if (bpygpu_Buffer_size(pybuffer_obj) < data_space_expected) {
      PyErr_SetString(PyExc_ValueError, "GPUTexture.__new__: Buffer size smaller than requested");
      return NULL;
    }
    data = pybuffer_obj->buf.asvoid;
  }

  GPUTexture *tex = NULL;
  if (is_cubemap && len != 1) {
    strncpy(err_out,
            "In cubemaps the same dimension represents height, width and depth. No tuple needed",
            256);
  }
  else if (size[0] < 1 || size[1] < 1 || size[2] < 1) {
    strncpy(err_out, "Values less than 1 are not allowed in dimensions", 256);
  }
  else if (layers && len == 3) {
    strncpy(err_out, "3D textures have no layers", 256);
  }
  else if (!GPU_context_active_get()) {
    strncpy(err_out, "No active GPU context found", 256);
  }
  else {
    const char *name = "python_texture";
    if (is_cubemap) {
      if (layers) {
        tex = GPU_texture_create_cube_array(
            name, size[0], layers, 1, pygpu_textureformat.value_found, data);
      }
      else {
        tex = GPU_texture_create_cube(name, size[0], 1, pygpu_textureformat.value_found, data);
      }
    }
    else if (layers) {
      if (len == 2) {
        tex = GPU_texture_create_2d_array(
            name, size[0], size[1], layers, 1, pygpu_textureformat.value_found, data);
      }
      else {
        tex = GPU_texture_create_1d_array(
            name, size[0], layers, 1, pygpu_textureformat.value_found, data);
      }
    }
    else if (len == 3) {
      tex = GPU_texture_create_3d(name,
                                  size[0],
                                  size[1],
                                  size[2],
                                  1,
                                  pygpu_textureformat.value_found,
                                  GPU_DATA_FLOAT,
                                  NULL);
    }
    else if (len == 2) {
      tex = GPU_texture_create_2d(
          name, size[0], size[1], 1, pygpu_textureformat.value_found, data);
    }
    else {
      tex = GPU_texture_create_1d(name, size[0], 1, pygpu_textureformat.value_found, data);
    }
  }

  if (tex == NULL) {
    PyErr_Format(PyExc_RuntimeError, "gpu.texture.new(...) failed with '%s'", err_out);
    return NULL;
  }

  return BPyGPUTexture_CreatePyObject(tex);
}

PyDoc_STRVAR(py_texture_width_doc, "Width of the texture.\n\n:type: `int`");
static PyObject *py_texture_width_get(BPyGPUTexture *self, void *UNUSED(type))
{
  PY_TEXTURE_CHECK_OBJ(self);
  return PyLong_FromLong(GPU_texture_width(self->tex));
}

PyDoc_STRVAR(py_texture_height_doc, "Height of the texture.\n\n:type: `int`");
static PyObject *py_texture_height_get(BPyGPUTexture *self, void *UNUSED(type))
{
  PY_TEXTURE_CHECK_OBJ(self);
  return PyLong_FromLong(GPU_texture_height(self->tex));
}

PyDoc_STRVAR(py_texture_clear_doc,
             ".. method:: clear(format='FLOAT', value=(0.0, 0.0, 0.0, 1.0))\n"
             "\n"
             "   Fill texture with specific value.\n"
             "\n"
             "   :param format: One of these primitive types: {\n"
             "      `FLOAT`,\n"
             "      `INT`,\n"
             "      `UNSIGNED_INT`,\n"
             "      `UNSIGNED_BYTE`,\n"
             "      `UNSIGNED_INT_24_8`,\n"
             "      `10_11_11_REV`,\n"
             "   :type type: `str`\n"
             "   :arg value: sequence each representing the value to fill.\n"
             "   :type value: sequence of 1, 2, 3 or 4 values\n");
static PyObject *py_texture_clear(BPyGPUTexture *self, PyObject *args)
{
  PY_TEXTURE_CHECK_OBJ(self);
  struct PyC_StringEnum pygpu_dataformat = {bpygpu_dataformat_items};
  union {
    int i[4];
    float f[4];
  } values;

  PyObject *py_values;
  if (!PyArg_ParseTuple(args, "O&O:clear", PyC_ParseStringEnum, &pygpu_dataformat, &py_values)) {
    return NULL;
  }

  if (!PySequence_Check(py_values)) {
    return NULL;
  }

  int dimensions = PySequence_Size(py_values);
  if (dimensions > 4) {
    PyErr_SetString(PyExc_AttributeError, "too many dimensions, max is 4");
    return NULL;
  }

  memset(&values, 0, sizeof(values));
  for (int i = 0; i < dimensions; i++) {
    PyObject *ob = PySequence_GetItem(py_values, i);

    if (pygpu_dataformat.value_found == GPU_DATA_FLOAT) {
      values.f[i] = (float)PyFloat_AsDouble(ob);
    }
    else {
      values.i[i] = PyLong_AsLong(ob);
    }
    Py_DECREF(ob);
  }

  GPU_texture_clear(self->tex, pygpu_dataformat.value_found, &values);
  Py_RETURN_NONE;
}

PyDoc_STRVAR(py_texture_read_doc,
             ".. method:: read(format='FLOAT')\n"
             "\n"
             "   Creates a buffer with the value of all pixels.\n"
             "\n"
             "   :param format: One of these primitive types: {\n"
             "      `FLOAT`,\n"
             "      `INT`,\n"
             "      `UNSIGNED_INT`,\n"
             "      `UNSIGNED_BYTE`,\n"
             "      `UNSIGNED_INT_24_8`,\n"
             "      `10_11_11_REV`,\n"
             "   :type type: `str`\n");
static PyObject *py_texture_read(BPyGPUTexture *self, PyObject *args)
{
  PY_TEXTURE_CHECK_OBJ(self);
  struct PyC_StringEnum pygpu_dataformat = {bpygpu_dataformat_items};
  if (!PyArg_ParseTuple(args, "O&:clear", PyC_ParseStringEnum, &pygpu_dataformat)) {
    return NULL;
  }

  void *buf = GPU_texture_read(self->tex, pygpu_dataformat.value_found, 1);
  return (PyObject *)BPyGPU_Buffer_CreatePyObject(
      pygpu_dataformat.value_found,
      2,
      (int[2]){GPU_texture_width(self->tex), GPU_texture_height(self->tex)},
      buf);
}

#ifdef BPYGPU_USE_GPUOBJ_FREE_METHOD
PyDoc_STRVAR(py_texture_free_doc,
             ".. method:: free()\n"
             "\n"
             "   Free the texture object.\n"
             "   The texture object will no longer be accessible.\n");
static PyObject *py_texture_free(BPyGPUTexture *self)
{
  PY_TEXTURE_CHECK_OBJ(self);

  GPU_texture_free(self->tex);
  self->tex = NULL;
  Py_RETURN_NONE;
}
#endif

static void BPyGPUTexture__tp_dealloc(BPyGPUTexture *self)
{
  if (self->tex) {
    GPU_texture_free(self->tex);
  }
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyGetSetDef py_texture_getseters[] = {
    {"width", (getter)py_texture_width_get, (setter)NULL, py_texture_width_doc, NULL},
    {"height", (getter)py_texture_height_get, (setter)NULL, py_texture_height_doc, NULL},
    {NULL, NULL, NULL, NULL, NULL} /* Sentinel */
};

static struct PyMethodDef py_texture_methods[] = {
    {"clear", (PyCFunction)py_texture_clear, METH_VARARGS, py_texture_clear_doc},
    {"read", (PyCFunction)py_texture_read, METH_VARARGS, py_texture_read_doc},
#ifdef BPYGPU_USE_GPUOBJ_FREE_METHOD
    {"free", (PyCFunction)py_texture_free, METH_NOARGS, py_texture_free_doc},
#endif
    {NULL, NULL, 0, NULL},
};

PyDoc_STRVAR(
    py_texture_doc,
    ".. class:: GPUTexture(size, layers=0, is_cubemap=False, format='RGBA8', data=None)\n"
    "\n"
    "   This object gives access to off GPU textures.\n"
    "\n"
    "   :arg size: Dimensions of the texture 1D, 2D, 3D or cubemap.\n"
    "   :type size: `tuple` or `int`\n"
    "   :arg layers: Number of layers in texture array or number of cubemaps in cubemap array\n"
    "   :type layers: `int`\n"
    "   :arg is_cubemap: Indicates the creation of a cubemap texture.\n"
    "   :type is_cubemap: `int`\n"
    "   :arg format: One of these primitive types: {\n"
    "      `RGBA8UI`,\n"
    "      `RGBA8I`,\n"
    "      `RGBA8`,\n"
    "      `RGBA32UI`,\n"
    "      `RGBA32I`,\n"
    "      `RGBA32F`,\n"
    "      `RGBA16UI`,\n"
    "      `RGBA16I`,\n"
    "      `RGBA16F`,\n"
    "      `RGBA16`,\n"
    "      `RG8UI`,\n"
    "      `RG8I`,\n"
    "      `RG8`,\n"
    "      `RG32UI`,\n"
    "      `RG32I`,\n"
    "      `RG32F`,\n"
    "      `RG16UI`,\n"
    "      `RG16I`,\n"
    "      `RG16F`,\n"
    "      `RG16`,\n"
    "      `R8UI`,\n"
    "      `R8I`,\n"
    "      `R8`,\n"
    "      `R32UI`,\n"
    "      `R32I`,\n"
    "      `R32F`,\n"
    "      `R16UI`,\n"
    "      `R16I`,\n"
    "      `R16F`,\n"
    "      `R16`,\n"
    "      `R11F_G11F_B10F`,\n"
    "      `DEPTH32F_STENCIL8`,\n"
    "      `DEPTH24_STENCIL8`,\n"
    "      `SRGB8_A8`,\n"
    "      `RGB16F`,\n"
    "      `SRGB8_A8_DXT1`,\n"
    "      `SRGB8_A8_DXT3`,\n"
    "      `SRGB8_A8_DXT5`,\n"
    "      `RGBA8_DXT1`,\n"
    "      `RGBA8_DXT3`,\n"
    "      `RGBA8_DXT5`,\n"
    "      `DEPTH_COMPONENT32F`,\n"
    "      `DEPTH_COMPONENT24`,\n"
    "      `DEPTH_COMPONENT16`,\n"
    "   :type format: `str`\n"
    "   :arg data: Buffer object to fill the texture.\n"
    "   :type data: `Buffer`\n");
PyTypeObject BPyGPUTexture_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "GPUTexture",
    .tp_basicsize = sizeof(BPyGPUTexture),
    .tp_dealloc = (destructor)BPyGPUTexture__tp_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = py_texture_doc,
    .tp_methods = py_texture_methods,
    .tp_getset = py_texture_getseters,
    .tp_new = py_texture_new,
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Local API
 * \{ */

int bpygpu_ParseTexture(PyObject *o, void *p)
{
  if (o == Py_None) {
    *(GPUTexture **)p = NULL;
    return 1;
  }

  if (!BPyGPUTexture_Check(o)) {
    PyErr_Format(
        PyExc_ValueError, "expected a texture or None object, got %s", Py_TYPE(o)->tp_name);
    return 0;
  }

  if (UNLIKELY(py_texture_valid_check((BPyGPUTexture *)o) == -1)) {
    return 0;
  }

  *(GPUTexture **)p = ((BPyGPUTexture *)o)->tex;
  return 1;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Public API
 * \{ */

PyObject *BPyGPUTexture_CreatePyObject(GPUTexture *tex)
{
  BPyGPUTexture *self;

  self = PyObject_New(BPyGPUTexture, &BPyGPUTexture_Type);
  self->tex = tex;

  return (PyObject *)self;
}

/** \} */

#undef PY_TEXTURE_CHECK_OBJ
