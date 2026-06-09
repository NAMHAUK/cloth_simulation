#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

inline constexpr std::uint32_t invalid_character_bvh_node = 0xFFFFFFFFu;

struct CharacterBvhNode final {
    glm::vec4 min_bounds{};
    glm::vec4 max_bounds{};
    glm::uvec4 metadata{invalid_character_bvh_node, invalid_character_bvh_node, 0u, 0u};
};

struct BvhBoundsUpdateLevelRange final {
    std::uint32_t first_node = 0;
    std::uint32_t node_count = 0;
};

struct CharacterBvhBuildResult final {
    std::vector<std::uint32_t> triangle_indices;
    std::vector<CharacterBvhNode> nodes;
    std::vector<BvhBoundsUpdateLevelRange> bounds_update_level_ranges;
    std::uint32_t root_node_index = 0;

    bool is_valid(std::uint32_t triangle_count) const;
};

CharacterBvhBuildResult build_character_bvh(std::uint32_t vertex_count,
                                            const std::vector<std::uint32_t>& triangle_indices,
                                            const std::vector<float>& vertices);
