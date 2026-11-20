#include "Window.h"
#include "../Engine/Paths/RuntimePaths.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#endif

#include <iostream>
#include <filesystem>
#include "../ThirdParty/stb/stb_image.h"

// Globais estáticas para callbacks
static Window* g_CurrentWindow = nullptr;
extern void* g_CurrentApplication; // Definido em Application.cpp

// Callback de redimensionamento
static void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    if (g_CurrentWindow && window == g_CurrentWindow->GetNativeWindow())
    {
        if (g_CurrentWindow->GetGraphicsAPI() == GraphicsAPI::OpenGL)
            glViewport(0, 0, width, height);
        g_CurrentWindow->OnResize(width, height);
    }
}

Window::Window(int width, int height, const char* title, GraphicsAPI graphicsAPI,
               bool nativeDecorations, bool startMaximized, bool fullscreen)
    : m_Width(width), m_Height(height), m_Window(nullptr),
      m_GLContextWindow(nullptr), m_GraphicsAPI(graphicsAPI)
{
    glfwDefaultWindowHints();
    if (m_GraphicsAPI == GraphicsAPI::Vulkan)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    else
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    // O topo é desenhado pelo ImGui para manter logo, menus e controles na
    // mesma linha, como no Visual Studio.
    glfwWindowHint(GLFW_DECORATED,  nativeDecorations ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, startMaximized && !fullscreen ? GLFW_TRUE : GLFW_FALSE);

    GLFWmonitor* monitor = fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    if (fullscreen && monitor)
    {
        if (const GLFWvidmode* mode = glfwGetVideoMode(monitor))
        {
            width = mode->width;
            height = mode->height;
        }
    }
    m_Window = glfwCreateWindow(width, height, title, monitor, nullptr);
    if (!m_Window)
    {
        std::cout << "Falha ao criar janela GLFW!" << std::endl;
        return;
    }

    // Ícone da barra de tarefas
    {
        namespace fs = std::filesystem;
        const fs::path iconPath = RuntimePaths::ResolveEngineAsset(
            __FILE__, "Editor/Branding/MaraeLogoTransparent.png");
        int iconW = 0, iconH = 0, iconChannels = 0;
        unsigned char* iconPixels = stbi_load(iconPath.string().c_str(),
                                               &iconW, &iconH, &iconChannels, STBI_rgb_alpha);
        if (iconPixels && iconW > 0 && iconH > 0)
        {
            const size_t n = static_cast<size_t>(iconW) * static_cast<size_t>(iconH);
            for (size_t i = 0; i < n; ++i)
            {
                unsigned char* px = iconPixels + i * 4;
                if (px[3] == 0 || (px[0] > 220 && px[1] > 220 && px[2] > 220))
                { px[0] = 0; px[1] = 0; px[2] = 0; px[3] = 255; }
            }
            GLFWimage icon{ iconW, iconH, iconPixels };
            glfwSetWindowIcon(m_Window, 1, &icon);
            stbi_image_free(iconPixels);
        }
    }

    g_CurrentWindow = this;
    if (m_GraphicsAPI == GraphicsAPI::Vulkan)
    {
        // Legacy scene/assets still use OpenGL. Keep a hidden worker context for
        // those systems while the visible no-API window is presented by Vulkan.
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        m_GLContextWindow = glfwCreateWindow(16, 16, "Marae OpenGL compatibility context", nullptr, nullptr);
        if (!m_GLContextWindow)
        {
            std::cout << "Falha ao criar o contexto auxiliar OpenGL para os sistemas legados." << std::endl;
            return;
        }
        glfwMakeContextCurrent(m_GLContextWindow);
        glfwSwapInterval(0);
    }
    else
    {
        glfwMakeContextCurrent(m_Window);
        glfwSwapInterval(1);
    }

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Falha ao carregar GLAD!" << std::endl;
        return;
    }

    if (!fullscreen)
    {
        if (startMaximized)
        {
            if (glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) != GLFW_TRUE)
                glfwMaximizeWindow(m_Window);
        }
        else
        {
            // Alguns ambientes Windows restauram o estado maximizado anterior
            // após criar a janela. Runtime windowed/borderless deve respeitar
            // a resolução explícita do manifesto, sem herdar o estado do editor.
            if (glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) == GLFW_TRUE)
                glfwRestoreWindow(m_Window);
            glfwSetWindowSize(m_Window, width, height);
        }
    }

    int fbW, fbH;
    glfwGetFramebufferSize(m_Window, &fbW, &fbH);
    m_Width  = fbW;
    m_Height = fbH;

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, m_Width, m_Height);
    glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);

    std::cout << (m_GraphicsAPI == GraphicsAPI::Vulkan ? "Vulkan selecionado (UI); OpenGL auxiliar" : "OpenGL carregado")
              << ": " << m_Width << "x" << m_Height << std::endl;
}

Window::~Window()
{
    if (m_GLContextWindow)
    {
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(m_GLContextWindow);
        m_GLContextWindow = nullptr;
    }
    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }
    g_CurrentWindow = nullptr;
}

bool Window::ShouldClose() const
{
    return m_Window ? glfwWindowShouldClose(m_Window) : true;
}

void Window::PollEvents()
{
    glfwPollEvents();
}

void Window::Clear()
{
    GLFWwindow* glWindow = m_GLContextWindow ? m_GLContextWindow : m_Window;
    if (!glWindow || m_GraphicsAPI == GraphicsAPI::Vulkan)
    {
        if (!glWindow)
            return;
    }
    glfwMakeContextCurrent(glWindow);
    glClearColor(0.1f, 0.15f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Window::Present()
{
    if (m_Window && m_GraphicsAPI == GraphicsAPI::OpenGL)
        glfwSwapBuffers(m_Window);
}

bool Window::IsKeyPressed(int key) const
{
    return m_Window ? glfwGetKey(m_Window, key) == GLFW_PRESS : false;
}

int Window::GetWidth()  const { return m_Width;  }
int Window::GetHeight() const { return m_Height; }

void Window::OnResize(int width, int height)
{
    m_Width  = width;
    m_Height = height;
}

GLFWwindow* Window::GetNativeWindow() const { return m_Window; }

void Window::RequestClose()
{
    if (m_Window) glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
}

void Window::Minimize()
{
    if (m_Window) glfwIconifyWindow(m_Window);
}

void Window::ToggleMaximize()
{
    if (!m_Window) return;
    if (glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) == GLFW_TRUE)
        glfwRestoreWindow(m_Window);
    else
        glfwMaximizeWindow(m_Window);
}

bool Window::IsWindowMaximized() const
{
    return m_Window && glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) == GLFW_TRUE;
}

void Window::BeginWindowDrag()
{
#ifdef _WIN32
    if (m_Window)
    {
        HWND hwnd = glfwGetWin32Window(m_Window);
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
#endif
}

double Window::GetMouseX() const
{
    if (!m_Window) return 0.0;
    double x, y;
    glfwGetCursorPos(m_Window, &x, &y);
    return x;
}

double Window::GetMouseY() const
{
    if (!m_Window) return 0.0;
    double x, y;
    glfwGetCursorPos(m_Window, &x, &y);
    return y;
}
