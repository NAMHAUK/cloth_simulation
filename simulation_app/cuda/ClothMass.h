#pragma once

#include <cstdint>

#include <cuda_runtime_api.h>

cudaError_t set_vertex_masses(float* masses, std::uint32_t vertex_count, float mass);
