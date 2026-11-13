#include "PdSystemMatrix.h"
#include "gpu/pd/PdGpuState.h"
#include "utils/CudaUtils.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include <cuda/std/algorithm>
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
        return static_cast<std::size_t>(constraint.vertex_count) * constraint.vertex_count;
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

        for (std::uint32_t row = 0; row < constraint.vertex_count; ++row) {
            for (std::uint32_t column = 0; column < constraint.vertex_count; ++column) {
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

    write_matrix_indices_kernel<<<block_count(std::max(vertex_count, constraint_count), block_size), block_size>>>(
        constraints,
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
        matrix.col_indices_ptr[index] = entry.column;
        matrix.values_ptr[index] = entry.row == entry.column ? masses[entry.row] * inv_dt_squared : 0.0f;

        if (index == 0 || indices[index - 1].row != entry.row) {
            matrix.row_offsets_ptr[entry.row] = index;
        }
    }
    
    if (index == 0) {
        matrix.row_offsets_ptr[vertex_count] = entry_count;
    }
}

// Constraint contributions 
__device__ std::uint32_t find_csr_index(const CsrMatrix& matrix, std::uint32_t row, std::uint32_t column)
{
    const auto* column_index_ptr = cuda::std::lower_bound(matrix.col_indices_ptr + matrix.row_offsets_ptr[row],
                                                          matrix.col_indices_ptr + matrix.row_offsets_ptr[row + 1],
                                                          column);

    return static_cast<std::uint32_t>(column_index_ptr - matrix.col_indices_ptr);
}

__global__ void accumulate_constraint_matrix_kernel(CsrMatrix matrix,
                                                    const PdConstraintMatrix* constraints,
                                                    std::uint32_t constraint_count)
{
    const std::uint32_t constraint_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (constraint_index >= constraint_count) {
        return;
    }

    // 각 constraint의 {w· S^T · A^T · A · S}를 system_matrix에 누적
    const PdConstraintMatrix& constraint = constraints[constraint_index];
    for (std::uint32_t column_a = 0; column_a < constraint.vertex_count; ++column_a) {
        const std::uint32_t system_matrix_row = constraint.vertex_indices_ptr[column_a];

        for (std::uint32_t column_b = 0; column_b < constraint.vertex_count; ++column_b) {
            float value = 0.0f;
            
            // (A^T·A)의 (column_a, column_b) 원소: A의 column_a와 column_b 내적
            for (std::uint32_t row = 0; row < constraint.A.rows; ++row) {
                const float* row_values = constraint.A.values_ptr + row * constraint.A.row_stride;
                value += row_values[column_a] * row_values[column_b];
            }
            
            // system_matrix의 (row, col)에 weight * A^T·A[column_a, column_b] 누적 계산
            const std::uint32_t system_matrix_col = constraint.vertex_indices_ptr[column_b];
            const auto system_matrix_index = find_csr_index(matrix, system_matrix_row, system_matrix_col);
            atomicAdd(&matrix.values_ptr[system_matrix_index], constraint.weight * value);
        }
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
    CUDA_CHECK(cudaMalloc(&matrix.col_indices_ptr, entry_count * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(&matrix.values_ptr, entry_count * sizeof(float)));

    // Initialize System matrix
    initialize_csr_matrix_kernel<<<block_count(entry_count, block_size), block_size>>>(
        matrix,
        thrust::raw_pointer_cast(indices.data()),
        entry_count,
        masses,
        vertex_count,
        inv_dt_squared);
    CUDA_CHECK(cudaGetLastError());

    // system matrix에 constraint 항 반영
    accumulate_constraint_matrix_kernel<<<block_count(constraint_count, block_size), block_size>>>(matrix,
                                                                                                   constraints,
                                                                                                   constraint_count);
    CUDA_CHECK(cudaGetLastError());
}
