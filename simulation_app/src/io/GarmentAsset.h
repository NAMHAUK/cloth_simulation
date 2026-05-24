#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/vec3.hpp>

struct GarmentMesh {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    glm::vec3 color{0.95f, 0.42f, 0.18f};
    glm::vec3 bounds_center{};
    float bounds_radius = 1.0f;
};

bool load_garment_mesh(const std::filesystem::path& obj_path, GarmentMesh& garment_mesh);
