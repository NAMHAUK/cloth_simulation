#include "app/ProjectPaths.h"
#include "asset/AssetIO.h"
#include "gpu/collision/MeshBvhBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr double metric_epsilon = 1.0e-12;

struct Bounds final {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};
};

struct FrameMetrics final {
    std::uint32_t frame_index = 0;
    double node_surface_sum_ratio = 1.0;
    double leaf_surface_sum_ratio = 1.0;
    double max_leaf_surface_ratio = 1.0;
    double max_leaf_extent_ratio = 1.0;
    double root_surface_ratio = 1.0;
    double max_sibling_overlap_fraction = 0.0;
};

struct MotionReport final {
    std::filesystem::path path;
    std::uint32_t frame_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    FrameMetrics worst_node_surface_sum;
    FrameMetrics worst_leaf_surface_sum;
    FrameMetrics worst_leaf_surface;
    FrameMetrics worst_leaf_extent;
    FrameMetrics worst_root_surface;
    FrameMetrics worst_sibling_overlap;
};

glm::vec3 load_frame_position(const CharacterMesh& mesh, std::uint32_t frame_index, std::uint32_t vertex_index)
{
    const std::size_t base =
        (static_cast<std::size_t>(frame_index) * mesh.vertex_count + vertex_index) * triangle_vertex_count;
    return {
        mesh.vertices[base],
        mesh.vertices[base + 1u],
        mesh.vertices[base + 2u],
    };
}

Bounds make_empty_bounds()
{
    return {};
}

void expand_bounds(Bounds& bounds, const glm::vec3& position)
{
    bounds.min = glm::min(bounds.min, position);
    bounds.max = glm::max(bounds.max, position);
}

void expand_bounds(Bounds& bounds, const Bounds& other)
{
    bounds.min = glm::min(bounds.min, other.min);
    bounds.max = glm::max(bounds.max, other.max);
}

Bounds to_bounds(const MeshBvhNode& node)
{
    return {glm::vec3{node.min_bounds}, glm::vec3{node.max_bounds}};
}

void write_bounds(MeshBvhNode& node, const Bounds& bounds)
{
    node.min_bounds = glm::vec4(bounds.min, 0.0f);
    node.max_bounds = glm::vec4(bounds.max, 0.0f);
}

bool is_leaf_node(const MeshBvhNode& node)
{
    return node.triangle_count > 0u;
}

double surface_area(const Bounds& bounds)
{
    const glm::vec3 extent = glm::max(bounds.max - bounds.min, glm::vec3{0.0f});
    return 2.0 * (static_cast<double>(extent.x) * extent.y +
                  static_cast<double>(extent.y) * extent.z +
                  static_cast<double>(extent.z) * extent.x);
}

double volume(const Bounds& bounds)
{
    const glm::vec3 extent = glm::max(bounds.max - bounds.min, glm::vec3{0.0f});
    return static_cast<double>(extent.x) * extent.y * extent.z;
}

double extent_length(const Bounds& bounds)
{
    return static_cast<double>(glm::length(glm::max(bounds.max - bounds.min, glm::vec3{0.0f})));
}

Bounds intersect_bounds(const Bounds& lhs, const Bounds& rhs)
{
    return {
        glm::max(lhs.min, rhs.min),
        glm::min(lhs.max, rhs.max),
    };
}

bool has_intersection(const Bounds& bounds)
{
    return bounds.min.x < bounds.max.x &&
           bounds.min.y < bounds.max.y &&
           bounds.min.z < bounds.max.z;
}

double safe_ratio(double value, double baseline)
{
    if (baseline <= metric_epsilon) {
        return value <= metric_epsilon ? 1.0 : value / metric_epsilon;
    }
    return value / baseline;
}

