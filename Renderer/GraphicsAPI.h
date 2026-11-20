#pragma once

// Graphics API used for the editor's visible presentation surface.
// Vulkan is currently experimental and renders Dear ImGui editor UI only;
// the scene renderer remains OpenGL until its passes are ported.
enum class GraphicsAPI : int
{
    OpenGL = 0,
    Vulkan = 1
};

// User preference is kept separate from the backend currently presenting the
// editor. Vulkan currently presents ImGui while scene passes remain OpenGL.
enum class RendererMode : int
{
    Automatic = 0,
    OpenGLOnly = 1,
    Hybrid = 2,
    VulkanOnly = 3
};
