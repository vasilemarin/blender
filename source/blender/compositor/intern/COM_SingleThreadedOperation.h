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

#pragma once

#include "COM_WriteBufferOperation.h"

namespace blender::compositor {

class SingleThreadedOperation : public WriteBufferOperation {
 private:
  bool executed = false;

 protected:
  bool is_executed()
  {
    return executed;
  }

 public:
  SingleThreadedOperation(DataType data_type);

  /**
   * Initialize the execution
   */
  void initExecution() override;

  /**
   * Deinitialize the execution
   */
  void deinitExecution() override;

  void executeRegion(rcti *rect, unsigned int tile_number) override;

  virtual void update_memory_buffer(MemoryBuffer &memory_buffer, rcti *rect) = 0;
};

}  // namespace blender::compositor