std::vector<MeshBvhNode> compute_frame_nodes(const CharacterMesh& mesh,
                                             const MeshBvhData& bvh,
                                             std::uint32_t frame_index)
{
    std::vector<MeshBvhNode> nodes = bvh.nodes;
    for (const BvhNodeRange& range : bvh.node_ranges_by_level) {
        for (std::uint32_t local_node_index = 0; local_node_index < range.node_count; ++local_node_index) {
            const std::uint32_t node_index = range.first_node + local_node_index;
            MeshBvhNode& node = nodes[node_index];
            Bounds bounds = make_empty_bounds();

            if (is_leaf_node(node)) {
                for (std::uint32_t triangle_offset = 0u; triangle_offset < node.triangle_count; ++triangle_offset) {
                    const std::uint32_t triangle_index = node.first_triangle_index + triangle_offset;
                    const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
                    expand_bounds(bounds, load_frame_position(mesh, frame_index, bvh.triangle_indices[index_base]));
                    expand_bounds(bounds, load_frame_position(mesh, frame_index, bvh.triangle_indices[index_base + 1u]));
                    expand_bounds(bounds, load_frame_position(mesh, frame_index, bvh.triangle_indices[index_base + 2u]));
                }
            } else {
                expand_bounds(bounds, to_bounds(nodes[node.left_child_index]));
                expand_bounds(bounds, to_bounds(nodes[node.right_child_index]));
            }

            write_bounds(node, bounds);
        }
    }
    return nodes;
}

FrameMetrics compute_frame_metrics(const MeshBvhData& bvh,
                                   const std::vector<MeshBvhNode>& baseline_nodes,
                                   const std::vector<MeshBvhNode>& frame_nodes,
                                   std::uint32_t frame_index)
{
    FrameMetrics metrics;
    metrics.frame_index = frame_index;

    double baseline_node_surface_sum = 0.0;
    double frame_node_surface_sum = 0.0;
    double baseline_leaf_surface_sum = 0.0;
    double frame_leaf_surface_sum = 0.0;

    for (std::size_t node_index = 0; node_index < frame_nodes.size(); ++node_index) {
        const MeshBvhNode& baseline_node = baseline_nodes[node_index];
        const MeshBvhNode& frame_node = frame_nodes[node_index];
        const Bounds baseline_bounds = to_bounds(baseline_node);
        const Bounds frame_bounds = to_bounds(frame_node);
        const double baseline_surface = surface_area(baseline_bounds);
        const double frame_surface = surface_area(frame_bounds);

        baseline_node_surface_sum += baseline_surface;
        frame_node_surface_sum += frame_surface;

        if (is_leaf_node(frame_node)) {
            baseline_leaf_surface_sum += baseline_surface;
            frame_leaf_surface_sum += frame_surface;
            metrics.max_leaf_surface_ratio = std::max(metrics.max_leaf_surface_ratio,
                                                      safe_ratio(frame_surface, baseline_surface));
            metrics.max_leaf_extent_ratio = std::max(metrics.max_leaf_extent_ratio,
                                                     safe_ratio(extent_length(frame_bounds),
                                                                extent_length(baseline_bounds)));
            continue;
        }

        const Bounds parent_bounds = frame_bounds;
        const Bounds overlap_bounds = intersect_bounds(to_bounds(frame_nodes[frame_node.left_child_index]),
                                                       to_bounds(frame_nodes[frame_node.right_child_index]));
        if (has_intersection(overlap_bounds)) {
            metrics.max_sibling_overlap_fraction = std::max(
                metrics.max_sibling_overlap_fraction,
                safe_ratio(volume(overlap_bounds), volume(parent_bounds))
            );
        }
    }

    metrics.node_surface_sum_ratio = safe_ratio(frame_node_surface_sum, baseline_node_surface_sum);
    metrics.leaf_surface_sum_ratio = safe_ratio(frame_leaf_surface_sum, baseline_leaf_surface_sum);
    metrics.root_surface_ratio = safe_ratio(surface_area(to_bounds(frame_nodes[bvh.root_node_index])),
                                           surface_area(to_bounds(baseline_nodes[bvh.root_node_index])));
    return metrics;
}

bool has_matching_topology(const CharacterMesh& default_mesh, const CharacterMesh& motion_mesh)
{
    return default_mesh.vertex_count == motion_mesh.vertex_count &&
           default_mesh.index_count == motion_mesh.index_count &&
           default_mesh.indices == motion_mesh.indices;
}

