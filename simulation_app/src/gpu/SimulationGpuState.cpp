#include "gpu/SimulationGpuState.h"

#include "simulation/SimulationScene.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>

#include <glm/vec3.hpp>

namespace {
constexpr glm::vec3 grid_color{0.25f, 0.29f, 0.34f};
constexpr std::uint64_t unuploaded_revision = std::numeric_limits<std::uint64_t>::max();
}

bool SimulationGpuState::is_initialized() const
{
    return viewer_shader_.initialized();
}

bool SimulationGpuState::initialize(const std::filesystem::path& vertex_shader_path,
                                    const std::filesystem::path& fragment_shader_path,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    if (!viewer_shader_.load(vertex_shader_path, fragment_shader_path, gl)) {
        std::cerr << "Failed to create viewer shader program.\n";
        return false;
    }

    grid_gpu_state_.upload(gl);
    return true;
}

void SimulationGpuState::sync(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized()) {
        return;
    }

    sync_character(scene, gl);
    sync_garments(scene, gl);
}

void SimulationGpuState::sync_character(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!scene.has_character()) {
        character_uploaded_ = false;
        uploaded_character_revision_ = 0;
        uploaded_character_frame_ = 0;
        return;
    }

    const CharacterMesh& character_mesh = scene.character_mesh();
    const std::uint64_t scene_revision = scene.character_revision();
    const std::uint32_t scene_frame = scene.current_character_frame();

    if (!character_uploaded_ || uploaded_character_revision_ != scene_revision) {
        character_gpu_state_.upload_mesh(character_mesh, gl);
        character_gpu_state_.upload_frame(character_mesh, scene_frame, gl);
        uploaded_character_revision_ = scene_revision;
        uploaded_character_frame_ = scene_frame;
        character_uploaded_ = true;
        return;
    }

    if (uploaded_character_frame_ != scene_frame) {
        character_gpu_state_.upload_frame(character_mesh, scene_frame, gl);
        uploaded_character_frame_ = scene_frame;
    }
}

void SimulationGpuState::sync_garments(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl)
{
    const std::vector<GarmentSceneObject>& garments = scene.garments();

    for (std::size_t index = garments.size(); index < garment_gpu_states_.size(); ++index) {
        garment_gpu_states_[index].release(gl);
    }

    garment_gpu_states_.resize(garments.size());
    uploaded_garment_revisions_.resize(garments.size(), unuploaded_revision);

    for (std::size_t index = 0; index < garments.size(); ++index) {
        const GarmentSceneObject& garment = garments[index];
        if (uploaded_garment_revisions_[index] == garment.revision) {
            continue;
        }

        garment_gpu_states_[index].upload(garment.mesh, gl);
        garment_gpu_states_[index].reset_states(garment.mesh, gl);
        uploaded_garment_revisions_[index] = garment.revision;
    }
}

void SimulationGpuState::draw(const SimulationScene& scene, const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized()) {
        return;
    }

    viewer_shader_.bind(gl);
    viewer_shader_.set_mvp(mvp, gl);

    if (grid_gpu_state_.initialized()) {
        viewer_shader_.set_solid_color(grid_color, gl);
        gl.glDepthMask(GL_FALSE);
        grid_gpu_state_.draw(gl);
        gl.glDepthMask(GL_TRUE);
    }

    if (scene.has_character() && character_gpu_state_.is_initialized()) {
        viewer_shader_.set_vertex_color_mode(gl);
        character_gpu_state_.draw(gl);
    }

    const std::vector<GarmentSceneObject>& garments = scene.garments();
    const std::size_t garment_count = std::min(garments.size(), garment_gpu_states_.size());
    for (std::size_t index = 0; index < garment_count; ++index) {
        const GarmentSceneObject& garment = garments[index];
        const ClothGpuState& gpu_state = garment_gpu_states_[index];
        if (!garment.visible || !gpu_state.is_initialized()) {
            continue;
        }

        viewer_shader_.set_solid_color(garment.mesh.color, gl);
        gpu_state.draw(gl);
    }
}

void SimulationGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    for (ClothGpuState& garment_gpu_state : garment_gpu_states_) {
        garment_gpu_state.release(gl);
    }
    garment_gpu_states_.clear();
    uploaded_garment_revisions_.clear();

    character_gpu_state_.release(gl);
    grid_gpu_state_.release(gl);
    viewer_shader_.release(gl);

    uploaded_character_revision_ = 0;
    uploaded_character_frame_ = 0;
    character_uploaded_ = false;
}
