#version 430 core

out vec2 gradientUv;

const vec2 fullscreenTrianglePositions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));

void main()
{
    vec2 position = fullscreenTrianglePositions[gl_VertexID];
    gradientUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
