#pragma once

#include "AttachmentConstraints.h"
#include "PdRhs.h"
#include "StretchConstraints.h"
#include "gpu/pd/ConstraintTypes.h"
#include "utils/CudaUtils.h"

#include <cstdint>
#include <tuple>

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
        is_active = false;
    }

    Constraint* buffer = nullptr;
    std::uint32_t count = 0;
    bool is_active = false;
};

class PdConstraints final
{
public:
    void initialize()
    {
        execute_each([](const auto& constraints) { initialize_constraints(constraints.buffer, constraints.count); });

        compute_constraint_rhs_matrices(*this);
    }

    template <ConstraintIndex index>
    auto& get()
    {
        return std::get<index>(constraints_);
    }

    template <typename Function>
    void execute_each(Function&& function)
    {
        for_each_constraint([&](auto& constraints) {
            if (constraints.is_active) {
                function(constraints);
            }
        });
    }

    void allocate_buffers()
    {
        for_each_constraint([](auto& constraints) {
            constraints.is_active = false;
            constraints.allocate_buffer();
            constraints.is_active = constraints.count > 0;
        });
    }

    void release_buffers()
    {
        for_each_constraint([](auto& constraints) { constraints.release_buffer(); });
    }

private:
    template <typename Function>
    void for_each_constraint(Function&& function)
    {
        std::apply([&](auto&... constraints) { (function(constraints), ...); }, constraints_);
    }

    std::tuple<ConstraintSet<DistanceConstraint>, ConstraintSet<AttachmentConstraint>> constraints_;
};
