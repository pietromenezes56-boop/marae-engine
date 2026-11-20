#include "RaceGame.h"
#include "DemoGameContext.h"

#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Camera/Camera.h"
#include "../../Window/Window.h"
#include "../Localization/Localization.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float RACE_HALF_WIDTH = 24.0f;
    constexpr float RACE_HALF_DEPTH = 14.0f;
    constexpr float RACE_ROUTE_LENGTH =
        2.0f * ((RACE_HALF_WIDTH * 2.0f) + (RACE_HALF_DEPTH * 2.0f));

    struct RaceRouteSample
    {
        glm::vec3 Position;
        float YawDegrees = 0.0f;
    };

    RaceRouteSample SampleRaceRoute(float distance)
    {
        distance = std::fmod(distance, RACE_ROUTE_LENGTH);
        if (distance < 0.0f)
            distance += RACE_ROUTE_LENGTH;

        const float straightLength = RACE_HALF_WIDTH * 2.0f;
        const float sideLength = RACE_HALF_DEPTH * 2.0f;

        if (distance < straightLength)
        {
            return {
                glm::vec3(RACE_HALF_WIDTH - distance, 0.45f, RACE_HALF_DEPTH),
                -90.0f
            };
        }

        distance -= straightLength;
        if (distance < sideLength)
        {
            return {
                glm::vec3(-RACE_HALF_WIDTH, 0.45f, RACE_HALF_DEPTH - distance),
                0.0f
            };
        }

        distance -= sideLength;
        if (distance < straightLength)
        {
            return {
                glm::vec3(-RACE_HALF_WIDTH + distance, 0.45f, -RACE_HALF_DEPTH),
                90.0f
            };
        }

        distance -= straightLength;
        return {
            glm::vec3(RACE_HALF_WIDTH, 0.45f, -RACE_HALF_DEPTH + distance),
            180.0f
        };
    }

    float QuaternionYawDegrees(const glm::quat& rotation)
    {
        const float sinYaw = 2.0f * (rotation.w * rotation.y + rotation.z * rotation.x);
        const float cosYaw = 1.0f - 2.0f * (rotation.y * rotation.y + rotation.z * rotation.z);
        return glm::degrees(std::atan2(sinYaw, cosYaw));
    }
}

void RaceGame::Reset()
{
    m_Active = false;
    m_Finished = false;
    m_PlayerID = 0;
    m_AICarIDs.clear();
    m_Elapsed = 0.0f;
    m_Lap = 1;
    m_NextCheckpoint = 0;
}

void RaceGame::ActivateFromScene(Scene& scene)
{
    for (const Entity& entity : scene.GetEntities())
    {
        if (entity.GetName() == "PlayerCar")
        {
            m_PlayerID = entity.GetID();
        }
        else if (entity.GetName().rfind("AI_Car", 0) == 0)
        {
            m_AICarIDs.push_back(entity.GetID());
        }
    }
    m_Active = m_PlayerID != 0;
}

