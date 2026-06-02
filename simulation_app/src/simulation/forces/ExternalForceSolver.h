#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>
#include <glm/vec3.hpp>

struct ClothPositionBufferView;

class ExternalForceSolver final {
public:
    ExternalForceSolver() = default;
    ExternalForceSolver(const ExternalForceSolver&) = delete;
    ExternalForceSolver& operator=(const ExternalForceSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void solve(const ClothPositionBufferView& position_view, float dt, const glm::vec3& external_acceleration, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_count_location_ = -1;
    GLint acc_displacement_location_ = -1;
};
