#include "rendering/BackgroundGradient.h"

#include "utils/ShaderUtils.h"

#include <cassert>

void BackgroundGradient::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const auto vertex_shader_path = shader_dir / "rendering" / "background.vert";
    const auto fragment_shader_path = shader_dir / "rendering" / "background.frag";
    program_ = load_render_program(vertex_shader_path, fragment_shader_path, gl);

    const GLint top_color_location = require_uniform_location(program_, "uTopColor", gl);
    const GLint bottom_color_location = require_uniform_location(program_, "uBottomColor", gl);
    gl.glProgramUniform3f(program_, top_color_location, top_color_.r, top_color_.g, top_color_.b);
    gl.glProgramUniform3f(program_, bottom_color_location, bottom_color_.r, bottom_color_.g, bottom_color_.b);

    gl.glCreateVertexArrays(1, &vao_);
}

void BackgroundGradient::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    assert(program_ != 0 && vao_ != 0);

    gl.glUseProgram(program_);
    gl.glBindVertexArray(vao_);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
}

void BackgroundGradient::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteVertexArrays(1, &vao_);
    gl.glDeleteProgram(program_);

    program_ = 0;
    vao_ = 0;
}
