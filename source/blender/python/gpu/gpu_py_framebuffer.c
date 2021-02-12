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
 * This file defines the framebuffer functionalities of the 'gpu' module
 * used for off-screen OpenGL rendering.
 *
 * - Use ``bpygpu_`` for local API.
 * - Use ``BPyGPU`` for public API.
 */

#include <Python.h>

#include "GPU_context.h"
#include "GPU_framebuffer.h"
#include "GPU_init_exit.h"

#include "../generic/py_capi_utils.h"
#include "../generic/python_utildefines.h"
#include "../mathutils/mathutils.h"

#include "gpu_py_api.h"
#include "gpu_py_texture.h"

#include "gpu_py_framebuffer.h" /* own include */

/* -------------------------------------------------------------------- */
/** \name GPUFrameBuffer Common Utilities
 * \{ */

static int py_framebuffer_valid_check(BPyGPUFrameBuffer *bpygpu_fb)
{
  if (UNLIKELY(bpygpu_fb->fb == NULL)) {
    PyErr_SetString(PyExc_ReferenceError,
#ifdef BPYGPU_USE_GPUOBJ_FREE_METHOD
                    "GPU framebuffer was freed, no further access is valid"
#else
                    "GPU framebuffer: internal error"
#endif
    );
    return -1;
  }
  return 0;
}

#define PY_FRAMEBUFFER_CHECK_OBJ(bpygpu) \
  { \
    if (UNLIKELY(py_framebuffer_valid_check(bpygpu) == -1)) { \
      return NULL; \
    } \
  } \
  ((void)0)

static void py_framebuffer_free_if_possible(GPUFrameBuffer *fb)
{
  if (!fb) {
    return;
  }

  if (GPU_is_init()) {
    GPU_framebuffer_free(fb);
  }
  else {
    printf("PyFramebuffer freed after the context has been destroyed.\n");
  }
}

/* Keep less than or equal to #FRAMEBUFFER_STACK_DEPTH */
#define GPU_PY_FRAMEBUFFER_STACK_LEN 16

static bool py_framebuffer_stack_push_and_bind_or_error(GPUFrameBuffer *fb)
{
  if (GPU_framebuffer_stack_level_get() >= GPU_PY_FRAMEBUFFER_STACK_LEN) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "Maximum framebuffer stack depth " STRINGIFY(GPU_PY_FRAMEBUFFER_STACK_LEN) " reached");
    return false;
  }
  GPU_framebuffer_push(GPU_framebuffer_active_get());
  GPU_framebuffer_bind(fb);
  return true;
}

