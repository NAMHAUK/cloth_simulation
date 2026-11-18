#pragma once

#include <cstdint>

#include <vector_types.h>

struct StretchConstraint
{
    static constexpr std::uint32_t vertex_count = 2;
    static constexpr std::uint32_t matrix_row_count = 1;
    static constexpr std::uint32_t projection_vector_count = 1;

    float A[matrix_row_count][vertex_count]{};
    float B[matrix_row_count][projection_vector_count]{};
    float rhs_matrix[vertex_count][projection_vector_count]{};
    float3 p[projection_vector_count]{};
    std::uint32_t vertex_indices[vertex_count]{};
    float weight = 0.0f;
    float rest_length = 0.0f;
    float limit_fraction = 0.0f;
};

struct BendingConstraint
{
    static constexpr std::uint32_t vertex_count = 2;
    static constexpr std::uint32_t matrix_row_count = 1;
    static constexpr std::uint32_t projection_vector_count = 1;

    float A[matrix_row_count][vertex_count]{};
    float B[matrix_row_count][projection_vector_count]{};
    float rhs_matrix[vertex_count][projection_vector_count]{};
    float3 p[projection_vector_count]{};
    std::uint32_t vertex_indices[vertex_count]{};
    float weight = 0.0f;

    float rest_length = 0.0f;
};

struct AttachmentConstraint
{
    static constexpr std::uint32_t vertex_count = 1;
    static constexpr std::uint32_t matrix_row_count = 1;
    static constexpr std::uint32_t projection_vector_count = 1;

    float A[matrix_row_count][vertex_count]{};
    float B[matrix_row_count][projection_vector_count]{};
    float rhs_matrix[vertex_count][projection_vector_count]{};
    float3 p[projection_vector_count]{};
    std::uint32_t vertex_indices[vertex_count]{};
    float weight = 0.0f;
    
    std::uint32_t body_triangle_index = UINT32_MAX;
    float3 barycentric{};
};

struct GroundConstraint
{
    static constexpr std::uint32_t vertex_count = 1;
    static constexpr std::uint32_t matrix_row_count = 1;
    static constexpr std::uint32_t projection_vector_count = 1;

    float A[matrix_row_count][vertex_count]{};
    float B[matrix_row_count][projection_vector_count]{};
    float rhs_matrix[vertex_count][projection_vector_count]{};
    float3 p[projection_vector_count]{};
    std::uint32_t vertex_indices[vertex_count]{};
    float weight = 1.0f;
};

struct BodyCollisionConstraint
{
    static constexpr std::uint32_t vertex_count = 1;
    static constexpr std::uint32_t matrix_row_count = 1;
    static constexpr std::uint32_t projection_vector_count = 1;

    float A[matrix_row_count][vertex_count]{};
    float B[matrix_row_count][projection_vector_count]{};
    float rhs_matrix[vertex_count][projection_vector_count]{};
    float3 p[projection_vector_count]{};
    std::uint32_t vertex_indices[vertex_count]{};
    float weight = 0.0f;

    float3 contact_point{};
    float3 normal{};
};
