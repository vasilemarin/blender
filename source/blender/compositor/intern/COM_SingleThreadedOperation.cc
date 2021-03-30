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
 * Copyright 2011, Blender Foundation.
 */

#include "COM_SingleThreadedOperation.h"

namespace blender::compositor {

SingleThreadedOperation::SingleThreadedOperation(DataType data_type)
    : WriteBufferOperation(data_type, false)
{
  addOutputSocket(data_type);
  flags.complex = true;
  flags.single_threaded = true;
}

void SingleThreadedOperation::initExecution()
{
  WriteBufferOperation::initExecution();
  initMutex();
}

void SingleThreadedOperation::deinitExecution()
{
  WriteBufferOperation::deinitExecution();
  deinitMutex();
}

void SingleThreadedOperation::executeRegion(rcti *rect, unsigned int UNUSED(tile_number))
{
  if (executed) {
    return;
  }
  lockMutex();
  if (executed) {
    return;
  }

  MemoryBuffer buffer = createMemoryBuffer(rect);
  MemoryBuffer *memory_buffer = getMemoryProxy()->getBuffer();
  memory_buffer->fill_from(buffer);

  unlockMutex();
  executed = true;
}

}  // namespace blender::compositor
