#pragma once

#include "AttachmentConstraints.h"
#include "PdRhs.h"
#include "StrainConstraints.h"
#include "gpu/common/ClothGpuState.h"
#include "gpu/pd/ConstraintTypes.h"
#include "simulation/SceneState.h"
#include "simulation/SimulationParams.h"
#include "utils/CudaUtils.h"

#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <cuda_runtime.h>

enum ConstraintIndex
{
    Strain = 0,
    Attachment = 1
};

template <typename Constraint>
struct ConstraintSet final
{
    ConstraintSet() = default;
    ConstraintSet(const ConstraintSet&) = delete;
    ConstraintSet& operator=(const ConstraintSet&) = delete;
    ~ConstraintSet() { cudaFree(buffer); }

    void allocate_buffer()
    {
        if (count != 0) {
            CUDA_CHECK(cudaMalloc(&buffer, count * sizeof(Constraint)));
        }
    }

    void release_buffer()
    {
        CUDA_CHECK(cudaFree(buffer));
        buffer = nullptr;
        count = 0;
    }

    std::uint32_t matrix_entry_count() const { return count * Constraint::vertex_count * Constraint::vertex_count; }

    Constraint* buffer = nullptr;
    std::uint32_t count = 0;
};

class PdConstraints final
{
public:
    template <ConstraintIndex index>
    auto& get()
    {
        return std::get<index>(constraints_);
    }

    void initialize(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
    {
        initialize_constraint_counts(garments, cloth_state);
        allocate_buffers();

        execute_each([&garments, &cloth_state](const auto& constraints) {
            initialize_constraints(constraints.buffer, garments, cloth_state);
        });

        compute_constraint_rhs_matrices(*this);
    }

    template <typename Function>
    void execute_each(Function&& function)
    {
        for_each_constraint([&](auto& constraints) {
            if (constraints.count > 0) {
                function(constraints);
            }
        });
    }

    void allocate_buffers()
    {
        for_each_constraint([](auto& constraints) { constraints.allocate_buffer(); });
    }

    void release_buffers()
    {
        for_each_constraint([](auto& constraints) { constraints.release_buffer(); });
    }

private:
    void initialize_constraint_counts(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
    {
        get<ConstraintIndex::Strain>().count = cloth_state.element_counts().triangle;
        
        for (const GarmentObject& garment : garments) {
            get<ConstraintIndex::Attachment>().count += garment.mesh.attachment_vertex_indices.size();
        }

        // check total count
        std::uint64_t total_count = 0;
        for_each_constraint([&total_count](const auto& constraints) { total_count += constraints.count; });

        if (total_count > simulation_limits::max_constraint_count) {
            throw std::length_error("Total constraint count exceeds the simulation limit.");
        }
    }

    template <typename Function>
    void for_each_constraint(Function&& function)
    {
        std::apply([&](auto&... constraints) { (function(constraints), ...); }, constraints_);
    }

    std::tuple<ConstraintSet<StrainConstraint>, ConstraintSet<AttachmentConstraint>> constraints_;
};
