#pragma once

#include <stdexcept>
#include <string>

#include <QtGui/qopengl.h>

#include <cuda_gl_interop.h>
#include <cuda_runtime_api.h>

inline void check_cuda_errors(cudaError_t result, const char* call, const char* file, int line)
{
    if (result != cudaSuccess) {
        const std::string location = std::string(file) + ":" + std::to_string(line);
        throw std::runtime_error(location + ": " + call + ": " + cudaGetErrorString(result));
    }
}

#define CUDA_CHECK(call) check_cuda_errors((call), #call, __FILE__, __LINE__)

inline void initialize_cuda()
{
    unsigned int cuda_device_count = 0;
    int cuda_device = -1;
    if (cudaGLGetDevices(&cuda_device_count, &cuda_device, 1, cudaGLDeviceListAll) != cudaSuccess ||
        cuda_device_count == 0 ||
        cudaSetDevice(cuda_device) != cudaSuccess) {
        throw std::runtime_error("CUDA device initialization failed.");
    }
}
