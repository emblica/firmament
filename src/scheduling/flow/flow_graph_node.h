// The Firmament project
// Copyright (c) 2013 Malte Schwarzkopf <malte.schwarzkopf@cl.cam.ac.uk>
//
// Export utility that converts a given resource topology and set of job's into
// a DIMACS file for use with the Quincy CS2 solver.

#ifndef FIRMAMENT_SCHEDULING_FLOW_FLOW_GRAPH_NODE_H
#define FIRMAMENT_SCHEDULING_FLOW_FLOW_GRAPH_NODE_H

#include <string>
#include <boost/uuid/nil_generator.hpp>

#include "base/common.h"
#include "base/types.h"
#include "base/resource_desc.pb.h"
#include "base/task_desc.pb.h"
#include "scheduling/flow/flow_graph_arc.h"

namespace firmament {

enum FlowNodeType {
  ROOT_TASK = 0,
  SCHEDULED_TASK = 1,
  UNSCHEDULED_TASK = 2,
  JOB_AGGREGATOR = 3,
  SINK = 4,
  EQUIVALENCE_CLASS = 5,
  COORDINATOR = 6,
  MACHINE = 7,
  NUMA_NODE = 8,
  SOCKET = 9,
  CACHE = 10,
  CORE = 11,
  PU = 12,
};

struct FlowGraphNode {
  explicit FlowGraphNode(uint64_t id);
  FlowGraphNode(uint64_t id, int64_t excess);
  void AddArc(FlowGraphArc* arc);
  bool IsEquivalenceClassNode() const {
    return type_ == FlowNodeType::EQUIVALENCE_CLASS;
  };
  bool IsResourceNode() const {
    return type_ == FlowNodeType::COORDINATOR ||
      type_ == FlowNodeType::MACHINE ||
      type_ == FlowNodeType::NUMA_NODE ||
      type_ == FlowNodeType::SOCKET ||
      type_ == FlowNodeType::CACHE ||
      type_ == FlowNodeType::CORE ||
      type_ == FlowNodeType::PU;
  };
  bool IsTaskNode() const {
    return type_ == FlowNodeType::ROOT_TASK ||
      type_ == FlowNodeType::SCHEDULED_TASK ||
      type_ == FlowNodeType::UNSCHEDULED_TASK;
  };
  bool IsTaskAssignedOrRunning() const {
    CHECK_NOTNULL(td_ptr_);
    return td_ptr_->state() == TaskDescriptor::ASSIGNED ||
      td_ptr_->state() == TaskDescriptor::RUNNING;
  }
  static FlowNodeType TransformToResourceNodeType(const ResourceDescriptor& rd);

  uint64_t id_;
  int64_t excess_;
  FlowNodeType type_;
  // TODO(malte): Not sure if these should be here, but they've got to go
  // somewhere.
  // The ID of the job that this task belongs to (if task node).
  JobID_t job_id_;
  // The ID of the resource that this node represents.
  ResourceID_t resource_id_;
  // The descriptor of the resource that this node represents.
  ResourceDescriptor* rd_ptr_;
  // The descriptor of the task represented by this node.
  TaskDescriptor* td_ptr_;
  // the ID of the equivalence class represented by this node.
  EquivClass_t ec_id_;
  // Free-form comment for debugging purposes (used to label special nodes)
  string comment_;
  // Outgoing arcs from this node, keyed by destination node
  unordered_map<uint64_t, FlowGraphArc*> outgoing_arc_map_;
  // Incoming arcs to this node, keyed by source node
  unordered_map<uint64_t, FlowGraphArc*> incoming_arc_map_;
  // Field use to mark if the node has been visited in a graph traversal.
  uint32_t visited_;
};

}  // namespace firmament

#endif  // FIRMAMENT_SCHEDULING_FLOW_FLOW_GRAPH_NODE_H