void RaceGame::Update(const DemoGameContext& ctx, float deltaTime)
{
    if (!m_Active || deltaTime <= 0.0f)
        return;

    Entity* player = ctx.Scene.FindEntityByID(m_PlayerID);
    if (!player)
    {
        Reset();
        return;
    }

    if (!m_Finished)
    {
        m_Elapsed += deltaTime;

        Transform& playerTransform = player->GetTransform();
        float yawDegrees = QuaternionYawDegrees(playerTransform.Rotation);

        const float yawRadians = glm::radians(yawDegrees);
        glm::vec3 forward(std::sin(yawRadians), 0.0f, -std::cos(yawRadians));
        const float forwardSpeed =
            player->Velocity.X * forward.x + player->Velocity.Z * forward.z;

        const bool accelerate = ctx.Window.IsKeyPressed(GLFW_KEY_W) ||
                                ctx.Window.IsKeyPressed(GLFW_KEY_UP);
        const bool brake = ctx.Window.IsKeyPressed(GLFW_KEY_S) ||
                           ctx.Window.IsKeyPressed(GLFW_KEY_DOWN);
        const float steer =
            (ctx.Window.IsKeyPressed(GLFW_KEY_D) || ctx.Window.IsKeyPressed(GLFW_KEY_RIGHT) ? 1.0f : 0.0f) -
            (ctx.Window.IsKeyPressed(GLFW_KEY_A) || ctx.Window.IsKeyPressed(GLFW_KEY_LEFT) ? 1.0f : 0.0f);

        float speed = forwardSpeed;
        if (accelerate)
            speed += 22.0f * deltaTime;
        else if (brake)
            speed -= 30.0f * deltaTime;
        else
            speed *= std::exp(-1.8f * deltaTime);

        speed = std::clamp(speed, -8.0f, 30.0f);
        const float steeringAmount = std::clamp(std::abs(speed) / 10.0f, 0.0f, 1.0f);
        if (std::abs(speed) > 0.1f && std::abs(steer) > 0.01f)
        {
            yawDegrees += steer * 105.0f * steeringAmount * deltaTime *
                          (speed >= 0.0f ? 1.0f : -1.0f);
        }

        const float newYawRadians = glm::radians(yawDegrees);
        forward = glm::vec3(
            std::sin(newYawRadians),
            0.0f,
            -std::cos(newYawRadians)
        );
        player->Velocity.X = forward.x * speed;
        player->Velocity.Z = forward.z * speed;
        playerTransform.Rotation = glm::angleAxis(
            newYawRadians,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        const glm::vec3 checkpoints[] = {
            glm::vec3(-RACE_HALF_WIDTH, 0.45f, RACE_HALF_DEPTH),
            glm::vec3(-RACE_HALF_WIDTH, 0.45f, -RACE_HALF_DEPTH),
            glm::vec3(RACE_HALF_WIDTH, 0.45f, -RACE_HALF_DEPTH),
            glm::vec3(RACE_HALF_WIDTH, 0.45f, RACE_HALF_DEPTH)
        };
        const glm::vec3 playerPosition(
            playerTransform.Position.X,
            playerTransform.Position.Y,
            playerTransform.Position.Z
        );
        const glm::vec3 checkpointDelta =
            playerPosition - checkpoints[m_NextCheckpoint];
        if (glm::dot(checkpointDelta, checkpointDelta) <= 7.0f * 7.0f)
        {
            ++m_NextCheckpoint;
            if (m_NextCheckpoint >= 4)
            {
                m_NextCheckpoint = 0;
                ++m_Lap;
                if (m_Lap > m_TotalLaps)
                {
                    m_Finished = true;
                    player->Velocity.X = 0.0f;
                    player->Velocity.Z = 0.0f;
                }
            }
        }
    }

    // IA simples baseada na mesma rota fechada. Ela já é suficiente para
    // validar movimentação, câmera e colisões do projeto de corrida.
    for (size_t i = 0; i < m_AICarIDs.size(); ++i)
    {
        Entity* ai = ctx.Scene.FindEntityByID(m_AICarIDs[i]);
        if (!ai)
            continue;

        const float aiSpeed = 13.0f + static_cast<float>(i) * 1.4f;
        const float phase = 9.0f + static_cast<float>(i) * 14.0f;
        const RaceRouteSample sample = SampleRaceRoute(m_Elapsed * aiSpeed + phase);
        ai->GetTransform().Position.X = sample.Position.x;
        ai->GetTransform().Position.Y = sample.Position.y;
        ai->GetTransform().Position.Z = sample.Position.z;
        ai->GetTransform().Rotation = glm::angleAxis(
            glm::radians(sample.YawDegrees),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        ai->Velocity = Vector3(0.0f, 0.0f, 0.0f);
    }

    // Câmera de perseguição: o botão direito ainda permite olhar livremente.
    const bool freeLook = glfwGetMouseButton(
        ctx.Window.GetNativeWindow(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (!freeLook)
    {
        const float cameraYaw = glm::radians(
            QuaternionYawDegrees(player->GetTransform().Rotation));
        const glm::vec3 carForward(std::sin(cameraYaw), 0.0f, -std::cos(cameraYaw));
        const glm::vec3 carPosition(
            player->GetTransform().Position.X,
            player->GetTransform().Position.Y,
            player->GetTransform().Position.Z
        );
        const glm::vec3 desiredCamera = carPosition - carForward * 9.0f + glm::vec3(0.0f, 5.5f, 0.0f);
        const float follow = std::clamp(deltaTime * 6.0f, 0.0f, 1.0f);
        ctx.FlyCamera.Position.X += (desiredCamera.x - ctx.FlyCamera.Position.X) * follow;
        ctx.FlyCamera.Position.Y += (desiredCamera.y - ctx.FlyCamera.Position.Y) * follow;
        ctx.FlyCamera.Position.Z += (desiredCamera.z - ctx.FlyCamera.Position.Z) * follow;

        const glm::vec3 lookAt = carPosition + carForward * 5.0f + glm::vec3(0.0f, 0.7f, 0.0f);
        const glm::vec3 lookDelta = lookAt - glm::vec3(
            ctx.FlyCamera.Position.X, ctx.FlyCamera.Position.Y, ctx.FlyCamera.Position.Z);
        const float horizontalDistance = std::sqrt(lookDelta.x * lookDelta.x + lookDelta.z * lookDelta.z);
        ctx.FlyCamera.Rotation.Y = glm::degrees(std::atan2(lookDelta.x, -lookDelta.z));
        ctx.FlyCamera.Rotation.X = glm::degrees(std::atan2(lookDelta.y, horizontalDistance));
    }
}

void RaceGame::DrawHUD(const DemoGameContext& ctx)
{
    if (!m_Active || ctx.EditMode)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->GetCenter().x, viewport->WorkPos.y + 42.0f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.0f)
    );
    ImGui::SetNextWindowBgAlpha(0.82f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav;

    if (ImGui::Begin(UI("Marae Speedway HUD##race"), nullptr, flags))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.70f, 1.0f, 1.0f));
        ImGui::TextUnformatted(UI("MARAE SPEEDWAY  |  APEX CUP"));
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Text(UI("%s  %d/%d     %s  %06.2fs"), L("runtime.lap"),
                    std::min(m_Lap, m_TotalLaps),
                    m_TotalLaps,
                    L("runtime.time"),
                    m_Elapsed);
        if (m_Finished)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.18f, 1.0f));
            ImGui::TextUnformatted(L("runtime.finish"));
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextUnformatted(L("runtime.race_controls"));
        }
    }
    ImGui::End();
}
