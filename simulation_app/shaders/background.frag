#version 430 core

in vec2 gradientUv;
out vec4 fragColor;

uniform vec3 uTopColor;
uniform vec3 uBottomColor;

void main()
{
    float tint = clamp(gradientUv.y, 0.0, 1.0);
    fragColor = vec4(mix(uBottomColor, uTopColor, tint), 1.0);
}
