#include "CameraController.h"

#include "../Window/Window.h"
#include "../Scene/Camera/Camera.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

float CameraController::s_ScrollOffset = 0.0f;
static GLFWscrollfun s_PreviousScrollCallback = nullptr;

static void ScrollCallback(
    GLFWwindow* window,
    double xoffset,
    double yoffset)
{
    CameraController::s_ScrollOffset += static_cast<float>(yoffset);

    // ImGui instala seu callback de scroll durante a inicialização. Preserve-o
    // para que os painéis continuem rolando mesmo com o dolly do viewport ativo.
    if (s_PreviousScrollCallback && s_PreviousScrollCallback != ScrollCallback)
        s_PreviousScrollCallback(window, xoffset, yoffset);
}

void CameraController::Reset(Window& window)
{
    if (m_IsActive)
    {
        if (GLFWwindow* nativeWindow = window.GetNativeWindow())
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    m_IsActive = false;
    m_FirstMouse = true;
    m_FlySpeedMultiplier = 1.0f;
    s_ScrollOffset = 0.0f;
}

void CameraController::Update(
    Window& window,
    Camera& camera,
    bool canStartControl,
    bool viewportHovered
)
{
    GLFWwindow* nativeWindow = window.GetNativeWindow();
    if (!nativeWindow)
        return;

    const double now = glfwGetTime();
    const float deltaTime = m_LastUpdateTime > 0.0
        ? std::clamp(static_cast<float>(now - m_LastUpdateTime), 0.0f, 0.1f)
        : (1.0f / 60.0f);
    m_LastUpdateTime = now;

    if (!m_ScrollCallbackRegistered)
    {
        s_PreviousScrollCallback = glfwSetScrollCallback(nativeWindow, ScrollCallback);
        m_ScrollCallbackRegistered = true;
        s_ScrollOffset = 0.0f;
    }

    // NOTA: Não usamos ImGui::WantCaptureMouse / WantCaptureKeyboard aqui,
    // pois CameraController::Update() é chamado ANTES de ImGui::NewFrame()
    // (que é quem recalcula essas flags para o frame atual).
    // Em vez disso, nos baseamos em:
    //   - m_IsActive (controle de câmera já iniciado no frame anterior)
    //   - canStartControl / viewportFocused / viewportHovered (flags exatas
    //     do frame anterior, calculadas quando o ViewportPanel foi desenhado
    //     no ImGui::Render() anterior)

    const bool rightMousePressed = glfwGetMouseButton(
        nativeWindow,
        GLFW_MOUSE_BUTTON_RIGHT
    ) == GLFW_PRESS;

    if (!rightMousePressed)
    {
        if (m_IsActive)
        {
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            m_IsActive = false;
        }
        m_FirstMouse = true;
    }

    // Só inicia o controle de câmera se o clique do usuário começou dentro
    // da área do viewport (verificado pelo caller via canStartControl).
    if (!m_IsActive && rightMousePressed)
    {
        if (!canStartControl)
        {
            s_ScrollOffset = 0.0f;
            return;
        }
        m_IsActive = true;
        glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        m_FirstMouse = true;
    }

    // Rotação da câmera (somente quando câmera está ativa = botão direito apertado)
    if (m_IsActive)
    {
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(nativeWindow, &mouseX, &mouseY);

        if (m_FirstMouse)
        {
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;
            m_FirstMouse = false;
        }

        const double offsetX = mouseX - m_LastMouseX;
        const double offsetY = mouseY - m_LastMouseY;
        m_LastMouseX = mouseX;
        m_LastMouseY = mouseY;

        constexpr float sensitivity = 0.15f;
        camera.Rotation.Y += static_cast<float>(offsetX * sensitivity);
        camera.Rotation.X += static_cast<float>(offsetY * sensitivity);
        camera.Rotation.X = std::clamp(camera.Rotation.X, -89.0f, 89.0f);
    }

    const float yaw = glm::radians(camera.Rotation.Y);
    const float pitch = glm::radians(camera.Rotation.X);
    const glm::vec3 forward(
        std::sin(yaw) * std::cos(pitch),
        -std::sin(pitch),
        -std::cos(yaw) * std::cos(pitch)
    );
    const glm::vec3 right(
        std::cos(yaw),
        0.0f,
        std::sin(yaw)
    );

    // Movimento de voo só acontece durante o RMB; sem ele, o teclado fica
    // livre para atalhos do editor e o scroll continua podendo fazer dolly.
    const bool allowKeyboardMove = m_IsActive && rightMousePressed;

    glm::vec3 movement(0.0f);
    if (allowKeyboardMove)
    {
        if (window.IsKeyPressed(GLFW_KEY_W)) movement += forward;
        if (window.IsKeyPressed(GLFW_KEY_S)) movement -= forward;
        if (window.IsKeyPressed(GLFW_KEY_A)) movement -= right;
        if (window.IsKeyPressed(GLFW_KEY_D)) movement += right;
        if (window.IsKeyPressed(GLFW_KEY_Q)) movement.y -= 1.0f;
        if (window.IsKeyPressed(GLFW_KEY_E)) movement.y += 1.0f;
    }

    const float scroll = s_ScrollOffset;
    s_ScrollOffset = 0.0f;
    if (std::abs(scroll) > 0.001f)
    {
        if (rightMousePressed && m_IsActive)
        {
            // Scroll com RMB altera a velocidade persistente do fly, sem mover
            // a câmera naquele evento. Um entalhe = multiplicador x1.25.
            m_FlySpeedMultiplier = std::clamp(
                m_FlySpeedMultiplier * std::pow(1.25f, scroll),
                0.01f,
                1000.0f
            );
        }
        else if (!rightMousePressed && viewportHovered)
        {
            // Scroll sem RMB faz dolly pela direção real da câmera e nunca
            // altera a velocidade das teclas.
            constexpr float scrollDistanceFactor = 0.75f;
            constexpr float maxScrollDistancePerEvent = 500.0f;
            constexpr float maxCameraCoordinate = 1000000.0f;
            const float requestedDistance = camera.MoveSpeed * 60.0f *
                scrollDistanceFactor * std::clamp(scroll, -1000.0f, 1000.0f);
            const float distance = std::clamp(
                requestedDistance,
                -maxScrollDistancePerEvent,
                maxScrollDistancePerEvent
            );
            camera.Position.X = std::clamp(
                camera.Position.X + forward.x * distance,
                -maxCameraCoordinate,
                maxCameraCoordinate);
            camera.Position.Y = std::clamp(
                camera.Position.Y + forward.y * distance,
                -maxCameraCoordinate,
                maxCameraCoordinate);
            camera.Position.Z = std::clamp(
                camera.Position.Z + forward.z * distance,
                -maxCameraCoordinate,
                maxCameraCoordinate);
        }
    }

    const float movementLength = glm::length(movement);
    if (movementLength <= 0.001f)
        return;

    if (movementLength > 1.0f)
        movement = glm::normalize(movement);

    // MoveSpeed permanece calibrado como velocidade por frame histórica;
    // convertemos para unidades/segundo para não variar com o FPS.
    const float speed = camera.MoveSpeed * 60.0f * deltaTime * m_FlySpeedMultiplier;
    constexpr float maxCameraCoordinate = 1000000.0f;
    camera.Position.X = std::clamp(camera.Position.X + movement.x * speed,
                                   -maxCameraCoordinate, maxCameraCoordinate);
    camera.Position.Y = std::clamp(camera.Position.Y + movement.y * speed,
                                   -maxCameraCoordinate, maxCameraCoordinate);
    camera.Position.Z = std::clamp(camera.Position.Z + movement.z * speed,
                                   -maxCameraCoordinate, maxCameraCoordinate);
}
