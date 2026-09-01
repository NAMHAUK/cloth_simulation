#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class ClothGpuState;

class BvhBoundsUpdater final
{
public:
    BvhBoundsUpdater() = default;
    BvhBoundsUpdater(const BvhBoundsUpdater&) = delete;
    BvhBoundsUpdater& operator=(const BvhBoundsUpdater&) = delete;

    void initialize(const std::filesystem::path& shader_dir,
                    float body_detection_distance,
                    QOpenGLFunctions_4_5_Core& gl);
    void set_body_level_offsets(const std::vector<std::uint32_t>& triangle_level_offsets,
                                const std::vector<std::uint32_t>& vertex_level_offsets,
                                const std::vector<std::uint32_t>& edge_level_offsets);

    void update_body_bvh(QOpenGLFunctions_4_5_Core& gl) const;
    void update_cloth_bvh(const ClothGpuState& cloth_state,
                          float bounds_margin,
                          QOpenGLFunctions_4_5_Core& gl) const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint body_program_ = 0;
    GLint body_triangle_first_node_index_loc_ = -1;
    GLint body_triangle_node_count_loc_ = -1;
    GLint body_vertex_first_node_index_loc_ = -1;
    GLint body_vertex_node_count_loc_ = -1;
    GLint body_edge_first_node_index_loc_ = -1;
    GLint body_edge_node_count_loc_ = -1;
    std::size_t body_bvh_level_count_ = 0;
    std::vector<std::uint32_t> body_triangle_level_offsets_;
    std::vector<std::uint32_t> body_vertex_level_offsets_;
    std::vector<std::uint32_t> body_edge_level_offsets_;

    GLuint cloth_program_ = 0;
    GLint cloth_level_first_node_index_loc_ = -1;
    GLint cloth_level_node_count_loc_ = -1;
    GLint cloth_bounds_margin_loc_ = -1;
};
