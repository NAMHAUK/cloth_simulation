#pragma once

#include <cstdint>
#include <filesystem>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SceneRenderShader final {
public:
    SceneRenderShader() = default;
    SceneRenderShader(const SceneRenderShader&) = delete;
    SceneRenderShader& operator=(const SceneRenderShader&) = delete;

    bool is_initialized() const;

    bool load(const std::filesystem::path& vertex_shader_path,
              const std::filesystem::path& fragment_shader_path,
              QOpenGLFunctions_4_5_Core& gl);
    void bind(QOpenGLFunctions_4_5_Core& gl) const;
    void set_mvp(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) const;
    void set_attribute_position_mode(QOpenGLFunctions_4_5_Core& gl) const;
    void set_character_animation_mode(std::uint32_t frame_index,
                                      std::uint32_t vertex_count,
                                      QOpenGLFunctions_4_5_Core& gl) const;
    void set_solid_color(const glm::vec3& color, QOpenGLFunctions_4_5_Core& gl) const;
    void set_vertex_color_mode(QOpenGLFunctions_4_5_Core& gl) const;
    void set_lighting(const glm::vec3& light_direction_world,
                      float ambient_strength,
                      float diffuse_strength,
                      QOpenGLFunctions_4_5_Core& gl) const;
    void set_normal_lighting_enabled(bool enabled, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint compile_shader(GLenum type, const char* source, QOpenGLFunctions_4_5_Core& gl);

    GLuint program_ = 0;
    GLint mvp_location_ = -1;
    GLint solid_mode_location_ = -1;
    GLint solid_color_location_ = -1;
    GLint animation_mode_location_ = -1;
    GLint animation_frame_index_location_ = -1;
    GLint animation_vertex_count_location_ = -1;
    GLint normal_lighting_mode_location_ = -1;
    GLint light_direction_location_ = -1;
    GLint ambient_strength_location_ = -1;
    GLint diffuse_strength_location_ = -1;
};
