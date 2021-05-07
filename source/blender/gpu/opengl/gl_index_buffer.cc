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
 * \ingroup gpu
 */

#include "gl_context.hh"
#include "gl_debug.hh"

#include "gl_index_buffer.hh"

namespace blender::gpu {

GLIndexBuf::~GLIndexBuf()
{
  GLContext::buf_free(ibo_id_);
}

void GLIndexBuf::bind()
{
  if (is_subrange_) {
    static_cast<GLIndexBuf *>(src_)->bind();
    return;
  }

  const bool allocate_on_device = ibo_id_ == 0;
  if (allocate_on_device) {
    glGenBuffers(1, &ibo_id_);
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id_);

  if (data_ != nullptr || allocate_on_device) {
    size_t size = this->size_get();
    /* Sends data to GPU. */
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data_, GL_STATIC_DRAW);
    /* No need to keep copy of data in system memory. */
    MEM_SAFE_FREE(data_);
  }
}

void GLIndexBuf::bind_as_ssbo(uint binding)
{
  bind();
  BLI_assert(ibo_id_ != 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ibo_id_);
}

}  // namespace blender::gpu
