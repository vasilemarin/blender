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
 * This file defines the gpu.state API.
 *
 * - Use ``bpygpu_`` for local API.
 * - Use ``BPyGPU`` for public API.
 */

#include <Python.h>

#include "BLI_utildefines.h"

#include "MEM_guardedalloc.h"

#include "GPU_texture.h"

#include "../generic/py_capi_utils.h"

#include "gpu_py.h"

#include "gpu_py_buffer.h"

/* -------------------------------------------------------------------- */
/** \name Utility Functions
 * \{ */

static int py_buffer_format_size(eGPUDataFormat format)
{
  switch (format) {
    case GPU_DATA_FLOAT:
      return sizeof(float);
    case GPU_DATA_INT:
      return sizeof(int);
    case GPU_DATA_UNSIGNED_BYTE:
      return sizeof(char);
    case GPU_DATA_UNSIGNED_INT:
    case GPU_DATA_UNSIGNED_INT_24_8:
    case GPU_DATA_10_11_11_REV:
      return sizeof(uint);
    default:
      BLI_assert(!"Unhandled data format");
      return -1;
  }
}

static bool py_buffer_dimensions_compare(int ndim, const int *dim1, const Py_ssize_t *dim2)
{
  for (int i = 0; i < ndim; i++) {
    if (dim1[i] != dim2[i]) {
      return false;
    }
  }
  return true;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name PyBuffer API
 * \{ */

static PyBuffer *py_buffer_make_from_data(
    PyObject *parent, eGPUDataFormat format, int ndimensions, int *dimensions, void *buf)
{
  PyBuffer *buffer = (PyBuffer *)PyObject_NEW(PyBuffer, &BPyGPU_BufferType);

  Py_XINCREF(parent);
  buffer->parent = parent;
  buffer->ndimensions = ndimensions;
  buffer->dimensions = MEM_mallocN(ndimensions * sizeof(int), "PyBuffer dimensions");
  memcpy(buffer->dimensions, dimensions, ndimensions * sizeof(int));
  buffer->format = format;
  buffer->buf.asvoid = buf;

  return buffer;
}

static PyObject *py_buffer_sq_item(PyBuffer *self, int i)
{
  if (i >= self->dimensions[0] || i < 0) {
    PyErr_SetString(PyExc_IndexError, "array index out of range");
    return NULL;
  }

  if (self->ndimensions == 1) {
    switch (self->format) {
      case GPU_DATA_FLOAT:
        return Py_BuildValue("f", self->buf.asfloat[i]);
      case GPU_DATA_INT:
        return Py_BuildValue("i", self->buf.asint[i]);
      case GPU_DATA_UNSIGNED_BYTE:
        return Py_BuildValue("B", self->buf.asbyte[i]);
      case GPU_DATA_UNSIGNED_INT:
      case GPU_DATA_UNSIGNED_INT_24_8:
      case GPU_DATA_10_11_11_REV:
        return Py_BuildValue("I", self->buf.asuint[i]);
    }
  }
  else {
    int offset = i * py_buffer_format_size(self->format);
    for (int j = 1; j < self->ndimensions; j++) {
      offset *= self->dimensions[j];
    }

    return (PyObject *)py_buffer_make_from_data((PyObject *)self,
                                                self->format,
                                                self->ndimensions - 1,
                                                self->dimensions + 1,
                                                self->buf.asbyte + offset);
  }

  return NULL;
}

static PyObject *py_buffer_to_list(PyBuffer *self)
{
  int i, len = self->dimensions[0];
  PyObject *list = PyList_New(len);

  for (i = 0; i < len; i++) {
    PyList_SET_ITEM(list, i, py_buffer_sq_item(self, i));
  }

  return list;
}

static PyObject *py_buffer_to_list_recursive(PyBuffer *self)
{
  PyObject *list;

  if (self->ndimensions > 1) {
    int i, len = self->dimensions[0];
    list = PyList_New(len);

    for (i = 0; i < len; i++) {
      PyBuffer *sub = (PyBuffer *)py_buffer_sq_item(self, i);
      PyList_SET_ITEM(list, i, py_buffer_to_list_recursive(sub));
      Py_DECREF(sub);
    }
  }
  else {
    list = py_buffer_to_list(self);
  }

  return list;
}

static PyObject *py_buffer_dimensions(PyBuffer *self, void *UNUSED(arg))
{
  PyObject *list = PyList_New(self->ndimensions);
  int i;

  for (i = 0; i < self->ndimensions; i++) {
    PyList_SET_ITEM(list, i, PyLong_FromLong(self->dimensions[i]));
  }

  return list;
}

static void py_buffer_tp_dealloc(PyBuffer *self)
{
  if (self->parent) {
    Py_DECREF(self->parent);
  }
  else {
    MEM_freeN(self->buf.asvoid);
  }

  MEM_freeN(self->dimensions);

  PyObject_DEL(self);
}

static PyObject *py_buffer_tp_repr(PyBuffer *self)
{
  PyObject *repr;

  PyObject *list = py_buffer_to_list_recursive(self);
  const char *typestr = PyC_StringEnum_find_id(bpygpu_dataformat_items, self->format);

  repr = PyUnicode_FromFormat("Buffer(%s, %R)", typestr, list);
  Py_DECREF(list);

  return repr;
}

static int py_buffer_sq_ass_item(PyBuffer *self, int i, PyObject *v);

static int py_buffer_ass_slice(PyBuffer *self, int begin, int end, PyObject *seq)
{
  PyObject *item;
  int count, err = 0;

  if (begin < 0) {
    begin = 0;
  }
  if (end > self->dimensions[0]) {
    end = self->dimensions[0];
  }
  if (begin > end) {
    begin = end;
  }

  if (!PySequence_Check(seq)) {
    PyErr_Format(PyExc_TypeError,
                 "buffer[:] = value, invalid assignment. "
                 "Expected a sequence, not an %.200s type",
                 Py_TYPE(seq)->tp_name);
    return -1;
  }

  /* re-use count var */
  if ((count = PySequence_Size(seq)) != (end - begin)) {
    PyErr_Format(PyExc_TypeError,
                 "buffer[:] = value, size mismatch in assignment. "
                 "Expected: %d (given: %d)",
                 count,
                 end - begin);
    return -1;
  }

  for (count = begin; count < end; count++) {
    item = PySequence_GetItem(seq, count - begin);
    if (item) {
      err = py_buffer_sq_ass_item(self, count, item);
      Py_DECREF(item);
    }
    else {
      err = -1;
    }
    if (err) {
      break;
    }
  }
  return err;
}

#define MAX_DIMENSIONS 256
static PyObject *py_buffer_tp_new(PyTypeObject *UNUSED(type), PyObject *args, PyObject *kwds)
{
  PyObject *length_ob, *init = NULL;
  PyBuffer *buffer = NULL;
  int dimensions[MAX_DIMENSIONS];

  Py_ssize_t i, ndimensions = 0;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "Buffer(): takes no keyword args");
    return NULL;
  }

  const struct PyC_StringEnum pygpu_dataformat = {bpygpu_dataformat_items, GPU_DATA_FLOAT};
  if (!PyArg_ParseTuple(
          args, "O&O|O: Buffer", PyC_ParseStringEnum, &pygpu_dataformat, &length_ob, &init)) {
    return NULL;
  }

  if (PyLong_Check(length_ob)) {
    ndimensions = 1;
    if (((dimensions[0] = PyLong_AsLong(length_ob)) < 1)) {
      PyErr_SetString(PyExc_AttributeError, "dimension must be greater than or equal to 1");
      return NULL;
    }
  }
  else if (PySequence_Check(length_ob)) {
    ndimensions = PySequence_Size(length_ob);
    if (ndimensions > MAX_DIMENSIONS) {
      PyErr_SetString(PyExc_AttributeError,
                      "too many dimensions, max is " STRINGIFY(MAX_DIMENSIONS));
      return NULL;
    }
    if (ndimensions < 1) {
      PyErr_SetString(PyExc_AttributeError, "sequence must have at least one dimension");
      return NULL;
    }
    for (i = 0; i < ndimensions; i++) {
      PyObject *ob = PySequence_GetItem(length_ob, i);
      if (!PyLong_Check(ob)) {
        PyErr_Format(PyExc_TypeError,
                     "invalid dimension %i, expected an int, not a %.200s",
                     i,
                     Py_TYPE(ob)->tp_name);
        Py_DECREF(ob);
        return NULL;
      }
      else {
        dimensions[i] = PyLong_AsLong(ob);
      }
      Py_DECREF(ob);

      if (dimensions[i] < 1) {
        PyErr_SetString(PyExc_AttributeError, "dimension must be greater than or equal to 1");
        return NULL;
      }
    }
  }
  else {
    PyErr_Format(PyExc_TypeError,
                 "invalid second argument argument expected a sequence "
                 "or an int, not a %.200s",
                 Py_TYPE(length_ob)->tp_name);
    return NULL;
  }

  if (init && PyObject_CheckBuffer(init)) {
    Py_buffer pybuffer;

    if (PyObject_GetBuffer(init, &pybuffer, PyBUF_ND | PyBUF_FORMAT) == -1) {
      /* PyObject_GetBuffer raise a PyExc_BufferError */
      return NULL;
    }

    if (ndimensions != pybuffer.ndim ||
        !py_buffer_dimensions_compare(ndimensions, dimensions, pybuffer.shape)) {
      PyErr_Format(PyExc_TypeError, "array size does not match");
    }
    else {
      buffer = py_buffer_make_from_data(
          init, pygpu_dataformat.value_found, pybuffer.ndim, dimensions, pybuffer.buf);
    }

    PyBuffer_Release(&pybuffer);
  }
  else {
    buffer = BPyGPU_Buffer_CreatePyObject(
        pygpu_dataformat.value_found, ndimensions, dimensions, NULL);
    if (init && py_buffer_ass_slice(buffer, 0, dimensions[0], init)) {
      Py_DECREF(buffer);
      return NULL;
    }
  }

  return (PyObject *)buffer;
}

