#version 450

// Vertex shader para shadow mapping Vulkan
// Transforma vértices do espaço de objeto para espaço de luz

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

layout(binding = 0) uniform ShadowUniforms {
    mat4 u_LightSpaceMatrix;
    mat4 u_ModelMatrix;
};

void main() {
    gl_Position = u_LightSpaceMatrix * u_ModelMatrix * vec4(a_Position, 1.0);
}
