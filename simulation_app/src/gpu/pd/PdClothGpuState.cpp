#include "gpu/pd/PdClothGpuState.h"
#include "ClothMass.h"
#include "MeshNormals.h"
#include "gpu/common/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "simulation/SimulationParams.h"
#include "utils/CudaUtils.h"

#include <cstddef>
#include <stdexcept>

#include <cuda_gl_interop.h>
#include <cuda_runtime.h>

void PdClothGpuState::rebuild_buffers(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
{
    prepare_normal_buffers(garments, cloth_state);

    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&previous_position_buffer_), sizeof(float4) * vertex_count_));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&predicted_position_buffer_), sizeof(float4) * vertex_count_));

    const std::size_t velocity_bytes = sizeof(float4) * vertex_count_;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&velocity_buffer_), velocity_bytes));
    CUDA_CHECK(cudaMemset(velocity_buffer_, 0, velocity_bytes));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&mass_buffer_), sizeof(float) * vertex_count_));
}

void PdClothGpuState::prepare_normal_buffers(const std::vector<GarmentObject>& garments,
                                             const ClothGpuState& cloth_state)
{
    if (cloth_state.element_counts().vertex == 0) {
        return;
    }
    if (cloth_state.element_counts().vertex > simulation_limits::max_vertex_count) {
        throw std::length_error("Cloth vertex count exceeds the simulation limit.");
    }

    vertex_count_ = cloth_state.element_counts().vertex;
    triangle_count_ = cloth_state.element_counts().triangle;

    for (const GarmentObject& garment : garments) {
        triangle_color_counts_.insert(triangle_color_counts_.end(),
                                      garment.mesh.triangle_color_counts.begin(),
                                      garment.mesh.triangle_color_counts.end());
    }

    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&position_resource_,
                                            cloth_state.buffer_set().current_position,
                                            cudaGraphicsRegisterFlagsNone));
    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&normal_resource_,
                                            cloth_state.buffer_set().vertex_normal,
                                            cudaGraphicsRegisterFlagsNone));
    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&triangle_resource_,
                                            cloth_state.buffer_set().triangle_vertex_indices,
                                            cudaGraphicsRegisterFlagsReadOnly));

    float4* positions = nullptr;
    float4* normals = nullptr;
    map_shared_buffers(positions, normals);
    update_normals(positions, normals, triangle_vertex_indices_buffer_, vertex_count_, triangle_color_counts_);
    unmap_shared_buffers();
}

void PdClothGpuState::initialize_masses(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
{
    for (const GarmentObject& garment : garments) {
        const GarmentBufferState& garment_state = cloth_state.garment_states()[garment.layer];

        set_vertex_masses(mass_buffer_ + garment_state.vertex_start_index,
                          garment_state.vertex_count,
                          garment.surface_density);
    }
}

void PdClothGpuState::reset_velocities() const
{
    CUDA_CHECK(cudaMemset(velocity_buffer_, 0, sizeof(float4) * vertex_count_));
}

// Shared Buffer Mapping

void PdClothGpuState::map_shared_buffers(float4*& positions, float4*& normals)
{
    // 공유 buffer CUDA 접근 활성화
    cudaGraphicsResource* resources[]{position_resource_, normal_resource_, triangle_resource_};
    CUDA_CHECK(cudaGraphicsMapResources(3, resources));

    // 공유 buffer pointer 가져오기
    void* position_buffer = nullptr;
    void* normal_buffer = nullptr;
    void* triangle_buffer = nullptr;
    std::size_t buffer_bytes = 0;
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&position_buffer, &buffer_bytes, position_resource_));
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&normal_buffer, &buffer_bytes, normal_resource_));
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&triangle_buffer, &buffer_bytes, triangle_resource_));

    positions = static_cast<float4*>(position_buffer);
    normals = static_cast<float4*>(normal_buffer);
    triangle_vertex_indices_buffer_ = static_cast<const TriangleVertexIndices*>(triangle_buffer);
}

void PdClothGpuState::unmap_shared_buffers()
{
    cudaGraphicsResource* resources[]{position_resource_, normal_resource_, triangle_resource_};
    CUDA_CHECK(cudaGraphicsUnmapResources(3, resources));
    triangle_vertex_indices_buffer_ = nullptr;
}

// Accessors

float4* PdClothGpuState::previous_position_buffer() const
{
    return previous_position_buffer_;
}

float4* PdClothGpuState::predicted_position_buffer() const
{
    return predicted_position_buffer_;
}

float4* PdClothGpuState::velocity_buffer() const
{
    return velocity_buffer_;
}

const float* PdClothGpuState::mass_buffer() const
{
    return mass_buffer_;
}

const TriangleVertexIndices* PdClothGpuState::triangle_vertex_indices_buffer() const
{
    return triangle_vertex_indices_buffer_;
}

const std::vector<std::uint32_t>& PdClothGpuState::triangle_color_counts() const
{
    return triangle_color_counts_;
}

std::uint32_t PdClothGpuState::vertex_count() const
{
    return vertex_count_;
}

std::uint32_t PdClothGpuState::triangle_count() const
{
    return triangle_count_;
}

void PdClothGpuState::release()
{
    // OpenGL과 공유하는 버퍼 사용 등록 해제
    if (position_resource_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(position_resource_));
        position_resource_ = nullptr;
    }
    if (normal_resource_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(normal_resource_));
        normal_resource_ = nullptr;
    }
    if (triangle_resource_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(triangle_resource_));
        triangle_resource_ = nullptr;
    }

    // CUDA 메모리 해제
    CUDA_CHECK(cudaFree(previous_position_buffer_));
    CUDA_CHECK(cudaFree(predicted_position_buffer_));
    CUDA_CHECK(cudaFree(velocity_buffer_));
    CUDA_CHECK(cudaFree(mass_buffer_));

    previous_position_buffer_ = nullptr;
    predicted_position_buffer_ = nullptr;
    velocity_buffer_ = nullptr;
    mass_buffer_ = nullptr;
    triangle_vertex_indices_buffer_ = nullptr;
    triangle_color_counts_.clear();
    vertex_count_ = 0;
    triangle_count_ = 0;
}
