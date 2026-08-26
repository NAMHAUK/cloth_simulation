#pragma once

#include <glm/glm.hpp>

#include <QOpenGLFunctions_4_5_Core>

struct BvhBufferView final
{
    GLuint node_buffer = 0;
    GLuint bounds_buffer = 0;
    glm::uvec4 arm_triangle_ranges{};
};
