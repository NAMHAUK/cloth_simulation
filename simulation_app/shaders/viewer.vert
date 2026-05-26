#version 430 core

layout (location = 0) in vec3 aPosition;

layout(std430, binding = 0) readonly buffer CharacterAnimationPositions {
    float positions[];
};

layout(std430, binding = 1) readonly buffer VertexNormals {
    vec4 vertex_normals[];
};

out vec3 vertexColor;
out vec3 vertexNormalWorld;

uniform mat4 uMVP;
uniform bool uUseSolidColor;
uniform vec3 uSolidColor;
uniform bool uUseAnimationBuffer;
uniform uint uAnimationFrameIndex;
uniform uint uAnimationVertexCount;
uniform bool uUseNormalLighting;

void main()
{
    vec3 position = aPosition;
    if (uUseAnimationBuffer) {
        uint base = (uAnimationFrameIndex * uAnimationVertexCount + uint(gl_VertexID)) * 3u;
        position = vec3(positions[base], positions[base + 1u], positions[base + 2u]);
    }

    float heightTint = clamp(position.y * 0.45 + 0.55, 0.0, 1.0);
    vec3 heightColor = mix(vec3(0.22, 0.42, 0.72), vec3(0.80, 0.86, 0.92), heightTint);
    vertexColor = uUseSolidColor ? uSolidColor : heightColor;
    vertexNormalWorld = uUseNormalLighting ? vertex_normals[gl_VertexID].xyz : vec3(0.0, 1.0, 0.0);
    gl_Position = uMVP * vec4(position, 1.0);
}
