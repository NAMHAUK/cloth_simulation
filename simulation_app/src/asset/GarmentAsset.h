#pragma once

#include "asset/MeshGeometryUtils.h"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/vec3.hpp>

using GarmentStretchEdge = MeshEdge;
using GarmentStretchRange = MeshEdgeRange;

struct GarmentStretchConstraints final {
    std::vector<GarmentStretchEdge> colorized_edges;
    std::vector<GarmentStretchRange> color_ranges;
    std::vector<float> rest_lengths;

    bool is_valid() const
    {
        return !colorized_edges.empty() &&
               !color_ranges.empty() &&
               colorized_edges.size() == rest_lengths.size();
    }
};

struct GarmentMesh {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    VertexFaceAdjacency adjacency;
    GarmentStretchConstraints stretch_constraints;
    glm::vec3 color{0.95f, 0.42f, 0.18f};
    glm::vec3 bounds_center{};
    float bounds_radius = 1.0f;
};

bool read_garment_obj(const std::filesystem::path& obj_path, GarmentMesh& garment_mesh);
