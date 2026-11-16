#include "gpu/pd/PdClothGpuState.h"
#include "ClothMass.h"
#include "MeshNormals.h"
#include "gpu/common/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "simulation/SimulationParams.h"
#include "utils/CudaUtils.h"

#include <cstddef>
#include <stdexcept>

#include <cuda_runtime.h>

void PdClothGpuState::create_simulation_buffers(const std::vector<GarmentObject>& garments,
                                                const ClothGpuState& cloth_state)
{
    // position
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&previous_position_buffer_), sizeof(float4) * vertex_count_));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&predicted_position_buffer_), sizeof(float4) * vertex_count_));

    // velocity
    const std::size_t velocity_bytes = sizeof(float4) * vertex_count_;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&velocity_buffer_), velocity_bytes));
    CUDA_CHECK(cudaMemset(velocity_buffer_, 0, velocity_bytes));

    // mass
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&mass_buffer_), sizeof(float) * vertex_count_));
    for (const GarmentObject& garment : garments) {
        const GarmentBufferState& garment_state = cloth_state.garment_states()[garment.layer];

        set_vertex_masses(mass_buffer_ + garment_state.vertex_start_index,
                          garment_state.vertex_count,
                          garment.surface_density);
    }
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

    shared_buffers.register_buffers(cloth_state.buffer_set().current_position,
                                    cloth_state.buffer_set().vertex_normal,
                                    cloth_state.buffer_set().triangle_vertex_indices);

    const MappedMeshBuffers mesh_buffers = shared_buffers.map();
    update_normals(mesh_buffers, vertex_count_, triangle_color_counts_);
    shared_buffers.unmap();
}

void PdClothGpuState::reset_velocities() const
{
    CUDA_CHECK(cudaMemset(velocity_buffer_, 0, sizeof(float4) * vertex_count_));
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

// Release
void PdClothGpuState::release_simulation_buffers()
{
    // CUDA 메모리 해제
    CUDA_CHECK(cudaFree(previous_position_buffer_));
    CUDA_CHECK(cudaFree(predicted_position_buffer_));
    CUDA_CHECK(cudaFree(velocity_buffer_));
    CUDA_CHECK(cudaFree(mass_buffer_));

    previous_position_buffer_ = nullptr;
    predicted_position_buffer_ = nullptr;
    velocity_buffer_ = nullptr;
    mass_buffer_ = nullptr;
}

void PdClothGpuState::release_normal_buffers()
{
    shared_buffers.release();

    triangle_color_counts_.clear();
    vertex_count_ = 0;
    triangle_count_ = 0;
}
