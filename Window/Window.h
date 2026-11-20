#pragma once

#include "../Renderer/GraphicsAPI.h"

struct GLFWwindow;

class Window
{
public:
    Window(int width, int height, const char* title, GraphicsAPI graphicsAPI = GraphicsAPI::OpenGL,
           bool nativeDecorations = false, bool startMaximized = true, bool fullscreen = false);
    ~Window();

    bool ShouldClose() const;
    void PollEvents();

    void Clear();
    void Present();

    bool IsKeyPressed(int key) const;

    int GetWidth() const;
    int GetHeight() const;
    double GetMouseX() const;
    double GetMouseY() const;

    GLFWwindow* GetNativeWindow() const;
    GraphicsAPI GetGraphicsAPI() const { return m_GraphicsAPI; }

    void RequestClose();
    void Minimize();
    void ToggleMaximize();
    bool IsWindowMaximized() const;
    void BeginWindowDrag();

    // Chamado pelo FramebufferSizeCallback para manter m_Width/m_Height atualizados
    void OnResize(int width, int height);

private:
    GLFWwindow* m_Window;
    GLFWwindow* m_GLContextWindow;
    GraphicsAPI m_GraphicsAPI;
    int m_Width;
    int m_Height;
};
