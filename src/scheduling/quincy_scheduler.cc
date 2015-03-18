// The Firmament project
// Copyright (c) 2013-2014 Malte Schwarzkopf <malte.schwarzkopf@cl.cam.ac.uk>
// Copyright (c) 2013 Ionel Gog <ionel.gog@cl.cam.ac.uk>
//
// Implementation of a Quincy-style min-cost flow scheduler.

#include "scheduling/quincy_scheduler.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/common.h"
#include "base/types.h"
#include "storage/reference_types.h"
#include "misc/map-util.h"
#include "misc/utils.h"
#include "misc/string_utils.h"
#include "engine/local_executor.h"
#include "engine/remote_executor.h"
#include "storage/object_store_interface.h"
#include "scheduling/cost_models.h"
#include "scheduling/flow_scheduling_cost_model_interface.h"
#include "scheduling/knowledge_base.h"

DEFINE_int32(flow_scheduling_cost_model, 0,
             "Flow scheduler cost model to use. "
             "Values: 0 = TRIVIAL, 1 = RANDOM, 2 = SJF, 3 = QUINCY, "
             "4 = WHARE, 5 = COCO");

namespace firmament {
namespace scheduler {

using executor::LocalExecutor;
using executor::RemoteExecutor;
using common::pb_to_set;
using store::ObjectStoreInterface;

QuincyScheduler::QuincyScheduler(
    shared_ptr<JobMap_t> job_map,
    shared_ptr<ResourceMap_t> resource_map,
    const ResourceTopologyNodeDescriptor& resource_topology,
    shared_ptr<ObjectStoreInterface> object_store,
    shared_ptr<TaskMap_t> task_map,
    KnowledgeBase* kb,
    shared_ptr<TopologyManager> topo_mgr,
    MessagingAdapterInterface<BaseMessage>* m_adapter,
    ResourceID_t coordinator_res_id,
    const string& coordinator_uri,
    const SchedulingParameters& params)
    : EventDrivenScheduler(job_map, resource_map, resource_topology,
                           object_store, task_map, topo_mgr, m_adapter,
                           coordinator_res_id, coordinator_uri),
      topology_manager_(topo_mgr),
      knowledge_base_(kb),
      parameters_(params) {
  // Select the cost model to use
  VLOG(1) << "Set cost model to use in flow graph to \""
          << FLAGS_flow_scheduling_cost_model << "\"";
  switch (FLAGS_flow_scheduling_cost_model) {
    case FlowSchedulingCostModelType::COST_MODEL_TRIVIAL:
      flow_graph_.reset(new FlowGraph(new TrivialCostModel()));
      VLOG(1) << "Using the trivial cost model";
      break;
    case FlowSchedulingCostModelType::COST_MODEL_RANDOM:
      flow_graph_.reset(new FlowGraph(new RandomCostModel()));
      VLOG(1) << "Using the random cost model";
      break;
    case FlowSchedulingCostModelType::COST_MODEL_SJF:
      flow_graph_.reset(new FlowGraph(
          new SJFCostModel(task_map, knowledge_base_)));
      VLOG(1) << "Using the SJF cost model";
      break;
    case FlowSchedulingCostModelType::COST_MODEL_QUINCY:
      flow_graph_.reset(
          new FlowGraph(new QuincyCostModel(resource_map, job_map, task_map,
                                            &task_bindings_)));
      VLOG(1) << "Using the Quincy cost model";
      break;
    default:
      LOG(FATAL) << "Unknown flow scheduling cost model specificed "
                 << "(" << FLAGS_flow_scheduling_cost_model << ")";
  }

  LOG(INFO) << "QuincyScheduler initiated; parameters: "
            << parameters_.ShortDebugString();
  // Set up the initial flow graph
  UpdateResourceTopology(resource_topology);
  // Set up the dispatcher, which starts the flow solver
  quincy_dispatcher_ = new QuincyDispatcher(flow_graph_, false);
}

QuincyScheduler::~QuincyScheduler() {
  delete quincy_dispatcher_;
  // XXX(ionel): stub
}

const ResourceID_t* QuincyScheduler::FindResourceForTask(
    TaskDescriptor*) {
  // XXX(ionel): stub
  return NULL;
}

uint64_t QuincyScheduler::ApplySchedulingDeltas(
    const vector<SchedulingDelta*>& deltas) {
  uint64_t num_scheduled = 0;
  // Perform the necessary actions to apply the scheduling changes passed to the
  // method
  VLOG(1) << "Applying " << deltas.size() << " scheduling deltas...";
  for (vector<SchedulingDelta*>::const_iterator it = deltas.begin();
       it != deltas.end();
       ++it) {
    VLOG(1) << "Processing delta of type " << (*it)->type();
    TaskID_t task_id = (*it)->task_id();
    ResourceID_t res_id = ResourceIDFromString((*it)->resource_id());
    if ((*it)->type() == SchedulingDelta::PLACE) {
      VLOG(1) << "Trying to place task " << task_id
              << " on resource " << (*it)->resource_id();
      TaskDescriptor* td = FindPtrOrNull(*task_map_, task_id);
      ResourceStatus* rs = FindPtrOrNull(*resource_map_, res_id);
      CHECK_NOTNULL(td);
      CHECK_NOTNULL(rs);
      VLOG(1) << "About to bind task " << td->uid() << " to resource "
              << rs->mutable_descriptor()->uuid();
      BindTaskToResource(td, rs->mutable_descriptor());
      // After the task is bound, we now remove all of its edges into the flow
      // graph apart from the bound resource.
      // N.B.: This disables preemption and migration!
      flow_graph_->UpdateArcsForBoundTask(task_id, res_id);
      // Tag the job to which this task belongs as running
      JobDescriptor* jd = FindOrNull(*job_map_, JobIDFromString(td->job_id()));
      if (jd->state() != JobDescriptor::RUNNING)
        jd->set_state(JobDescriptor::RUNNING);
      num_scheduled++;
      (*it)->set_actioned(true);
    }
  }
  return num_scheduled;
}

void QuincyScheduler::HandleJobCompletion(JobID_t job_id) {
  // Call into superclass handler
  EventDrivenScheduler::HandleJobCompletion(job_id);
  {
    boost::lock_guard<boost::mutex> lock(scheduling_lock_);
    // Job completed, so remove its nodes
    flow_graph_->DeleteNodesForJob(job_id);
  }
}

void QuincyScheduler::HandleTaskCompletion(TaskDescriptor* td_ptr,
                                           TaskFinalReport* report) {
  // Call into superclass handler
  EventDrivenScheduler::HandleTaskCompletion(td_ptr, report);
  {
    boost::lock_guard<boost::mutex> lock(scheduling_lock_);
    flow_graph_->DeleteTaskNode(td_ptr->uid());
  }
}

uint64_t QuincyScheduler::ScheduleJob(JobDescriptor* job_desc) {
  boost::lock_guard<boost::mutex> lock(scheduling_lock_);
  LOG(INFO) << "START SCHEDULING " << job_desc->uuid();
  // Check if we have any runnable tasks in this job
  const set<TaskID_t> runnable_tasks = RunnableTasksForJob(job_desc);
  if (runnable_tasks.size() > 0) {
    // Check if the job is already in the flow graph
    // If not, simply add the whole job
    flow_graph_->AddOrUpdateJobNodes(job_desc);
    // If it is, only add the new bits
    // Run a scheduler iteration
    uint64_t newly_scheduled = RunSchedulingIteration();
    LOG(INFO) << "STOP SCHEDULING " << job_desc->uuid();
    return newly_scheduled;
  } else {
    LOG(INFO) << "STOP SCHEDULING " << job_desc->uuid();
    return 0;
  }
}

void QuincyScheduler::RegisterResource(ResourceID_t res_id, bool local) {
  // Update the flow graph
  UpdateResourceTopology(resource_topology_);
  // Call into superclass method to do scheduler resource initialisation.
  // This will create the executor for the new resource.
  EventDrivenScheduler::RegisterResource(res_id, local);
}

uint64_t QuincyScheduler::RunSchedulingIteration() {
  multimap<uint64_t, uint64_t>* task_mappings = quincy_dispatcher_->Run();
  // Solver's done, let's post-process the results.
  multimap<uint64_t, uint64_t>::iterator it;
  vector<SchedulingDelta*> deltas;
  for (it = task_mappings->begin(); it != task_mappings->end(); it++) {
    VLOG(1) << "Bind " << it->first << " to " << it->second << endl;
    SchedulingDelta* delta = new SchedulingDelta;
    quincy_dispatcher_->NodeBindingToSchedulingDelta(
        *flow_graph_->Node(it->first), *flow_graph_->Node(it->second),
        &task_bindings_, delta);
    if (delta->type() == SchedulingDelta::NOOP)
      continue;
    // Mark the task as scheduled
    flow_graph_->Node(it->first)->type_.set_type(FlowNodeType::SCHEDULED_TASK);
    // Remember the delta
    deltas.push_back(delta);
  }
  uint64_t num_scheduled = ApplySchedulingDeltas(deltas);
  // Drop all deltas that were actioned
  for (vector<SchedulingDelta*>::iterator it = deltas.begin();
       it != deltas.end(); ) {
    if ((*it)->actioned())
      it = deltas.erase(it);
    else
      it++;
  }
  if (deltas.size() > 0)
    LOG(WARNING) << "Not all deltas were processed, " << deltas.size()
                 << " remain!";
  return num_scheduled;
}

void QuincyScheduler::PrintGraph(vector< map<uint64_t, uint64_t> > adj_map) {
  for (vector< map<uint64_t, uint64_t> >::size_type i = 1;
       i < adj_map.size(); ++i) {
    map<uint64_t, uint64_t>::iterator it;
    for (it = adj_map[i].begin();
         it != adj_map[i].end(); it++) {
      cout << i << " " << it->first << " " << it->second << endl;
    }
  }
}

void QuincyScheduler::UpdateResourceTopology(
    const ResourceTopologyNodeDescriptor& root) {
  // Run a topology refresh (somewhat expensive!); if only two nodes exist, the
  // flow graph is empty apart from cluster aggregator and sink.
  VLOG(1) << "Num nodes in flow graph is: " << flow_graph_->NumNodes();
  if (flow_graph_->NumNodes() == 1)
    flow_graph_->AddResourceTopology(root);
  else
    flow_graph_->UpdateResourceTopology(root);
}

}  // namespace scheduler
}  // namespace firmament
