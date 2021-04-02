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

#include "COM_ExecutionSystem.h"

#include "BLI_utildefines.h"
#include "PIL_time.h"

#include "BKE_node.h"

#include "BLT_translation.h"

#include "COM_Converter.h"
#include "COM_Debug.h"
#include "COM_ExecutionGroup.h"
#include "COM_NodeOperation.h"
#include "COM_NodeOperationBuilder.h"
#include "COM_ReadBufferOperation.h"
#include "COM_WorkScheduler.h"

#ifdef WITH_CXX_GUARDEDALLOC
#  include "MEM_guardedalloc.h"
#endif

namespace blender::compositor {

ExecutionSystem::ExecutionSystem(RenderData *rd,
                                 Scene *scene,
                                 bNodeTree *editingtree,
                                 bool rendering,
                                 bool fastcalculation,
                                 const ColorManagedViewSettings *viewSettings,
                                 const ColorManagedDisplaySettings *displaySettings,
                                 const char *viewName)
{
  this->m_context.setViewName(viewName);
  this->m_context.setScene(scene);
  this->m_context.setbNodeTree(editingtree);
  this->m_context.setPreviewHash(editingtree->previews);
  this->m_context.setFastCalculation(fastcalculation);
  /* initialize the CompositorContext */
  if (rendering) {
    this->m_context.setQuality((eCompositorQuality)editingtree->render_quality);
  }
  else {
    this->m_context.setQuality((eCompositorQuality)editingtree->edit_quality);
  }
  this->m_context.setRendering(rendering);
  this->m_context.setHasActiveOpenCLDevices(WorkScheduler::has_gpu_devices() &&
                                            (editingtree->flag & NTREE_COM_OPENCL));

  this->m_context.setRenderData(rd);
  this->m_context.setViewSettings(viewSettings);
  this->m_context.setDisplaySettings(displaySettings);

  {
    NodeOperationBuilder builder(&m_context, editingtree);
    builder.convertToOperations(this);
  }

  unsigned int resolution[2];

  rctf *viewer_border = &editingtree->viewer_border;
  bool use_viewer_border = (editingtree->flag & NTREE_VIEWER_BORDER) &&
                           viewer_border->xmin < viewer_border->xmax &&
                           viewer_border->ymin < viewer_border->ymax;

  editingtree->stats_draw(editingtree->sdh, TIP_("Compositing | Determining resolution"));

  for (ExecutionGroup *executionGroup : m_groups) {
    resolution[0] = 0;
    resolution[1] = 0;
    executionGroup->determineResolution(resolution);

    if (rendering) {
      /* case when cropping to render border happens is handled in
       * compositor output and render layer nodes
       */
      if ((rd->mode & R_BORDER) && !(rd->mode & R_CROP)) {
        executionGroup->setRenderBorder(
            rd->border.xmin, rd->border.xmax, rd->border.ymin, rd->border.ymax);
      }
    }

    if (use_viewer_border) {
      executionGroup->setViewerBorder(
          viewer_border->xmin, viewer_border->xmax, viewer_border->ymin, viewer_border->ymax);
    }
  }

  //  DebugInfo::graphviz(this);
}

ExecutionSystem::~ExecutionSystem()
{
  for (NodeOperation *operation : m_operations) {
    delete operation;
  }
  this->m_operations.clear();

  for (ExecutionGroup *group : m_groups) {
    delete group;
  }
  this->m_groups.clear();
}

void ExecutionSystem::set_operations(const Vector<NodeOperation *> &operations,
                                     const Vector<ExecutionGroup *> &groups)
{
  m_operations = operations;
  m_groups = groups;
}

static void update_read_buffer_offset(Vector<NodeOperation *> &operations)
{
  unsigned int order = 0;
  for (NodeOperation *operation : operations) {
    if (operation->get_flags().is_read_buffer_operation) {
      ReadBufferOperation *readOperation = (ReadBufferOperation *)operation;
      readOperation->setOffset(order);
      order++;
    }
  }
}

static void init_write_operations_for_execution(Vector<NodeOperation *> &operations,
                                                const bNodeTree *bTree)
{
  for (NodeOperation *operation : operations) {
    if (operation->get_flags().is_write_buffer_operation) {
      operation->setbNodeTree(bTree);
      operation->initExecution();
    }
  }
}

static void link_write_buffers(Vector<NodeOperation *> &operations)
{
  for (NodeOperation *operation : operations) {
    if (operation->get_flags().is_read_buffer_operation) {
      ReadBufferOperation *readOperation = static_cast<ReadBufferOperation *>(operation);
      readOperation->updateMemoryBuffer();
    }
  }
}

static void init_non_write_operations_for_execution(Vector<NodeOperation *> &operations,
                                                    const bNodeTree *bTree)
{
  for (NodeOperation *operation : operations) {
    if (!operation->get_flags().is_write_buffer_operation) {
      operation->setbNodeTree(bTree);
      operation->initExecution();
    }
  }
}

static void init_execution_groups_for_execution(Vector<ExecutionGroup *> &groups,
                                                const int chunk_size)
{
  for (ExecutionGroup *execution_group : groups) {
    execution_group->setChunksize(chunk_size);
    execution_group->initExecution();
  }
}

/**
 * Link all work packages with the work packages they depend on.
 */
static void link_work_packages(blender::Vector<ExecutionGroup *> &groups)
{
  for (ExecutionGroup *group : groups) {
    for (WorkPackage &work_package : group->get_work_packages()) {
      for (ReadBufferOperation *read_operation : group->get_read_buffer_operations()) {
        rcti area = {0};
        group->getOutputOperation()->determineDependingAreaOfInterest(
            &work_package.rect, read_operation, &area);
        ExecutionGroup *parent = read_operation->getMemoryProxy()->getExecutor();
        parent->link_child_work_packages(&work_package, &area);
      }
    }
  }
}

static void schedule_root_work_packages(blender::Vector<ExecutionGroup *> &groups)
{
  for (ExecutionGroup *group : groups) {
    for (WorkPackage &work_package : group->get_work_packages()) {
      if (work_package.state != eWorkPackageState::NotScheduled) {
        continue;
      }
      if (work_package.priority == eCompositorPriority::Unset) {
        continue;
      }
      if (work_package.num_parents == 0) {
        WorkScheduler::schedule(&work_package);
      }
    }
  }
}

static void mark_priority(WorkPackage &work_package, eCompositorPriority priority)
{
  if (work_package.state != eWorkPackageState::NotScheduled) {
    return;
  }
  if (work_package.priority != eCompositorPriority::Unset) {
    return;
  }
  work_package.priority = priority;
  for (WorkPackage *parent : work_package.parents) {
    mark_priority(*parent, priority);
  }
}

static void mark_priority(blender::Vector<WorkPackage> &work_packages,
                          eCompositorPriority priority)
{
  for (WorkPackage &work_package : work_packages) {
    mark_priority(work_package, priority);
  }
}

void ExecutionSystem::execute()
{
  const bNodeTree *editingtree = this->m_context.getbNodeTree();
  editingtree->stats_draw(editingtree->sdh, TIP_("Compositing | Initializing execution"));

  DebugInfo::execute_started(this);

  update_read_buffer_offset(m_operations);
  init_write_operations_for_execution(m_operations, m_context.getbNodeTree());
  link_write_buffers(m_operations);
  init_non_write_operations_for_execution(m_operations, m_context.getbNodeTree());
  init_execution_groups_for_execution(m_groups, m_context.getChunksize());
  link_work_packages(m_groups);

  WorkScheduler::start(this->m_context);
  editingtree->stats_draw(editingtree->sdh, TIP_("Compositing | Started"));
  execute_groups(eCompositorPriority::High);
  if (!this->getContext().isFastCalculation()) {
    execute_groups(eCompositorPriority::Medium);
    execute_groups(eCompositorPriority::Low);
  }
  WorkScheduler::finish();
  WorkScheduler::stop();

  editingtree->stats_draw(editingtree->sdh, TIP_("Compositing | De-initializing execution"));

  for (NodeOperation *operation : m_operations) {
    operation->deinitExecution();
  }

  for (ExecutionGroup *execution_group : m_groups) {
    execution_group->deinitExecution();
  }
}

static bool is_completed(Vector<ExecutionGroup *> &groups)
{
  for (ExecutionGroup *group : groups) {
    for (WorkPackage &work_package : group->get_work_packages()) {
      if (work_package.state != eWorkPackageState::Executed) {
        return false;
      }
    }
  }
  return true;
}

static void wait_for_completion(Vector<ExecutionGroup *> &groups)
{
  /* Todo: check for break! */
  while (!is_completed(groups)) {
    PIL_sleep_ms(100);
  }
}

void ExecutionSystem::execute_groups(eCompositorPriority priority)
{
  switch (COM_SCHEDULING_MODE) {
    case eSchedulingMode::InputToOutput: {
      const bNodeTree *bnodetree = this->m_context.getbNodeTree();
      Vector<ExecutionGroup *> groups;
      for (ExecutionGroup *execution_group : m_groups) {
        if (execution_group->get_flags().is_output &&
            execution_group->getRenderPriority() == priority) {
          execution_group->set_btree(bnodetree);
          mark_priority(execution_group->get_work_packages(), priority);
          groups.append(execution_group);
        }
      }
      schedule_root_work_packages(m_groups);
      wait_for_completion(groups);
      break;
    }
    case eSchedulingMode::OutputToInput: {
      for (ExecutionGroup *execution_group : m_groups) {
        if (execution_group->get_flags().is_output &&
            execution_group->getRenderPriority() == priority) {
          execution_group->execute(this);
        }
      }
      break;
    }
  }
}

}  // namespace blender::compositor
