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

#include "COM_GlareBaseOperation.h"
#include "BLI_math.h"

namespace blender::compositor {

GlareBaseOperation::GlareBaseOperation() : SingleThreadedOperation(DataType::Color)
{
  this->addInputSocket(DataType::Color);
  this->m_settings = nullptr;
}
void GlareBaseOperation::initExecution()
{
  SingleThreadedOperation::initExecution();
  this->m_inputProgram = getInputSocketReader(0);
}

void GlareBaseOperation::deinitExecution()
{
  this->m_inputProgram = nullptr;
  SingleThreadedOperation::deinitExecution();
}

void GlareBaseOperation::update_memory_buffer(MemoryBuffer &memory_buffer, rcti *rect2)
{
  MemoryBuffer *tile = (MemoryBuffer *)this->m_inputProgram->initializeTileData(rect2);
  this->generateGlare(memory_buffer.getBuffer(), tile, this->m_settings);
}

bool GlareBaseOperation::determineDependingAreaOfInterest(rcti * /*input*/,
                                                          ReadBufferOperation *readOperation,
                                                          rcti *output)
{
  if (is_executed()) {
    return false;
  }

  rcti newInput;
  newInput.xmax = this->getWidth();
  newInput.xmin = 0;
  newInput.ymax = this->getHeight();
  newInput.ymin = 0;
  return NodeOperation::determineDependingAreaOfInterest(&newInput, readOperation, output);
}

}  // namespace blender::compositor
