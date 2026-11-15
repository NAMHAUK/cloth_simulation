#include "PdSystemMatrix.h"
#include "gpu/pd/PdGpuState.h"
#include "utils/CudaUtils.h"

#include <cstdint>

#include <cuda_runtime.h>
#include <thrust/device_vector.h>
#include <thrust/reduce.h>
#include <thrust/sort.h>

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

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void write_vertex_diagonal_entries_kernel(MatrixIndex* indices,
                                                     float* values,
                                                     std::uint32_t vertex_count,
                                                     const float* masses,
                                                     float inv_dt_squared)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < vertex_count) {
        indices[index] = {index, index};
        values[index] = masses[index] * inv_dt_squared;
    }
}

// 각 constraint의 weight × AᵀA를 계산해 모든 원소를 배열에 순서대로 저장
// [..., AᵀA[0,0], AᵀA[0,1], ..., AᵀA[n-1,n-1], ...]
template <typename Constraint>
__global__ void write_constraint_entries_kernel(MatrixIndex* indices,
                                                float* values,
                                                const Constraint* constraints,
                                                std::uint32_t constraint_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    const Constraint& constraint = constraints[index];
    std::uint32_t offset = index * Constraint::vertex_count * Constraint::vertex_count;

    for (std::uint32_t column_a = 0; column_a < Constraint::vertex_count; ++column_a) {
        for (std::uint32_t column_b = 0; column_b < Constraint::vertex_count; ++column_b) {
            float value = 0.0f;

            // Aᵀ·A[column_a, column_b]: A의 column_a와 column_b 내적
            for (std::uint32_t row = 0; row < constraint.matrix_row_count; ++row) {
                const float* row_values = constraint.A[row];
                value += row_values[column_a] * row_values[column_b];
            }

            indices[offset] = {constraint.vertex_indices[column_a], constraint.vertex_indices[column_b]};
            values[offset] = constraint.weight * value;
            ++offset;
        }
    }
}

__global__ void build_csr_matrix_kernel(CsrMatrix matrix,
                                        const MatrixIndex* indices,
                                        const float* values,
                                        std::uint32_t entry_count,
                                        std::uint32_t vertex_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (index < entry_count) {
        const MatrixIndex& entry = indices[index];

        matrix.col_indices_ptr[index] = entry.column;
        matrix.values_ptr[index] = values[index];

        if (index == 0 || indices[index - 1].row != entry.row) {
            matrix.row_offsets_ptr[entry.row] = index;
        }
    }
    
    if (index == 0) {
        matrix.row_offsets_ptr[vertex_count] = entry_count;
    }
}

}

void prepare_pd_system(PdGpuState& state)
{
    const std::uint32_t vertex_count = state.cloth_gpu_state().vertex_count();
    
    // 1. 모든 constraint의 영향 값을 저장할 수 있는 임시 배열 생성
    std::uint32_t index_count = vertex_count;
    state.constraints().execute_each([&](const auto& constraints) { index_count += constraints.matrix_entry_count(); });

    thrust::device_vector<MatrixIndex> matrix_indices(index_count, thrust::no_init);
    thrust::device_vector<float> matrix_values(index_count, thrust::no_init);

    // 2. M/dt²와 각 constraint의 AᵀA를 계산해 matrix_indices, matrix_values에 저장
    // matrix_indices: system matrix에서 (row, col) index
    // matrix_values: system matrix에서 (row, col) 값
    write_vertex_diagonal_entries_kernel<<<block_count(vertex_count, block_size), block_size>>>(
        matrix_indices.data().get(),
        matrix_values.data().get(),
        vertex_count,
        state.cloth_gpu_state().mass_buffer(),
        state.inv_dt_squared_);
    CUDA_CHECK(cudaGetLastError());

    std::uint32_t offset = vertex_count;
    state.constraints().execute_each([&](const auto& constraints) {
        write_constraint_entries_kernel<<<block_count(constraints.count, block_size), block_size>>>(
            matrix_indices.data().get() + offset,
            matrix_values.data().get() + offset,
            constraints.buffer,
            constraints.count);
        CUDA_CHECK(cudaGetLastError());

        offset += constraints.matrix_entry_count();
    });

    // 3. (row, col) index 기준으로 정렬
    thrust::sort_by_key(matrix_indices.begin(), matrix_indices.end(), matrix_values.begin());

    // 4. system matrix에서 동일한 (row, col) element를 합쳐서 중복 제거
    thrust::device_vector<MatrixIndex> unique_indices(index_count, thrust::no_init);
    thrust::device_vector<float> summed_values(index_count, thrust::no_init);

    const auto result_end = thrust::reduce_by_key(matrix_indices.begin(),
                                                  matrix_indices.end(),
                                                  matrix_values.begin(),
                                                  unique_indices.begin(),
                                                  summed_values.begin());
    index_count = static_cast<std::uint32_t>(result_end.first - unique_indices.begin());

    // 5. 구한 System matrix 내부의 index와 value 값으로 CSR matrix로 변환
    CUDA_CHECK(cudaMalloc(&state.system_matrix_.row_offsets_ptr, (vertex_count + 1) * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(&state.system_matrix_.col_indices_ptr, index_count * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(&state.system_matrix_.values_ptr, index_count * sizeof(float)));

    build_csr_matrix_kernel<<<block_count(index_count, block_size), block_size>>>(state.system_matrix_,
                                                                                  unique_indices.data().get(),
                                                                                  summed_values.data().get(),
                                                                                  index_count,
                                                                                  vertex_count);
    CUDA_CHECK(cudaGetLastError());
}
