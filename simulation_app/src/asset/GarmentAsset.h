#pragma once

#include "app/ProjectPaths.h"
#include "asset/MeshGeometryUtils.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct ConverterCommand;

struct GarmentDistanceConstraints final {
    std::vector<MeshEdge> colorized_edges;
    std::vector<MeshEdgeRange> color_ranges;
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
    GarmentDistanceConstraints stretch_constraints;
    GarmentDistanceConstraints bending_constraints;
    glm::vec3 color{0.95f, 0.42f, 0.18f};
    glm::vec3 bounds_center{};
    float bounds_radius = 1.0f;
};

struct GarmentAsset {
    std::filesystem::path garment_asset_path;
    std::string display_name;
};

bool read_garment_asset(const std::filesystem::path& garment_asset_path, GarmentMesh& garment_mesh);
ConverterCommand make_garment_converter_command(
    const ProjectPaths& project_paths,
    const std::filesystem::path& garment_obj_path,
    const std::filesystem::path& garment_asset_path);
std::vector<GarmentAsset> scan_garment_assets(const ProjectPaths& project_paths);