MotionReport analyze_motion_mesh(const std::filesystem::path& motion_path,
                                 const CharacterMesh& default_mesh,
                                 const CharacterMesh& motion_mesh,
                                 const MeshBvhData& default_bvh,
                                 const std::vector<MeshBvhNode>& baseline_nodes)
{
    MotionReport report;
    report.path = motion_path;
    if (!has_matching_topology(default_mesh, motion_mesh)) {
        std::cerr << "Skipping motion with different topology: " << motion_path << '\n';
        return report;
    }

    report.frame_count = motion_mesh.frame_count;
    report.vertex_count = motion_mesh.vertex_count;
    report.triangle_count = motion_mesh.index_count / triangle_vertex_count;

    for (std::uint32_t frame_index = 0; frame_index < motion_mesh.frame_count; ++frame_index) {
        const std::vector<MeshBvhNode> frame_nodes = compute_frame_nodes(motion_mesh, default_bvh, frame_index);
        const FrameMetrics metrics = compute_frame_metrics(default_bvh, baseline_nodes, frame_nodes, frame_index);

        if (metrics.node_surface_sum_ratio > report.worst_node_surface_sum.node_surface_sum_ratio) {
            report.worst_node_surface_sum = metrics;
        }
        if (metrics.leaf_surface_sum_ratio > report.worst_leaf_surface_sum.leaf_surface_sum_ratio) {
            report.worst_leaf_surface_sum = metrics;
        }
        if (metrics.max_leaf_surface_ratio > report.worst_leaf_surface.max_leaf_surface_ratio) {
            report.worst_leaf_surface = metrics;
        }
        if (metrics.max_leaf_extent_ratio > report.worst_leaf_extent.max_leaf_extent_ratio) {
            report.worst_leaf_extent = metrics;
        }
        if (metrics.root_surface_ratio > report.worst_root_surface.root_surface_ratio) {
            report.worst_root_surface = metrics;
        }
        if (metrics.max_sibling_overlap_fraction > report.worst_sibling_overlap.max_sibling_overlap_fraction) {
            report.worst_sibling_overlap = metrics;
        }
    }

    return report;
}

void print_metric(const char* label, double value, std::uint32_t frame_index)
{
    std::cout << "  " << std::left << std::setw(31) << label
              << std::right << std::fixed << std::setprecision(3) << value
              << "  frame=" << frame_index << '\n';
}

void print_report_metrics(const char* title, const MotionReport& report)
{
    std::cout << "  " << title << '\n';
    print_metric("node surface sum ratio", report.worst_node_surface_sum.node_surface_sum_ratio,
                 report.worst_node_surface_sum.frame_index);
    print_metric("leaf surface sum ratio", report.worst_leaf_surface_sum.leaf_surface_sum_ratio,
                 report.worst_leaf_surface_sum.frame_index);
    print_metric("max leaf surface ratio", report.worst_leaf_surface.max_leaf_surface_ratio,
                 report.worst_leaf_surface.frame_index);
    print_metric("max leaf extent ratio", report.worst_leaf_extent.max_leaf_extent_ratio,
                 report.worst_leaf_extent.frame_index);
    print_metric("max sibling overlap fraction", report.worst_sibling_overlap.max_sibling_overlap_fraction,
                 report.worst_sibling_overlap.frame_index);
}

void print_improvement_metrics(const MotionReport& spatial_report, const MotionReport& labeled_report)
{
    std::cout << "  improvement\n";
    print_metric("node surface sum ratio",
                 safe_ratio(spatial_report.worst_node_surface_sum.node_surface_sum_ratio,
                            labeled_report.worst_node_surface_sum.node_surface_sum_ratio),
                 labeled_report.worst_node_surface_sum.frame_index);
    print_metric("leaf surface sum ratio",
                 safe_ratio(spatial_report.worst_leaf_surface_sum.leaf_surface_sum_ratio,
                            labeled_report.worst_leaf_surface_sum.leaf_surface_sum_ratio),
                 labeled_report.worst_leaf_surface_sum.frame_index);
    print_metric("max leaf surface ratio",
                 safe_ratio(spatial_report.worst_leaf_surface.max_leaf_surface_ratio,
                            labeled_report.worst_leaf_surface.max_leaf_surface_ratio),
                 labeled_report.worst_leaf_surface.frame_index);
    print_metric("max leaf extent ratio",
                 safe_ratio(spatial_report.worst_leaf_extent.max_leaf_extent_ratio,
                            labeled_report.worst_leaf_extent.max_leaf_extent_ratio),
                 labeled_report.worst_leaf_extent.frame_index);
    print_metric("max sibling overlap fraction",
                 safe_ratio(spatial_report.worst_sibling_overlap.max_sibling_overlap_fraction,
                            labeled_report.worst_sibling_overlap.max_sibling_overlap_fraction),
                 labeled_report.worst_sibling_overlap.frame_index);
}

