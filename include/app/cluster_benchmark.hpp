#pragma once

#include "app/cluster_session.hpp"
#include "app/experiment_execution.hpp"

namespace conway::app {

StoredExperiment runClusterBenchmark(ClusterSession& group, const ExecutionRequest& request,
    const ExperimentExecution::PhasePublisher& phase, const std::function<bool()>& cancelled);

} // namespace conway::app
