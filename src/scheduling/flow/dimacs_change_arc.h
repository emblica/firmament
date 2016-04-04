// The Firmament project
// Copyright (c) 2015 Ionel Gog <ionel.gog@cl.cam.ac.uk>

#ifndef FIRMAMENT_SCHEDULING_FLOW_DIMACS_CHANGE_ARC_H
#define FIRMAMENT_SCHEDULING_FLOW_DIMACS_CHANGE_ARC_H

#include <string>

#include "base/types.h"
#include "scheduling/flow/dimacs_change.h"
#include "scheduling/flow/dimacs_change_stats.h"
#include "scheduling/flow/flow_graph_arc.h"

namespace firmament {

class DIMACSChangeArc : public DIMACSChange {
 public:
  explicit DIMACSChangeArc(const FlowGraphArc& arc, int64_t old_cost);
  const string GenerateChange() const;

  uint64_t src_;
  uint64_t dst_;
  uint64_t cap_lower_bound_;
  uint64_t cap_upper_bound_;
  int64_t cost_;
  FlowGraphArcType type_;
  int64_t old_cost_;

  friend DIMACSChangeStats;
};

} // namespace firmament

#endif // FIRMAMENT_SCHEDULING_FLOW_DIMACS_CHANGE_ARC_H