/* PyBuffer sequence methods */

static int py_buffer_sq_length(PyBuffer *self)
{
  return self->dimensions[0];
}

static PyObject *py_buffer_slice(PyBuffer *self, int begin, int end)
{
  PyObject *list;
  int count;

  if (begin < 0) {
    begin = 0;
  }
  if (end > self->dimensions[0]) {
    end = self->dimensions[0];
  }
  if (begin > end) {
    begin = end;
  }

  list = PyList_New(end - begin);

  for (count = begin; count < end; count++) {
    PyList_SET_ITEM(list, count - begin, py_buffer_sq_item(self, count));
  }
  return list;
}

static int py_buffer_sq_ass_item(PyBuffer *self, int i, PyObject *v)
{
  if (i >= self->dimensions[0] || i < 0) {
    PyErr_SetString(PyExc_IndexError, "array assignment index out of range");
    return -1;
  }

  if (self->ndimensions != 1) {
    PyBuffer *row = (PyBuffer *)py_buffer_sq_item(self, i);

    if (row) {
      const int ret = py_buffer_ass_slice(row, 0, self->dimensions[1], v);
      Py_DECREF(row);
      return ret;
    }

    return -1;
  }

  switch (self->format) {
    case GPU_DATA_FLOAT:
      return PyArg_Parse(v, "f:Expected floats", &self->buf.asfloat[i]) ? 0 : -1;
    case GPU_DATA_INT:
      return PyArg_Parse(v, "i:Expected ints", &self->buf.asint[i]) ? 0 : -1;
    case GPU_DATA_UNSIGNED_BYTE:
      return PyArg_Parse(v, "b:Expected ints", &self->buf.asbyte[i]) ? 0 : -1;
    case GPU_DATA_UNSIGNED_INT:
    case GPU_DATA_UNSIGNED_INT_24_8:
    case GPU_DATA_10_11_11_REV:
      return PyArg_Parse(v, "b:Expected ints", &self->buf.asuint[i]) ? 0 : -1;
    default:
      return 0; /* should never happen */
  }
}

