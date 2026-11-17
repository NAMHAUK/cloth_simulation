#pragma once

#include "AttachmentConstraints.h"
#include "PdRhs.h"
#include "StretchConstraints.h"
#include "gpu/pd/ConstraintTypes.h"
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
        validate_constraint_count();

        for_each_constraint([](auto& constraints) { constraints.allocate_buffer(); });
    }

    void release_buffers()
    {
        for_each_constraint([](auto& constraints) { constraints.release_buffer(); });
    }

private:
    void validate_constraint_count()
    {
        std::uint64_t constraint_count = 0;
        for_each_constraint([&](const auto& constraints) { constraint_count += constraints.count; });

        if (constraint_count > simulation_limits::max_constraint_count) {
            throw std::length_error("Total constraint count exceeds the simulation limit.");
        }
    }

    template <typename Function>
    void for_each_constraint(Function&& function)
    {
        std::apply([&](auto&... constraints) { (function(constraints), ...); }, constraints_);
    }

    std::tuple<ConstraintSet<DistanceConstraint>, ConstraintSet<AttachmentConstraint>> constraints_;
};
