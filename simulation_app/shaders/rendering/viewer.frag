#version 430 core

in vec3 vertexColor;
in vec3 vertexNormalWorld;
out vec4 fragColor;

uniform bool uUseNormalLighting;
uniform float uOpacity;
uniform vec3 uLightDirectionWorld;
uniform vec3 uFillLightDirectionWorld;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uFillDiffuseStrength;

void main()
{
    vec3 color = vertexColor;
    if (uUseNormalLighting) {
        float normalLengthSquared = dot(vertexNormalWorld, vertexNormalWorld);
        vec3 normal = normalLengthSquared > 1.0e-20
            ? vertexNormalWorld * inversesqrt(normalLengthSquared)
            : vec3(0.0, 1.0, 0.0);
        float diffuse = max(dot(normal, uLightDirectionWorld), 0.0);
        float fillDiffuse = max(dot(normal, uFillLightDirectionWorld), 0.0);
        color *= uAmbientStrength + uDiffuseStrength * diffuse + uFillDiffuseStrength * fillDiffuse;
    }

    fragColor = vec4(color, uOpacity);
}