static PyObject *py_buffer_mp_subscript(PyBuffer *self, PyObject *item)
{
  if (PyIndex_Check(item)) {
    Py_ssize_t i;
    i = PyNumber_AsSsize_t(item, PyExc_IndexError);
    if (i == -1 && PyErr_Occurred()) {
      return NULL;
    }
    if (i < 0) {
      i += self->dimensions[0];
    }
    return py_buffer_sq_item(self, i);
  }
  if (PySlice_Check(item)) {
    Py_ssize_t start, stop, step, slicelength;

    if (PySlice_GetIndicesEx(item, self->dimensions[0], &start, &stop, &step, &slicelength) < 0) {
      return NULL;
    }

    if (slicelength <= 0) {
      return PyTuple_New(0);
    }
    if (step == 1) {
      return py_buffer_slice(self, start, stop);
    }

    PyErr_SetString(PyExc_IndexError, "slice steps not supported with vectors");
    return NULL;
  }

  PyErr_Format(
      PyExc_TypeError, "buffer indices must be integers, not %.200s", Py_TYPE(item)->tp_name);
  return NULL;
}

static int py_buffer_mp_ass_subscript(PyBuffer *self, PyObject *item, PyObject *value)
{
  if (PyIndex_Check(item)) {
    Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
    if (i == -1 && PyErr_Occurred()) {
      return -1;
    }
    if (i < 0) {
      i += self->dimensions[0];
    }
    return py_buffer_sq_ass_item(self, i, value);
  }
  if (PySlice_Check(item)) {
    Py_ssize_t start, stop, step, slicelength;

    if (PySlice_GetIndicesEx(item, self->dimensions[0], &start, &stop, &step, &slicelength) < 0) {
      return -1;
    }

    if (step == 1) {
      return py_buffer_ass_slice(self, start, stop, value);
    }

    PyErr_SetString(PyExc_IndexError, "slice steps not supported with vectors");
    return -1;
  }

  PyErr_Format(
      PyExc_TypeError, "buffer indices must be integers, not %.200s", Py_TYPE(item)->tp_name);
  return -1;
}

