#pragma once

#include <algorithm>

class Window;
class Camera;

class CameraController
{
public:
    void Update(
        Window& window,
        Camera& camera,
        bool canStartControl,
        bool viewportHovered
    );

    // Libera o mouse quando o viewport passa a ser dirigido por uma câmera
    // de cena, evitando que o fly continue capturando o cursor em segundo plano.
    void Reset(Window& window);

    bool IsControlling() const { return m_IsActive; }
    float GetFlySpeedMultiplier() const { return m_FlySpeedMultiplier; }
    void SetFlySpeedMultiplier(float multiplier)
    {
        m_FlySpeedMultiplier = std::clamp(multiplier, 0.01f, 1000.0f);
    }

    static float s_ScrollOffset;

private:
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    double m_LastUpdateTime = 0.0;

    bool m_IsActive = false;
    bool m_FirstMouse = true;
    bool m_ScrollCallbackRegistered = false;
    float m_FlySpeedMultiplier = 1.0f;

   
};
