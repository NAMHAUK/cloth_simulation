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
        cudssMatrix_t input_matrix = nullptr;
        cudssMatrix_t vectors = nullptr;

        // CSR matrix / RHS vector를 CuDSS용으로 변환
        CUDSS_CHECK(cudssMatrixCreateCsr(&input_matrix,
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
        CUDSS_CHECK(cudssMatrixCreateDn(&vectors, row_count, 3, row_count, rhs, CUDA_R_32F, CUDSS_LAYOUT_COL_MAJOR));
        
        // Cholesky decomposition 수행
        CUDSS_CHECK(
            cudssExecute(handle, CUDSS_PHASE_ANALYSIS, config, decomposed_matrix, input_matrix, vectors, vectors));
        CUDSS_CHECK(
            cudssExecute(handle, CUDSS_PHASE_FACTORIZATION, config, decomposed_matrix, input_matrix, vectors, vectors));
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
        CUDSS_CHECK(cudssMatrixDestroy(vectors));
        CUDSS_CHECK(cudssMatrixDestroy(input_matrix));
    }

    void release()
    {
        if (handle == nullptr) {
            return;
        }

        CUDSS_CHECK(cudssDataDestroy(handle, decomposed_matrix));
        CUDSS_CHECK(cudssConfigDestroy(config));
        CUDSS_CHECK(cudssDestroy(handle));

        *this = {};
    }
};