static bool py_framebuffer_stack_pop_and_restore_or_error(GPUFrameBuffer *fb)
{
  if (GPU_framebuffer_stack_level_get() == 0) {
    PyErr_SetString(PyExc_RuntimeError, "Minimum framebuffer stack depth reached");
    return false;
  }

  if (fb && !GPU_framebuffer_bound(fb)) {
    PyErr_SetString(PyExc_RuntimeError, "Framebuffer is not bound");
    return false;
  }

  GPUFrameBuffer *fb_prev = GPU_framebuffer_pop();
  GPU_framebuffer_bind(fb_prev);
  return true;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Stack (Context Manager)
 *
 * Safer alternative to ensure balanced push/pop calls.
 *
 * \{ */

typedef struct {
  PyObject_HEAD /* required python macro */
      BPyGPUFrameBuffer *py_fb;
  int level;
} PyFrameBufferStackContext;

static void py_framebuffer_stack_context__tp_dealloc(PyFrameBufferStackContext *self)
{
  Py_DECREF(self->py_fb);
  PyObject_DEL(self);
}

static PyObject *py_framebuffer_stack_context_enter(PyFrameBufferStackContext *self)
{
  PY_FRAMEBUFFER_CHECK_OBJ(self->py_fb);

  /* sanity - should never happen */
  if (self->level != -1) {
    PyErr_SetString(PyExc_RuntimeError, "Already in use");
    return NULL;
  }

  if (!py_framebuffer_stack_push_and_bind_or_error(self->py_fb->fb)) {
    return NULL;
  }

  self->level = GPU_framebuffer_stack_level_get();
  Py_RETURN_NONE;
}

static PyObject *py_framebuffer_stack_context_exit(PyFrameBufferStackContext *self,
                                                   PyObject *UNUSED(args))
{
  PY_FRAMEBUFFER_CHECK_OBJ(self->py_fb);

  /* sanity - should never happen */
  if (self->level == -1) {
    fprintf(stderr, "Not yet in use\n");
    return NULL;
  }

  const int level = GPU_framebuffer_stack_level_get();
  if (level != self->level) {
    fprintf(stderr, "Level of bind mismatch, expected %d, got %d\n", self->level, level);
  }

  if (!py_framebuffer_stack_pop_and_restore_or_error(self->py_fb->fb)) {
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyMethodDef py_framebuffer_stack_context_methods[] = {
    {"__enter__", (PyCFunction)py_framebuffer_stack_context_enter, METH_NOARGS},
    {"__exit__", (PyCFunction)py_framebuffer_stack_context_exit, METH_VARARGS},
    {NULL},
};

static PyTypeObject py_framebuffer_stack_context_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "GPUFrameBufferStackContext",
    .tp_basicsize = sizeof(PyFrameBufferStackContext),
    .tp_dealloc = (destructor)py_framebuffer_stack_context__tp_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = py_framebuffer_stack_context_methods,
};

PyDoc_STRVAR(py_framebuffer_bind_doc,
             ".. function:: bind()\n"
             "\n"
             "   Context manager to ensure balanced bind calls, even in the case of an error.\n");
static PyObject *py_framebuffer_bind(BPyGPUFrameBuffer *self)
{
  PyFrameBufferStackContext *ret = PyObject_New(PyFrameBufferStackContext,
                                                &py_framebuffer_stack_context_Type);
  ret->py_fb = self;
  ret->level = -1;
  Py_INCREF(self);
  return (PyObject *)ret;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name GPUFramebuffer Type
 * \{ */

static bool py_framebuffer_parse_arg(PyObject *o, GPUTexture **r_tex, int *r_layer, int *r_mip)
{
  if (!o || o == Py_None) {
    return true;
  }

  if (BPyGPUTexture_Check(o)) {
    if (!bpygpu_ParseTexture(o, r_tex)) {
      return false;
    }
  }
  else {
    static const char *_keywords[] = {"texture", "layer", "mip", NULL};
    static _PyArg_Parser _parser = {"|$O&ii:attachment", _keywords, 0};
    PyObject *tuple = PyTuple_New(0);
    int ret = _PyArg_ParseTupleAndKeywordsFast(
        tuple, o, &_parser, bpygpu_ParseTexture, r_tex, r_layer, r_mip);
    Py_DECREF(tuple);
    if (!ret) {
      return false;
    }
  }
  return true;
}

static PyObject *py_framebuffer_new(PyTypeObject *UNUSED(self), PyObject *args, PyObject *kwds)
{
  BPYGPU_IS_INIT_OR_ERROR_OBJ;
  if (!GPU_context_active_get()) {
    PyErr_SetString(PyExc_RuntimeError, "No active GPU context found");
    return NULL;
  }

  PyObject *depth_attachment = NULL;
  PyObject *color_attachements = NULL;

  if (!PyArg_ParseTuple(
          args, "OO:GPUFrameBuffer.__new__", &depth_attachment, &color_attachements)) {
    return NULL;
  }

  struct {
    GPUTexture *tex;
    int layer;
    int mip;
  } slot[7] = {{.layer = -1},
               {.layer = -1},
               {.layer = -1},
               {.layer = -1},
               {.layer = -1},
               {.layer = -1},
               {.layer = -1}};

  if (!py_framebuffer_parse_arg(depth_attachment, &slot[6].tex, &slot[6].layer, &slot[6].mip)) {
    return NULL;
  }
  else if (slot[6].tex && !GPU_texture_depth(slot[6].tex)) {
    PyErr_SetString(PyExc_ValueError, "Depth texture with incompatible format");
    return NULL;
  }

  if (color_attachements && color_attachements != Py_None) {
    if (PySequence_Check(color_attachements)) {
      int len = PySequence_Size(color_attachements);
      for (int i = 0; i < len; i++) {
        PyObject *o = PySequence_GetItem(color_attachements, i);
        bool ok = py_framebuffer_parse_arg(o, &slot[i].tex, &slot[i].layer, &slot[i].mip);
        Py_DECREF(o);
        if (!ok) {
          return NULL;
        }
      }
    }
    else {
      if (!py_framebuffer_parse_arg(
              color_attachements, &slot[0].tex, &slot[0].layer, &slot[0].mip)) {
        return NULL;
      }
    }
  }

  for (int i = 0; i < ARRAY_SIZE(slot); i++) {
    if (slot[i].tex == NULL) {
      /* GPU_ATTACHMENT_LEAVE */
      slot[i].mip = -1;
    }
  }

  GPUFrameBuffer *fb_python = NULL;
  GPU_framebuffer_ensure_config(
      &fb_python,
      {
          /* Depth texture. */
          GPU_ATTACHMENT_TEXTURE_LAYER_MIP(slot[6].tex, slot[6].layer, slot[6].mip),
          /* Color textures. */
          GPU_ATTACHMENT_TEXTURE_LAYER_MIP(slot[0].tex, slot[0].layer, slot[0].mip),
          GPU_ATTACHMENT_TEXTURE_LAYER_MIP(slot[1].tex, slot[1].layer, slot[1].mip),
          GPU_ATTACHMENT_TEXTURE_LAYER_MIP(slot[2].tex, slot[2].layer, slot[2].mip),
          GPU_ATTACHMENT_TEXTURE_LAYER_MIP(slot[3].tex, slot[3].layer, slot[3].mip),
          GPU_ATTACHMENT_TEXTURE_LAYER_MIP(slot[4].tex, slot[4].layer, slot[4].mip),
          GPU_ATTACHMENT_TEXTURE_LAYER_MIP(slot[5].tex, slot[5].layer, slot[5].mip),
      });

  return BPyGPUFrameBuffer_CreatePyObject(fb_python);
}

PyDoc_STRVAR(py_framebuffer_is_bound_doc,
             ".. method:: is_bound()\n"
             "\n"
             "   Checks if this is the active framebuffer in the context.\n"
             "\n");
static PyObject *py_framebuffer_is_bound(BPyGPUFrameBuffer *self, void *UNUSED(type))
{
  PY_FRAMEBUFFER_CHECK_OBJ(self);
  return PyBool_FromLong(GPU_framebuffer_bound(self->fb));
}

PyDoc_STRVAR(py_framebuffer_clear_doc,
             ".. method:: clear(color=(0.0, 0.0, 0.0, 1.0), depth=None, stencil=None,)\n"
             "\n"
             "   Fill color, depth and stencil textures with specific value.\n"
             "\n"
             "   :arg color: float sequence each representing ``(r, g, b, a)``.\n"
             "   :type color: sequence of 3 or 4 floats\n"
             "   :arg depth: depth value.\n"
             "   :type depth: `float`\n"
             "   :arg stencil: stencil value.\n"
             "   :type stencil: `int`\n");
static PyObject *py_framebuffer_clear(BPyGPUFrameBuffer *self, PyObject *args, PyObject *kwds)
{
  PY_FRAMEBUFFER_CHECK_OBJ(self);

  if (!GPU_framebuffer_bound(self->fb)) {
    return NULL;
  }

  PyObject *py_col = NULL;
  PyObject *py_depth = NULL;
  PyObject *py_stencil = NULL;

  static const char *_keywords[] = {"color", "depth", "stencil", NULL};
  static _PyArg_Parser _parser = {"|OOO:texture_attach", _keywords, 0};
  if (!_PyArg_ParseTupleAndKeywordsFast(args, kwds, &_parser, &py_col, &py_depth, &py_stencil)) {
    return NULL;
  }

  eGPUFrameBufferBits buffers = 0;
  float col[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float depth = 1.0f;
  uint stencil = 0;

  if (py_col && py_col != Py_None) {
    if (mathutils_array_parse(col, 3, 4, py_col, "GPUFrameBuffer.clear(), invalid 'color' arg") ==
        -1) {
      return NULL;
    }
    buffers |= GPU_COLOR_BIT;
  }

  if (py_depth && py_depth != Py_None) {
    depth = PyFloat_AsDouble(py_depth);
    if (PyErr_Occurred()) {
      return NULL;
    }
    buffers |= GPU_DEPTH_BIT;
  }

  if (py_stencil && py_stencil != Py_None) {
    if ((stencil = PyC_Long_AsU32(py_stencil)) == (uint)-1) {
      return NULL;
    }
    buffers |= GPU_STENCIL_BIT;
  }

  GPU_framebuffer_clear(self->fb, buffers, col, depth, stencil);
  Py_RETURN_NONE;
}

PyDoc_STRVAR(py_framebuffer_viewport_set_doc,
             ".. function:: viewport_set(x, y, xsize, ysize)\n"
             "\n"
             "   Set the viewport for this framebuffer object.\n"
             "   Note: The viewport state is not saved upon framebuffer rebind.\n"
             "\n"
             "   :param x, y: lower left corner of the viewport_set rectangle, in pixels.\n"
             "   :param xsize, ysize: width and height of the viewport_set.\n"
             "   :type x, y, xsize, ysize: `int`\n");
static PyObject *py_framebuffer_viewport_set(BPyGPUFrameBuffer *self,
                                             PyObject *args,
                                             void *UNUSED(type))
{
  int x, y, xsize, ysize;
  if (!PyArg_ParseTuple(args, "iiii:viewport_set", &x, &y, &xsize, &ysize)) {
    return NULL;
  }

  GPU_framebuffer_viewport_set(self->fb, x, y, xsize, ysize);
  Py_RETURN_NONE;
}

PyDoc_STRVAR(py_framebuffer_viewport_get_doc,
             ".. function:: viewport_get()\n"
             "\n"
             "   Returns position and dimension to current viewport.\n");
static PyObject *py_framebuffer_viewport_get(BPyGPUFrameBuffer *self, void *UNUSED(type))
{
  PY_FRAMEBUFFER_CHECK_OBJ(self);
  int viewport[4];
  GPU_framebuffer_viewport_get(self->fb, viewport);

  PyObject *ret = PyTuple_New(4);
  PyTuple_SET_ITEMS(ret,
                    PyLong_FromLong(viewport[0]),
                    PyLong_FromLong(viewport[1]),
                    PyLong_FromLong(viewport[2]),
                    PyLong_FromLong(viewport[3]));
  return ret;
}

#ifdef BPYGPU_USE_GPUOBJ_FREE_METHOD
PyDoc_STRVAR(py_framebuffer_free_doc,
             ".. method:: free()\n"
             "\n"
             "   Free the framebuffer object.\n"
             "   The framebuffer will no longer be accessible.\n");
static PyObject *py_framebuffer_free(BPyGPUFrameBuffer *self)
{
  PY_FRAMEBUFFER_CHECK_OBJ(self);
  py_framebuffer_free_if_possible(self->fb);
  self->fb = NULL;
  Py_RETURN_NONE;
}
#endif

static void BPyGPUFrameBuffer__tp_dealloc(BPyGPUFrameBuffer *self)
{
  py_framebuffer_free_if_possible(self->fb);
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyGetSetDef py_framebuffer_getseters[] = {
    {"is_bound", (getter)py_framebuffer_is_bound, (setter)NULL, py_framebuffer_is_bound_doc, NULL},
    {NULL, NULL, NULL, NULL, NULL} /* Sentinel */
};

static struct PyMethodDef py_framebuffer_methods[] = {
    {"bind", (PyCFunction)py_framebuffer_bind, METH_NOARGS, py_framebuffer_bind_doc},
    {"clear",
     (PyCFunction)py_framebuffer_clear,
     METH_VARARGS | METH_KEYWORDS,
     py_framebuffer_clear_doc},
    {"viewport_set",
     (PyCFunction)py_framebuffer_viewport_set,
     METH_NOARGS,
     py_framebuffer_viewport_set_doc},
    {"viewport_get",
     (PyCFunction)py_framebuffer_viewport_get,
     METH_VARARGS,
     py_framebuffer_viewport_get_doc},
#ifdef BPYGPU_USE_GPUOBJ_FREE_METHOD
    {"free", (PyCFunction)py_framebuffer_free, METH_NOARGS, py_framebuffer_free_doc},
#endif
    {NULL, NULL, 0, NULL},
};

PyDoc_STRVAR(py_framebuffer_doc,
             ".. class:: GPUFrameBuffer(depth_attachment, color_attachments)\n"
             "\n"
             "   This object gives access to framebuffer functionallities.\n"
             "   When a 'layer' is specified in a argument, a single layer of a 3D or array "
             "texture is attached to the frame-buffer.\n"
             "   For cube map textures, layer is translated into a cube map face.\n"
             "\n"
             "   :arg depth_attachment: GPUTexture to attach or a `dict` containing keywords: "
             "'texture', 'layer' and 'mip'.\n"
             "   :type depth_attachment: :class:`gpu.types.GPUTexture`, `dict` or `Nonetype`\n"
             "   :arg color_attachments: Tuple where each item can be a GPUTexture or a `dict` "
             "containing keywords: 'texture', 'layer' and 'mip'.\n"
             "   :type color_attachments: `tuple` or `Nonetype`\n");
PyTypeObject BPyGPUFrameBuffer_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "GPUFrameBuffer",
    .tp_basicsize = sizeof(BPyGPUFrameBuffer),
    .tp_dealloc = (destructor)BPyGPUFrameBuffer__tp_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = py_framebuffer_doc,
    .tp_methods = py_framebuffer_methods,
    .tp_getset = py_framebuffer_getseters,
    .tp_new = py_framebuffer_new,
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Public API
 * \{ */

PyObject *BPyGPUFrameBuffer_CreatePyObject(GPUFrameBuffer *fb)
{
  BPyGPUFrameBuffer *self;

  self = PyObject_New(BPyGPUFrameBuffer, &BPyGPUFrameBuffer_Type);
  self->fb = fb;

  return (PyObject *)self;
}

/** \} */

#undef PY_FRAMEBUFFER_CHECK_OBJ
