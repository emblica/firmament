// The Firmament project
// Copyright (c) 2014 Malte Schwarzkopf <malte.schwarzkopf@cl.cam.ac.uk>
//
// Google cluster trace simulator tool.

#ifndef FIRMAMENT_SIM_TRACE_EXTRACT_GOOGLE_TRACE_SIMULATOR_H
#define FIRMAMENT_SIM_TRACE_EXTRACT_GOOGLE_TRACE_SIMULATOR_H

#include <fstream>

#include "base/common.h"
#include "base/resource_topology_node_desc.pb.h"
#include "misc/utils.h"
#include "misc/map-util.h"
#include "scheduling/flow_graph.h"
#include "scheduling/quincy_cost_model.h"

namespace firmament {
namespace sim {

struct TaskIdentifier {
  uint64_t job_id;
  uint64_t task_index;

  bool operator==(const TaskIdentifier& other) const {
    return job_id == other.job_id && task_index == other.task_index;
  }
};

struct TaskIdentifierHasher {
  size_t operator()(const TaskIdentifier& key) const {
    return hash<uint64_t>()(key.job_id) * 17 + hash<uint64_t>()(key.task_index);
  }
};

struct MachineEvent {
  uint64_t machine_id;
  int32_t event_type;
};

class GoogleTraceSimulator {
 public:
  explicit GoogleTraceSimulator(string& trace_path);
  void Run();

 private:
  void AddMachineToTopologyAndResetUuid(const ResourceTopologyNodeDescriptor& machine_tmpl,
                                        uint64_t machine_id,
                                        ResourceTopologyNodeDescriptor* rtn_root);
  void AddNewTask(FlowGraph* flow_graph, QuincyCostModel* cost_model, uint64_t job_id,
                  uint64_t task_id, ofstream& out_file);

  // Compute the number of events of a particular type withing each time interval.
  void BinTasksByEventType(uint64_t event_type, ofstream& out_file);

  unordered_map<uint64_t, JobDescriptor*>& LoadInitialJobs(int64_t max_jobs);
  ResourceTopologyNodeDescriptor& LoadInitialMachines(int64_t max_num);
  void LoadInitalTasks(const unordered_map<uint64_t, JobDescriptor*>& initial_jobs);
  // Loads all the machine events and returns a multimap timestamp -> event.
  multimap<uint64_t, MachineEvent>& LoadMachineEvents();
  // Loads all the task runtimes and returns map task_identifier -> runtime.
  unordered_map<TaskIdentifier, uint64_t, TaskIdentifierHasher>& LoadTasksRunningTime();

  // Read the jobs that are running or waiting to be scheduled at the beginning of the simulation.
  // Read num_jobs if it's specified.
  uint64_t ReadJobsFile(vector<uint64_t>* jobs, int64_t num_jobs);
  // Read the machines that are alive at the beginning of the simulation.
  // Read num_machines if it's specified.
  uint64_t ReadMachinesFile(vector<uint64_t>* machines, int64_t num_machines);
  // Read the tasks events from the beginning of the trace that are related to the jobs passed
  // in as an argument.
  uint64_t ReadInitialTasksFile(const unordered_map<uint64_t, JobDescriptor*>& jobs);

  void PopulateJob(JobDescriptor* jd, uint64_t job_id);
  // The resource topology is build from the same protobuf file. The function changes the
  // uuids to make sure that there's no two identical uuids.
  void ResetUuid(ResourceTopologyNodeDescriptor* rtnd, const string& hostname,
                 const string& root_uuid);

  void ReplayTrace(FlowGraph* flow_graph, QuincyCostModel* cost_model, const string& file_base);

  // Map used to convert between the new uuids assigned to the machine nodes and
  // the old uuids read from the machine topology file.
  unordered_map<string, string> uuid_conversion_map_;
  // Map used to convert between the google trace job_ids and the boost-based Firmament uuids.
  unordered_map<uint64_t, JobID_t> job_id_conversion_map_;
  string trace_path_;
};

}  // namespace sim
}  // namespace firmament

#endif  // FIRMAMENT_SIM_TRACE_EXTRACT_GOOGLE_TRACE_SIMULATOR_H
