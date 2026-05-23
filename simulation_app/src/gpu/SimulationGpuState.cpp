#include "gpu/SimulationGpuState.h"

#include "simulation/SimulationScene.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>

namespace {
constexpr std::uint64_t unuploaded_revision = std::numeric_limits<std::uint64_t>::max();
constexpr GLuint character_animation_position_binding = 0;
}

bool SimulationGpuState::is_initialized() const
{
    return viewer_shader_.is_initialized();
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

void SimulationGpuState::set_character_mesh(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!scene.has_character()) {
        return;
    }

    const std::uint64_t scene_revision = scene.character_revision();
    const std::uint32_t scene_frame = scene.current_character_frame();

    // 새 캐릭터 mesh가 들어옴 -> 전체 frame 캐릭터 mesh를 GPU에 올리고 frame 상태 설정
    const CharacterMesh& character_mesh = scene.character_mesh();
    character_gpu_state_.upload_mesh(character_mesh, gl);
    character_gpu_state_.set_current_frame(scene_frame);
    character_revision_ = scene_revision;
    current_character_frame_ = scene_frame;
    character_uploaded_ = true;
}

void SimulationGpuState::set_garment_mesh(const GarmentSceneObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    GarmentGpuSlot* slot = find_garment_gpu_slot(garment.id);
    if (slot == nullptr) {
        std::cerr << "Missing garment GPU state for garment id " << garment.id << ".\n";
        return;
    }

    if (slot->uploaded_revision == garment.revision) {
        return;
    }

    slot->gpu_state.upload(garment.mesh, gl);
    slot->gpu_state.reset_states(garment.mesh, gl);
    slot->uploaded_revision = garment.revision;
}

// 새로운 garment GPU state 추가
void SimulationGpuState::add_garment_gpu_state(GarmentId garment_id)
{
    if (find_garment_gpu_slot(garment_id) != nullptr) {
        return;
    }

    GarmentGpuSlot& new_slot = garment_gpu_slots_.emplace_back();
    new_slot.id = garment_id;
    new_slot.uploaded_revision = unuploaded_revision;
}

void SimulationGpuState::remove_garment_gpu_state(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl)
{
}

// id에 해당하는 garment의 위치 찾기
SimulationGpuState::GarmentGpuSlot* SimulationGpuState::find_garment_gpu_slot(GarmentId garment_id)
{
    const auto iter = std::find_if(garment_gpu_slots_.begin(), garment_gpu_slots_.end(),
        [garment_id](const GarmentGpuSlot& slot) {
            return slot.id == garment_id;
        }
    );
    if (iter == garment_gpu_slots_.end()) {
        return nullptr;
    }

    return &(*iter);
}

void SimulationGpuState::sync(const SimulationScene& scene, QOpenGLFunctions_4_5_Core&)
{
    if (!is_initialized()) {
        return;
    }

    update_character_frame(scene);
}

void SimulationGpuState::update_character_frame(const SimulationScene& scene)
{
    if (!scene.has_character() || !character_uploaded_) {
        return;
    }

    const std::uint32_t scene_frame = scene.current_character_frame();

    // 캐릭터 frame 상태 갱신
    if (current_character_frame_ != scene_frame) {
        character_gpu_state_.set_current_frame(scene_frame);
        current_character_frame_ = scene_frame;
    }
}

void SimulationGpuState::draw(const SimulationScene& scene, const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized()) {
        return;
    }

    // shader setting
    viewer_shader_.bind(gl);
    viewer_shader_.set_mvp(mvp, gl);
    viewer_shader_.set_attribute_position_mode(gl);

    // grid
    if (grid_gpu_state_.initialized()) {
        viewer_shader_.set_solid_color(grid_gpu_state_.color(), gl);

        gl.glDepthMask(GL_FALSE);
        grid_gpu_state_.draw(gl);
        gl.glDepthMask(GL_TRUE);
    }

    // character
    if (scene.has_character() && character_gpu_state_.is_initialized()) {
        character_gpu_state_.bind_animation_positions(character_animation_position_binding, gl);
        viewer_shader_.set_character_animation_mode(character_gpu_state_.current_frame_index(), character_gpu_state_.vertex_count(), gl);
        viewer_shader_.set_vertex_color_mode(gl);
        character_gpu_state_.draw(gl);
    }

    // garments
    viewer_shader_.set_attribute_position_mode(gl);
    const std::vector<GarmentSceneObject>& garments = scene.garments();
    for (const GarmentSceneObject& garment : garments) {
        const GarmentGpuSlot* slot = find_garment_gpu_slot(garment.id);
        if (slot == nullptr || !garment.visible || !slot->gpu_state.is_initialized()) {
            continue;
        }

        viewer_shader_.set_solid_color(garment.mesh.color, gl);
        slot->gpu_state.draw(gl);
    }
}

void SimulationGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    for (GarmentGpuSlot& slot : garment_gpu_slots_) {
        slot.gpu_state.release(gl);
    }
    garment_gpu_slots_.clear();

    character_gpu_state_.release(gl);
    grid_gpu_state_.release(gl);
    viewer_shader_.release(gl);

    character_revision_ = 0;
    current_character_frame_ = 0;
    character_uploaded_ = false;
}
