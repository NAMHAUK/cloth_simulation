#version 330 core

layout (location = 0) in vec3 aPosition;

out vec3 vertexColor;

uniform mat4 uMVP;
uniform bool uUseSolidColor;
uniform vec3 uSolidColor;

void main()
{
    float heightTint = clamp(aPosition.y * 0.45 + 0.55, 0.0, 1.0);
    vec3 heightColor = mix(vec3(0.22, 0.42, 0.72), vec3(0.80, 0.86, 0.92), heightTint);
    vertexColor = uUseSolidColor ? uSolidColor : heightColor;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
