#pragma once

#include <stdexcept>
#include <string>

#include <cuda_runtime_api.h>

inline void check_cuda_errors(cudaError_t result, const char* call, const char* file, int line)
{
    if (result != cudaSuccess) {
        const std::string location = std::string(file) + ":" + std::to_string(line);
        throw std::runtime_error(location + ": " + call + ": " + cudaGetErrorString(result));
    }
}

#define CUDA_CHECK(call) check_cuda_errors((call), #call, __FILE__, __LINE__)
