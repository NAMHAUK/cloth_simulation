#pragma once

#include "utils/CudaUtils.h"

#include <array>
#include <cstddef>

#if defined(_WIN32)
#include <QtCore/qt_windows.h>
#endif

#include <cuda_gl_interop.h>
#include <vector_types.h>

struct TriangleVertexIndices;

struct MappedMeshBuffers
{
    float4* positions;
    float4* normals;
    const TriangleVertexIndices* triangle_vertex_indices;
};

struct CudaMeshResources final
{
    CudaMeshResources() = default;
    CudaMeshResources(const CudaMeshResources&) = delete;
    CudaMeshResources& operator=(const CudaMeshResources&) = delete;

    void register_buffers(unsigned int position_buffer,
                          unsigned int normal_buffer,
                          unsigned int triangle_vertex_indices_buffer)
    {
        CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&resources_[0], position_buffer, cudaGraphicsRegisterFlagsNone));
        CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&resources_[1], normal_buffer, cudaGraphicsRegisterFlagsNone));
        CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&resources_[2],
                                                triangle_vertex_indices_buffer,
                                                cudaGraphicsRegisterFlagsReadOnly));
    }

    MappedMeshBuffers map()
    {
        // 공유 buffer CUDA 접근 활성화
        CUDA_CHECK(cudaGraphicsMapResources(resources_.size(), resources_.data()));

        // 공유 buffer pointer 가져오기
        void* position_buffer = nullptr;
        void* normal_buffer = nullptr;
        void* triangle_buffer = nullptr;
        std::size_t buffer_bytes = 0;
        CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&position_buffer, &buffer_bytes, resources_[0]));
        CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&normal_buffer, &buffer_bytes, resources_[1]));
        CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&triangle_buffer, &buffer_bytes, resources_[2]));

        return {static_cast<float4*>(position_buffer),
                static_cast<float4*>(normal_buffer),
                static_cast<const TriangleVertexIndices*>(triangle_buffer)};
    }

    void unmap() { CUDA_CHECK(cudaGraphicsUnmapResources(resources_.size(), resources_.data())); }

    void release()
    {
        for (cudaGraphicsResource*& resource : resources_) {
            if (resource != nullptr) {
                CUDA_CHECK(cudaGraphicsUnregisterResource(resource));
                resource = nullptr;
            }
        }
    }

private:
    std::array<cudaGraphicsResource*, 3> resources_{};
};
