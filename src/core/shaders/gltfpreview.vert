#version 330 core

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord0;
layout (location = 3) in vec2 inTexCoord1;

uniform mat4 uMvp;
uniform mat4 uModelView;
uniform mat3 uNormalMatrix;

out vec3 vNormal;
out vec3 vViewDir;
out vec2 vTexCoord0;
out vec2 vTexCoord1;

void main() {
    vec4 viewPos = uModelView * vec4(inPosition, 1.0);
    vNormal = normalize(uNormalMatrix * inNormal);
    vViewDir = normalize(-viewPos.xyz);
    vTexCoord0 = inTexCoord0;
    vTexCoord1 = inTexCoord1;
    gl_Position = uMvp * vec4(inPosition, 1.0);
}
