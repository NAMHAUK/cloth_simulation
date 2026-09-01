#version 430 core

#include "../simulation_buffer_bindings.glsl"

layout (location = 0) in vec3 aPosition;

layout(std430, binding = CHARACTER_CURRENT_POSITIONS) readonly buffer CharacterCurrentPositions {
    vec4 character_current_positions[];
};

layout(std430, binding = CLOTH_VERTEX_NORMALS) readonly buffer ClothVertexNormals {
    vec4 cloth_vertex_normals[];
};

layout(std430, binding = CHARACTER_VERTEX_NORMALS) readonly buffer CharacterVertexNormals {
    vec4 character_vertex_normals[];
};

out vec3 vertexColor;
out vec3 vertexNormalWorld;

uniform mat4 uMVP;
uniform bool uUseSolidColor;
uniform vec3 uSolidColor;
uniform bool uUsePositionBuffer;
uniform bool uUseNormalLighting;

void main()
{
    vec3 position = aPosition;
    if (uUsePositionBuffer) {
        position = character_current_positions[gl_VertexID].xyz;
    }

    float heightTint = clamp(position.y * 0.45 + 0.55, 0.0, 1.0);
    vec3 heightColor = mix(vec3(0.22, 0.42, 0.72), vec3(0.80, 0.86, 0.92), heightTint);
    vertexColor = uUseSolidColor ? uSolidColor : heightColor;
    vertexNormalWorld = uUseNormalLighting
        ? (uUsePositionBuffer ? character_vertex_normals[gl_VertexID].xyz
                              : cloth_vertex_normals[gl_VertexID].xyz)
        : vec3(0.0, 1.0, 0.0);
    gl_Position = uMVP * vec4(position, 1.0);
}
