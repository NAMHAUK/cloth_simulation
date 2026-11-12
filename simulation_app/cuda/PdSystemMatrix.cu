#include "PdSystemMatrix.h"
#include "gpu/pd/PdGpuState.h"
#include "utils/CudaUtils.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include <cuda/std/functional>
#include <cuda_runtime.h>
#include <thrust/device_ptr.h>
#include <thrust/device_vector.h>
#include <thrust/sort.h>
#include <thrust/transform_scan.h>
#include <thrust/unique.h>

namespace {
constexpr std::uint32_t block_size = 256;

struct MatrixIndex
{
    std::uint32_t row;
    std::uint32_t column;

    __host__ __device__ bool operator<(const MatrixIndex& other) const
    {
        return row < other.row || (row == other.row && column < other.column);
    }

    __host__ __device__ bool operator==(const MatrixIndex& other) const
    {
        return row == other.row && column == other.column;
    }
};

struct ConstraintIndexCount
{
    __host__ __device__ std::size_t operator()(const PdConstraintMatrix& constraint) const
    {
        return static_cast<std::size_t>(constraint.A.columns) * constraint.A.columns;
    }
};

// Initialize system matrix
__global__ void write_matrix_indices_kernel(const PdConstraintMatrix* constraints,
                                            std::uint32_t constraint_count,
                                            std::uint32_t vertex_count,
                                            const std::size_t* constraint_offsets,
                                            MatrixIndex* indices)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < vertex_count) {
        indices[index] = {index, index};
    }
    if (index < constraint_count) {
        const PdConstraintMatrix& constraint = constraints[index];
        std::size_t offset = vertex_count + constraint_offsets[index];

        for (std::uint32_t row = 0; row < constraint.A.columns; ++row) {
            for (std::uint32_t column = 0; column < constraint.A.columns; ++column) {
                indices[offset] = {constraint.vertex_indices_ptr[row], constraint.vertex_indices_ptr[column]};
                ++offset;
            }
        }
    }
}

thrust::device_vector<MatrixIndex> build_system_matrix_indices(const PdConstraintMatrix* constraints,
                                                               std::uint32_t constraint_count,
                                                               std::uint32_t vertex_count)
{
    thrust::device_vector<std::size_t> constraint_offsets(static_cast<std::size_t>(constraint_count) + 1, 0);
    const auto constraint_begin = thrust::device_pointer_cast(constraints);

    thrust::transform_inclusive_scan(constraint_begin,
                                     constraint_begin + constraint_count,
                                     constraint_offsets.begin() + 1,
                                     ConstraintIndexCount{},
                                     cuda::std::plus<std::size_t>{});

    const std::size_t index_count = vertex_count + constraint_offsets.back();
    if (index_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("CSR candidate entry count exceeds the 32-bit index limit.");
    }

    thrust::device_vector<MatrixIndex> indices(index_count);
    const std::uint32_t block_count = (std::max(vertex_count, constraint_count) - 1) / block_size + 1;

    write_matrix_indices_kernel<<<block_count, block_size>>>(constraints,
                                                             constraint_count,
                                                             vertex_count,
                                                             thrust::raw_pointer_cast(constraint_offsets.data()),
                                                             thrust::raw_pointer_cast(indices.data()));
    CUDA_CHECK(cudaGetLastError());

    thrust::sort(indices.begin(), indices.end());
    const auto unique_end = thrust::unique(indices.begin(), indices.end());
    indices.erase(unique_end, indices.end());

    return indices;
}

__global__ void initialize_csr_matrix_kernel(CsrMatrix matrix,
                                             const MatrixIndex* indices,
                                             std::uint32_t entry_count,
                                             const float* masses,
                                             std::uint32_t vertex_count,
                                             float inv_dt_squared)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < entry_count) {
        const MatrixIndex& entry = indices[index];
        matrix.column_indices_ptr[index] = entry.column;
        matrix.values_ptr[index] = entry.row == entry.column ? masses[entry.row] * inv_dt_squared : 0.0f;

        if (index == 0 || indices[index - 1].row != entry.row) {
            matrix.row_offsets_ptr[entry.row] = index;
        }
    }
    
    if (index == 0) {
        matrix.row_offsets_ptr[vertex_count] = entry_count;
    }
}

}

void build_system_matrix(CsrMatrix& matrix,
                         const PdConstraintMatrix* constraints,
                         std::uint32_t constraint_count,
                         const float* masses,
                         std::uint32_t vertex_count,
                         float inv_dt_squared)
{
    const auto indices = build_system_matrix_indices(constraints, constraint_count, vertex_count);
    const auto entry_count = static_cast<std::uint32_t>(indices.size());

    CUDA_CHECK(cudaMalloc(&matrix.row_offsets_ptr, (static_cast<std::size_t>(vertex_count) + 1) * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(&matrix.column_indices_ptr, entry_count * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(&matrix.values_ptr, entry_count * sizeof(float)));

    // Initialize System matrix
    const std::uint32_t csr_block_count = (entry_count - 1) / block_size + 1;
    initialize_csr_matrix_kernel<<<csr_block_count, block_size>>>(matrix,
                                                                  thrust::raw_pointer_cast(indices.data()),
                                                                  entry_count,
                                                                  masses,
                                                                  vertex_count,
                                                                  inv_dt_squared);
    CUDA_CHECK(cudaGetLastError());
}