static PyMethodDef py_buffer_methods[] = {
    {"to_list",
     (PyCFunction)py_buffer_to_list_recursive,
     METH_NOARGS,
     "return the buffer as a list"},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef py_buffer_getseters[] = {
    {"dimensions", (getter)py_buffer_dimensions, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PySequenceMethods py_buffer_SeqMethods = {
    (lenfunc)py_buffer_sq_length,           /*sq_length */
    (binaryfunc)NULL,                       /*sq_concat */
    (ssizeargfunc)NULL,                     /*sq_repeat */
    (ssizeargfunc)py_buffer_sq_item,        /*sq_item */
    (ssizessizeargfunc)NULL,                /*sq_slice, deprecated, handled in py_buffer_sq_item */
    (ssizeobjargproc)py_buffer_sq_ass_item, /*sq_ass_item */
    (ssizessizeobjargproc)NULL, /* sq_ass_slice, deprecated handled in py_buffer_sq_ass_item */
    (objobjproc)NULL,           /* sq_contains */
    (binaryfunc)NULL,           /* sq_inplace_concat */
    (ssizeargfunc)NULL,         /* sq_inplace_repeat */
};

static PyMappingMethods py_buffer_AsMapping = {
    (lenfunc)py_buffer_sq_length,
    (binaryfunc)py_buffer_mp_subscript,
    (objobjargproc)py_buffer_mp_ass_subscript,
};

PyDoc_STRVAR(py_buffer_doc,
             ".. class:: Buffer(format, dimensions, data)\n"
             "\n"
             "   For Python access to GPU functions requiring a pointer.\n"
             "\n"
             "   :arg format: One of these primitive types: {\n"
             "      `FLOAT`,\n"
             "      `INT`,\n"
             "      `UNSIGNED_INT`,\n"
             "      `UNSIGNED_BYTE`,\n"
             "      `UNSIGNED_INT_24_8`,\n"
             "      `10_11_11_REV`,\n"
             "   :type type: `str`\n"
             "   :arg dimensions: Array describing the dimensions.\n"
             "   :type dimensions: `int`\n"
             "   :arg data: Optional data array.\n"
             "   :type data: `array`\n");
PyTypeObject BPyGPU_BufferType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "Buffer",
    .tp_basicsize = sizeof(PyBuffer),
    .tp_dealloc = (destructor)py_buffer_tp_dealloc,
    .tp_repr = (reprfunc)py_buffer_tp_repr,
    .tp_as_sequence = &py_buffer_SeqMethods,
    .tp_as_mapping = &py_buffer_AsMapping,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = py_buffer_doc,
    .tp_methods = py_buffer_methods,
    .tp_getset = py_buffer_getseters,
    .tp_new = py_buffer_tp_new,
};

static size_t py_buffer_calc_size(int format, int ndimensions, int *dimensions)
{
  size_t r_size = py_buffer_format_size(format);

  for (int i = 0; i < ndimensions; i++) {
    r_size *= dimensions[i];
  }

  return r_size;
}

size_t bpygpu_Buffer_size(PyBuffer *buffer)
{
  return py_buffer_calc_size(buffer->format, buffer->ndimensions, buffer->dimensions);
}

/**
 * Create a buffer object
 *
 * \param dimensions: An array of ndimensions integers representing the size of each dimension.
 * \param initbuffer: When not NULL holds a contiguous buffer
 * with the correct format from which the buffer will be initialized
 */
PyBuffer *BPyGPU_Buffer_CreatePyObject(int format,
                                       int ndimensions,
                                       int *dimensions,
                                       void *initbuffer)
{
  PyBuffer *buffer;
  void *buf = NULL;
  size_t size = py_buffer_calc_size(format, ndimensions, dimensions);

  buf = MEM_mallocN(size, "PyBuffer buffer");
  buffer = py_buffer_make_from_data(NULL, format, ndimensions, dimensions, buf);

  if (initbuffer) {
    memcpy(buffer->buf.asvoid, initbuffer, size);
  }
  else {
    memset(buffer->buf.asvoid, 0, size);
  }
  return buffer;
}

/** \} */
