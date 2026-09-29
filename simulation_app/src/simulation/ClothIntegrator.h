#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#include <glm/vec3.hpp>

struct ClothIntegrationParams;
struct ReferenceFrameKinematics;
struct IntegrationCoefficients;
class ClothGpuState;
class SceneState;

class ClothIntegrator final
{
public:
    explicit ClothIntegrator(const ClothIntegrationParams& params);
    ClothIntegrator(const ClothIntegrator&) = delete;
    ClothIntegrator& operator=(const ClothIntegrator&) = delete;

    void initialize(const std::filesystem::path& shader_dir, float dt, QOpenGLFunctions_4_5_Core& gl);
    void integrate(const ClothGpuState& cloth_state,
                   const SceneState& scene,
                   QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    IntegrationCoefficients integration_coefficients(const ReferenceFrameKinematics& frame) const;

    GLuint program_ = 0;
    GLint vertex_offset_loc_ = -1;
    GLint vertex_count_loc_ = -1;

    GLint displacement_coefficient_loc_ = -1;
    GLint position_coefficient_loc_ = -1;
    GLint position_offset_loc_ = -1;

    float dt_ = 0.0f;
    glm::vec3 gravity_{0.0f};
    float damping_ = 0.0f;
    float frame_inertia_scale_ = 0.0f;
};
