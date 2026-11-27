#pragma once
#include <mpi.h>
#include <filesystem>

void run_cluster_didactic(MPI_Comm comm, const std::filesystem::path& input_path);
