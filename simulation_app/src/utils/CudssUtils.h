#pragma once

#include "utils/CudaUtils.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <cudss.h>

inline void check_cudss_errors(cudssStatus_t result, const char* call, const char* file, int line)
{
    if (result != CUDSS_STATUS_SUCCESS) {
        const std::string location = std::string(file) + ":" + std::to_string(line);
        throw std::runtime_error(location + ": " + call + ": cuDSS status " + std::to_string(result));
    }
}

#define CUDSS_CHECK(call) check_cudss_errors((call), #call, __FILE__, __LINE__)

struct CudssSystem
{
    cudssHandle_t handle = nullptr;
    cudssConfig_t config = nullptr;
    cudssData_t decomposed_matrix = nullptr;
    cudssMatrix_t matrix_descriptor = nullptr;
    cudssMatrix_t vector_descriptor = nullptr;

    void initialize()
    {
        CUDSS_CHECK(cudssCreate(&handle));
        CUDSS_CHECK(cudssConfigCreate(&config));
        CUDSS_CHECK(cudssDataCreate(handle, &decomposed_matrix));

        const int refinement_steps = 0;
        CUDSS_CHECK(cudssConfigSet(config, CUDSS_CONFIG_IR_N_STEPS, &refinement_steps, sizeof(refinement_steps)));
    }

    void cholesky_decompose(std::uint32_t row_count,
                            std::uint32_t entry_count,
                            std::int32_t* row_offsets,
                            std::int32_t* col_indices,
                            float* values,
                            void* rhs)
    {
        CUDSS_CHECK(cudssMatrixCreateCsr(&matrix_descriptor,
                                         row_count,
                                         row_count,
                                         entry_count,
                                         row_offsets,
                                         nullptr,
                                         col_indices,
                                         values,
                                         CUDA_R_32I,
                                         CUDA_R_32I,
                                         CUDA_R_32F,
                                         CUDSS_MTYPE_SPD,
                                         CUDSS_MVIEW_FULL,
                                         CUDSS_BASE_ZERO));
        CUDSS_CHECK(cudssMatrixCreateDn(&vector_descriptor, row_count, 3, row_count, rhs, CUDA_R_32F, CUDSS_LAYOUT_COL_MAJOR));

        // Cholesky decomposition 수행
        CUDSS_CHECK(cudssExecute(handle,
                                 CUDSS_PHASE_ANALYSIS,
                                 config,
                                 decomposed_matrix,
                                 matrix_descriptor,
                                 vector_descriptor,
                                 vector_descriptor));
        CUDSS_CHECK(cudssExecute(handle,
                                 CUDSS_PHASE_FACTORIZATION,
                                 config,
                                 decomposed_matrix,
                                 matrix_descriptor,
                                 vector_descriptor,
                                 vector_descriptor));
        CUDA_CHECK(cudaStreamSynchronize(nullptr));

        // 분해 validation
        int decomposition_status = 0;
        std::size_t written_bytes = 0;
        CUDSS_CHECK(cudssDataGet(handle,
                                 decomposed_matrix,
                                 CUDSS_DATA_INFO,
                                 &decomposition_status,
                                 sizeof(decomposition_status),
                                 &written_bytes));
        if (decomposition_status != 0) {
            throw std::runtime_error("Cholesky factorization failed: " + std::to_string(decomposition_status));
        }

        // release
        CUDSS_CHECK(cudssMatrixSetCsrPointers(matrix_descriptor, nullptr, nullptr, nullptr, nullptr));
    }

    void release()
    {
        if (handle == nullptr) {
            return;
        }

        CUDSS_CHECK(cudssDataDestroy(handle, decomposed_matrix));
        CUDSS_CHECK(cudssMatrixDestroy(vector_descriptor));
        CUDSS_CHECK(cudssMatrixDestroy(matrix_descriptor));
        CUDSS_CHECK(cudssConfigDestroy(config));
        CUDSS_CHECK(cudssDestroy(handle));

        *this = {};
    }
};
