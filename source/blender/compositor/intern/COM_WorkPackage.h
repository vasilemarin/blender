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

#include "COM_defines.h"

#include "BLI_rect.h"
#include "BLI_vector.hh"

#include "atomic_ops.h"

namespace blender::compositor {
// Forward Declarations.
class ExecutionGroup;

/**
 * \brief contains data about work that can be scheduled
 * \see WorkScheduler
 */
struct WorkPackage {
  eChunkExecutionState state = eChunkExecutionState::NotScheduled;
  CompositorPriority priority = CompositorPriority::Unset;
  /**
   * \brief executionGroup with the operations-setup to be evaluated
   */
  ExecutionGroup *execution_group;

  /**
   * \brief number of the chunk to be executed
   */
  unsigned int chunk_number;

  /**
   * Area of the execution group that the work package calculates.
   */
  rcti rect;

  /**
   * Number of work packages this instance is still waiting for before it can be scheduled.
   * During execution this counter will decrease and when it hits zero this workpackage will be
   * scheduled.
   */
  int32_t num_parents = 0;

  /**
   * Vector of work packages that are waiting for my completion.
   */
  blender::Vector<WorkPackage *> children;
  blender::Vector<WorkPackage *> parents;

  bool parent_finished()
  {
    return atomic_add_and_fetch_int32(&num_parents, -1) == 0;
  }

  void add_child(WorkPackage *child)
  {
    children.append(child);
    child->parents.append(this);
    child->num_parents++;
  }

  std::string str(int indent = 0) const;

#ifdef WITH_CXX_GUARDEDALLOC
  MEM_CXX_CLASS_ALLOC_FUNCS("COM:WorkPackage")
#endif
};

std::ostream &operator<<(std::ostream &os, const WorkPackage &WorkPackage);

}  // namespace blender::compositor
