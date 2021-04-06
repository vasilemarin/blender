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
 * Copyright 2021, Blender Foundation.
 */

#pragma once

#include "COM_defines.h"

#include <ostream>

namespace blender::compositor {

/**
 * \brief Possible quality settings
 * \see CompositorContext.quality
 * \ingroup Execution
 */
enum class eCompositorQuality {
  /** \brief High quality setting */
  High = 0,
  /** \brief Medium quality setting */
  Medium = 1,
  /** \brief Low quality setting */
  Low = 2,
};

/**
 * \brief Possible priority settings
 * \ingroup Execution
 */
enum class eCompositorPriority {
  /** \brief High quality setting */
  High = 2,
  /** \brief Medium quality setting */
  Medium = 1,
  /** \brief Low quality setting */
  Low = 0,
  /**
   * No priority set. Is used to mark WorkPackages that aren't needed to be scheduled (yet).
   *
   * When executing the compositor marks all dependend work packages with a High priority. Work
   * packages with the high priority are scheduled. After completed medium priority work packages
   * are determined and scheduled.
   *
   * The Unset marks work packages that don't have been selected for the current priority that
   * is being executed. They might be selected when executing the other priority or aren't needed
   * at all. The later can happen when the tile is moved completely out the visible viewport.
   */
  Unset = -1,
};

/**
 * \brief the execution state of a chunk in an ExecutionGroup
 * \ingroup Execution
 */
enum class eWorkPackageState : int32_t {
  /**
   * \brief chunk is not yet scheduled
   */
  NotScheduled = 0,
  /**
   * \brief chunk is scheduled, but not yet executed
   */
  Scheduled = 1,
  /**
   * \brief chunk is executed.
   */
  Executed = 2,
};

std::ostream &operator<<(std::ostream &os, const eCompositorPriority &priority);
std::ostream &operator<<(std::ostream &os, const eWorkPackageState &execution_state);

}  // namespace blender::compositor
