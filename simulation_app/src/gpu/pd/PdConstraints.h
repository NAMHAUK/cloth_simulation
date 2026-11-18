#pragma once

#include "AttachmentConstraints.h"
#include "BendingConstraints.h"
#include "BodyCollisionConstraints.h"
#include "GroundConstraints.h"
#include "PdRhs.h"
#include "StretchConstraints.h"
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
    Stretch = 0,
    Bending = 1,
    Attachment = 2,
    Ground = 3,
    BodyCollision = 4
};

template <typename Constraint>
struct ConstraintSet final
{
    ConstraintSet() = default;
    ConstraintSet(const ConstraintSet&) = delete;
    ConstraintSet& operator=(const ConstraintSet&) = delete;
    ~ConstraintSet() { cudaFree(buffer); }

    void reserve(std::uint32_t required_capacity)
    {
        if (required_capacity <= capacity) {
            return;
        }

        Constraint* new_buffer = nullptr;
        CUDA_CHECK(cudaMalloc(&new_buffer, required_capacity * sizeof(Constraint)));
        CUDA_CHECK(cudaFree(buffer));

        buffer = new_buffer;
        capacity = required_capacity;
    }

    void release_buffer()
    {
        CUDA_CHECK(cudaFree(buffer));
        buffer = nullptr;
        count = 0;
        capacity = 0;
    }

    std::uint32_t matrix_entry_count() const { return count * Constraint::vertex_count * Constraint::vertex_count; }

    Constraint* buffer = nullptr;
    std::uint32_t count = 0;
    std::uint32_t capacity = 0;
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
        validate_constraint_count();
        allocate_buffers();

        execute_each([&garments, &cloth_state](const auto& constraints) {
            initialize_constraints(constraints.buffer, garments, cloth_state);
        });

        compute_constraint_rhs_matrices(*this);
    }

    void validate_constraint_count()
    {
        // check total count
        std::uint64_t total_count = 0;
        for_each_constraint([&total_count](const auto& constraints) { total_count += constraints.count; });

        if (total_count > simulation_limits::max_constraint_count) {
            throw std::length_error("Total constraint count exceeds the simulation limit.");
        }
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
        for_each_constraint([](auto& constraints) { constraints.reserve(constraints.count); });
    }

    void release_buffers()
    {
        for_each_constraint([](auto& constraints) { constraints.release_buffer(); });
    }

private:
    void initialize_constraint_counts(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
    {
        get<ConstraintIndex::Stretch>().count = 0;
        get<ConstraintIndex::Bending>().count = 0;
        get<ConstraintIndex::Ground>().count = cloth_state.element_counts().vertex;
        get<ConstraintIndex::BodyCollision>().count = 0;

        for (const GarmentObject& garment : garments) {
            get<ConstraintIndex::Stretch>().count += garment.mesh.stretch_constraints.colorized_edges.size();
            get<ConstraintIndex::Bending>().count += garment.mesh.bending_constraints.colorized_edges.size();
            get<ConstraintIndex::Attachment>().count += garment.mesh.attachment_vertex_indices.size();
        }
    }

    template <typename Function>
    void for_each_constraint(Function&& function)
    {
        std::apply([&](auto&... constraints) { (function(constraints), ...); }, constraints_);
    }

    std::tuple<ConstraintSet<StretchConstraint>,
               ConstraintSet<BendingConstraint>,
               ConstraintSet<AttachmentConstraint>,
               ConstraintSet<GroundConstraint>,
               ConstraintSet<BodyCollisionConstraint>>
        constraints_;
};