void print_motion_comparison(const MotionReport& spatial_report, const MotionReport& labeled_report)
{
    if (spatial_report.frame_count == 0u || labeled_report.frame_count == 0u) {
        return;
    }

    std::cout << "\nMotion: " << spatial_report.path << '\n';
    std::cout << "  frames=" << spatial_report.frame_count
              << " vertices=" << spatial_report.vertex_count
              << " triangles=" << spatial_report.triangle_count << '\n';
    print_report_metrics("spatial-only", spatial_report);
    print_report_metrics("part-labeled", labeled_report);
    print_improvement_metrics(spatial_report, labeled_report);
}

std::vector<std::filesystem::path> default_motion_paths(const ProjectPaths& project_paths)
{
    std::vector<std::filesystem::path> paths = asset_io::scan_motion_asset_paths(project_paths);
    paths.erase(std::remove(paths.begin(), paths.end(), project_paths.default_character_motion_path), paths.end());
    if (paths.size() > 3u) {
        paths.resize(3u);
    }
    return paths;
}
}

int main(int argc, char** argv)
{
    const std::filesystem::path project_root =
#ifdef PROJECT_ROOT_DIR
        std::filesystem::path{PROJECT_ROOT_DIR};
#else
        std::filesystem::current_path().parent_path();
#endif
    const ProjectPaths project_paths = make_project_paths(project_root);

    std::vector<std::filesystem::path> motion_paths;
    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        motion_paths.emplace_back(argv[arg_index]);
    }
    if (motion_paths.empty()) {
        motion_paths = default_motion_paths(project_paths);
    }

    CharacterMesh default_mesh;
    std::vector<std::uint8_t> default_triangle_part_labels;
    if (!asset_io::read_default_character_mesh_asset(project_paths.default_character_motion_path,
                                                     default_mesh,
                                                     default_triangle_part_labels)) {
        return 1;
    }

    const std::uint32_t source_triangle_count = default_mesh.index_count / triangle_vertex_count;
    MeshBvhBuilder spatial_builder(default_mesh.vertex_count, default_mesh.indices, default_mesh.vertices);
    MeshBvhData spatial_bvh = spatial_builder.build_mesh_bvh();
    if (!spatial_bvh.is_valid(source_triangle_count)) {
        std::cerr << "Failed to build spatial-only default character BVH.\n";
        return 1;
    }

    MeshBvhBuilder labeled_builder(default_mesh.vertex_count,
                                   default_mesh.indices,
                                   default_mesh.vertices,
                                   default_triangle_part_labels);
    MeshBvhData labeled_bvh = labeled_builder.build_mesh_bvh();
    if (!labeled_bvh.is_valid(source_triangle_count)) {
        std::cerr << "Failed to build part-labeled default character BVH.\n";
        return 1;
    }

    const std::vector<MeshBvhNode> spatial_baseline_nodes = compute_frame_nodes(default_mesh, spatial_bvh, 0u);
    const std::vector<MeshBvhNode> labeled_baseline_nodes = compute_frame_nodes(default_mesh, labeled_bvh, 0u);
    std::cout << "Default BVH: " << project_paths.default_character_motion_path << '\n';
    std::cout << "  spatial_nodes=" << spatial_bvh.nodes.size()
              << " labeled_nodes=" << labeled_bvh.nodes.size()
              << " triangles=" << source_triangle_count
              << " labels=" << default_triangle_part_labels.size()
              << " tested_motions=" << motion_paths.size() << '\n';

    for (const std::filesystem::path& motion_path : motion_paths) {
        CharacterMesh motion_mesh;
        if (!asset_io::read_character_mesh_asset(motion_path, motion_mesh)) {
            continue;
        }

        const MotionReport spatial_report =
            analyze_motion_mesh(motion_path, default_mesh, motion_mesh, spatial_bvh, spatial_baseline_nodes);
        const MotionReport labeled_report =
            analyze_motion_mesh(motion_path, default_mesh, motion_mesh, labeled_bvh, labeled_baseline_nodes);
        print_motion_comparison(spatial_report, labeled_report);
    }

    return 0;
}
