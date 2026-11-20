#include "SurvivalGame.h"
#include "DemoGameContext.h"

#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Camera/Camera.h"
#include "../../Window/Window.h"
#include "../Audio/AudioSystem.h"
#include "../Localization/Localization.h"
#include "../../Renderer/Mesh/MeshCache.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

void SurvivalGame::Reset()
{
    m_Active = false;
    m_WorldGenerated = false;
    m_ToolboxOpen = false;
    m_MouseWasDown = false;
    m_InteractWasDown = false;
    m_PlaceWasDown = false;
    m_PlayerID = 0;
    m_ToolboxID = 0;
    m_HeldToolID = 0;
    m_ArmID = 0;
    m_PlayerEditorMesh.reset();
    m_SelectedTool = 4;
    m_Apples = 0;
    m_Hunger = 100.0f;
    m_Health = 100.0f;
    m_BobTime = 0.0f;
    m_ToolSwing = 0.0f;
    m_AutosaveTimer = 0.0f;
    m_ResourceIDs.clear();
    m_VoxelEntityIDs.clear();
    m_VoxelWorld.Clear();
    m_Wood = 0;
    m_Stone = 0;
    m_Dirt = 0;
    m_Planks = 0;
    m_Axes = 0;
    m_Pickaxes = 0;
    m_SelectedBlock = static_cast<int>(VoxelWorld::BlockType::Dirt);
    m_FirstMouse = true;
}

void SurvivalGame::ActivateFromScene(Scene& scene)
{
    for (const Entity& entity : scene.GetEntities())
    {
        if (entity.GetName() == "SurvivalPlayer")
        {
            m_PlayerID = entity.GetID();
        }
        else if (entity.GetName() == "Toolbox")
        {
            m_ToolboxID = entity.GetID();
        }
    }
    m_Active = m_PlayerID != 0;
}

void SurvivalGame::Save(const DemoGameContext& ctx)
{
    if (!m_Active || !m_WorldGenerated || ctx.ProjectPath.empty())
    {
        return;
    }

    const std::filesystem::path saveDirectory =
        std::filesystem::path(ctx.ProjectPath) / "Save";
    std::filesystem::create_directories(saveDirectory);
    const std::filesystem::path worldPath = saveDirectory / "survival_world.dat";
    const std::filesystem::path playerPath = saveDirectory / "survival_player.dat";
    if (!m_VoxelWorld.Save(worldPath.string()))
        return;

    std::ofstream file(playerPath, std::ios::trunc);
    Entity* player = ctx.Scene.FindEntityByID(m_PlayerID);
    if (!file.is_open() || !player)
        return;

    const Vector3& position = player->GetTransform().Position;
    file << position.X << ' ' << position.Y << ' ' << position.Z << '\n';
    file << ctx.FlyCamera.Rotation.X << ' ' << ctx.FlyCamera.Rotation.Y << '\n';
    file << m_Hunger << ' ' << m_Health << ' '
         << m_Apples << '\n';
    file << m_Wood << ' ' << m_Stone << ' '
         << m_Dirt << ' ' << m_Planks << '\n';
    file << m_Axes << ' ' << m_Pickaxes << ' '
         << m_SelectedBlock << '\n';
}

bool SurvivalGame::LoadGame(const DemoGameContext& ctx)
{
    if (ctx.ProjectPath.empty())
        return false;

    const std::filesystem::path saveDirectory =
        std::filesystem::path(ctx.ProjectPath) / "Save";
    const std::filesystem::path worldPath = saveDirectory / "survival_world.dat";
    if (!m_VoxelWorld.Load(worldPath.string()))
        return false;

    const std::filesystem::path playerPath = saveDirectory / "survival_player.dat";
    std::ifstream file(playerPath);
    Entity* player = ctx.Scene.FindEntityByID(m_PlayerID);
    if (file.is_open() && player)
    {
        Vector3 position;
        file >> position.X >> position.Y >> position.Z;
        file >> ctx.FlyCamera.Rotation.X >> ctx.FlyCamera.Rotation.Y;
        file >> m_Hunger >> m_Health >> m_Apples;
        file >> m_Wood >> m_Stone >> m_Dirt >> m_Planks;
        file >> m_Axes >> m_Pickaxes >> m_SelectedBlock;
        if (file.good())
            player->GetTransform().Position = position;
    }
    return true;
}

void SurvivalGame::RebuildVoxelEntities(const DemoGameContext& ctx)
{
    m_VoxelEntityIDs.clear();
    for (const VoxelWorld::RenderPart& part : m_VoxelWorld.GetRenderParts())
    {
        Entity& entity = ctx.Scene.CreateEntity();
        entity.SetName("VoxelChunk_" + std::to_string(part.ChunkX) + "_" +
                       std::to_string(part.ChunkZ) + "_" +
                       std::to_string(static_cast<int>(part.Type)));
        entity.SetEditorOnly(false);
        entity.GetMeshComponent().MeshData = part.MeshData;
        entity.GetTransform().Position = Vector3(
            static_cast<float>(part.ChunkX * VoxelWorld::ChunkSize),
            0.0f,
            static_cast<float>(part.ChunkZ * VoxelWorld::ChunkSize));
        entity.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
        entity.GetColorComponent().Color = part.Color;
        entity.GetMaterialComponent().MaterialData.Color[0] = part.Color.X;
        entity.GetMaterialComponent().MaterialData.Color[1] = part.Color.Y;
        entity.GetMaterialComponent().MaterialData.Color[2] = part.Color.Z;
        entity.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
        entity.PhysicsEnabled = false;
        entity.UseGravity = false;
        entity.IsStatic = true;
        entity.ColliderEnabled = false;
        entity.PickingRadius = 1.0f;
        m_VoxelEntityIDs.push_back(entity.GetID());
    }
}

void SurvivalGame::GenerateWorld(const DemoGameContext& ctx)
{
    if (!m_Active || m_WorldGenerated)
        return;

    Entity* player = ctx.Scene.FindEntityByID(m_PlayerID);
    if (!player)
        return;

    auto cubeMesh = MeshCache::GetCube();

    auto createRuntimeEntity = [&](const std::string& name,
                                   const std::shared_ptr<Mesh>& mesh,
                                   const Vector3& position,
                                   const Vector3& scale,
                                   const Vector3& color,
                                   bool isStatic,
                                   bool collider) -> Entity&
    {
        Entity& entity = ctx.Scene.CreateEntity();
        entity.SetName(name);
        entity.SetEditorOnly(false);
        entity.GetMeshComponent().MeshData = mesh;
        entity.GetTransform().Position = position;
        entity.GetTransform().Scale = scale;
        entity.GetColorComponent().Color = color;
        entity.GetMaterialComponent().MaterialData.Color[0] = color.X;
        entity.GetMaterialComponent().MaterialData.Color[1] = color.Y;
        entity.GetMaterialComponent().MaterialData.Color[2] = color.Z;
        entity.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
        entity.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
        entity.PhysicsEnabled = false;
        entity.UseGravity = false;
        entity.IsStatic = isStatic;
        entity.ColliderEnabled = collider;
        entity.Collider = Entity::ColliderShape::Box;
        entity.PickingRadius = 1.0f;
        return entity;
    };

    const bool loaded = LoadGame(ctx);
    if (!loaded)
    {
        // O seed fica salvo junto do mundo para que a mesma topologia seja
        // reaberta em sessões futuras.
        std::mt19937 random(std::random_device{}());
        const int seed = static_cast<int>(random());
        m_VoxelWorld.Generate(seed, 2);
        std::uniform_int_distribution<int> spawn(-20, 20);
        const int spawnX = spawn(random);
        const int spawnZ = spawn(random);
        player->GetTransform().Position = Vector3(
            static_cast<float>(spawnX),
            static_cast<float>(m_VoxelWorld.GetSurfaceHeight(spawnX, spawnZ)),
            static_cast<float>(spawnZ));
        ctx.FlyCamera.Rotation.X = 0.0f;
        ctx.FlyCamera.Rotation.Y = 0.0f;
    }

    const int playerX = static_cast<int>(std::floor(player->GetTransform().Position.X));
    const int playerZ = static_cast<int>(std::floor(player->GetTransform().Position.Z));
    player->GetTransform().Position.Y = static_cast<float>(
        m_VoxelWorld.GetSurfaceHeight(playerX, playerZ));
    player->GetTransform().Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    player->Velocity = Vector3(0.0f, 0.0f, 0.0f);
    m_PlayerEditorMesh = player->GetMeshComponent().MeshData;
    player->GetMeshComponent().MeshData.reset();
    player->PhysicsEnabled = false;
    player->UseGravity = false;
    player->ColliderEnabled = false;

    // O terreno agora é um conjunto de chunks voxel mesclados em poucas
    // malhas, em vez de uma entidade por árvore/pedra/bloco.
    m_VoxelWorld.BuildMeshes();
    RebuildVoxelEntities(ctx);

    const int toolboxX = static_cast<int>(std::floor(player->GetTransform().Position.X));
    const int toolboxZ = static_cast<int>(std::floor(player->GetTransform().Position.Z)) + 3;
    if (Entity* toolbox = ctx.Scene.FindEntityByID(m_ToolboxID))
    {
        toolbox->GetTransform().Position = Vector3(
            static_cast<float>(toolboxX),
            static_cast<float>(m_VoxelWorld.GetSurfaceHeight(toolboxX, toolboxZ)) + 0.5f,
            static_cast<float>(toolboxZ));
        toolbox->PhysicsEnabled = false;
        toolbox->UseGravity = false;
    }

    Entity& heldTool = createRuntimeEntity(
        "HeldTool",
        cubeMesh,
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.14f, 0.14f, 0.75f),
        Vector3(0.56f, 0.30f, 0.10f), false, false);
    m_HeldToolID = heldTool.GetID();

    Entity& arm = createRuntimeEntity(
        "SurvivalArm",
        cubeMesh,
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.22f, 0.22f, 0.62f),
        Vector3(0.58f, 0.32f, 0.18f), false, false);
    m_ArmID = arm.GetID();
    m_WorldGenerated = true;
}

void SurvivalGame::Update(const DemoGameContext& ctx, float deltaTime)
{
    if (!m_Active || !m_WorldGenerated || deltaTime <= 0.0f)
        return;

    Entity* player = ctx.Scene.FindEntityByID(m_PlayerID);
    Entity* toolbox = ctx.Scene.FindEntityByID(m_ToolboxID);
    Entity* heldTool = ctx.Scene.FindEntityByID(m_HeldToolID);
    if (!player)
    {
        Reset();
        return;
    }

    const bool rightMouse = glfwGetMouseButton(
        ctx.Window.GetNativeWindow(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool mouseDown = glfwGetMouseButton(
        ctx.Window.GetNativeWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool mouseClicked = mouseDown && !m_MouseWasDown;
    m_MouseWasDown = mouseDown;
    const bool placeDown = ctx.Window.IsKeyPressed(GLFW_KEY_B);
    const bool placePressed = placeDown && !m_PlaceWasDown;
    m_PlaceWasDown = placeDown;

    // Mouse look de primeira pessoa, com cursor preso apenas enquanto o RMB
    // está pressionado para preservar o fluxo normal do editor.
    if (rightMouse && !m_ToolboxOpen)
    {
        glfwSetInputMode(ctx.Window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(ctx.Window.GetNativeWindow(), &mouseX, &mouseY);
        if (m_FirstMouse)
        {
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;
            m_FirstMouse = false;
        }
        ctx.FlyCamera.Rotation.Y += static_cast<float>((mouseX - m_LastMouseX) * 0.15);
        ctx.FlyCamera.Rotation.X += static_cast<float>((mouseY - m_LastMouseY) * 0.15);
        m_LastMouseX = mouseX;
        m_LastMouseY = mouseY;
        ctx.FlyCamera.Rotation.X = std::clamp(ctx.FlyCamera.Rotation.X, -89.0f, 89.0f);
    }
    else
    {
        glfwSetInputMode(ctx.Window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        m_FirstMouse = true;
    }

    const float yaw = glm::radians(ctx.FlyCamera.Rotation.Y);
    const float pitch = glm::radians(ctx.FlyCamera.Rotation.X);
    const glm::vec3 forward(std::sin(yaw), 0.0f, -std::cos(yaw));
    const glm::vec3 right(std::cos(yaw), 0.0f, std::sin(yaw));
    const glm::vec3 cameraForward(
        std::sin(yaw) * std::cos(pitch),
        -std::sin(pitch),
        -std::cos(yaw) * std::cos(pitch));
    glm::vec3 movement(0.0f);
    if (ctx.Window.IsKeyPressed(GLFW_KEY_W) || ctx.Window.IsKeyPressed(GLFW_KEY_UP))
        movement += forward;
    if (ctx.Window.IsKeyPressed(GLFW_KEY_S) || ctx.Window.IsKeyPressed(GLFW_KEY_DOWN))
        movement -= forward;
    if (ctx.Window.IsKeyPressed(GLFW_KEY_D) || ctx.Window.IsKeyPressed(GLFW_KEY_RIGHT))
        movement += right;
    if (ctx.Window.IsKeyPressed(GLFW_KEY_A) || ctx.Window.IsKeyPressed(GLFW_KEY_LEFT))
        movement -= right;

    const float movementLength = glm::length(movement);
    const bool moving = movementLength > 0.001f;
    if (moving)
    {
        movement /= movementLength;
        const bool sprint = ctx.Window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
                            ctx.Window.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
        const float moveSpeed = sprint ? 8.0f : 5.0f;
        const float step = moveSpeed * deltaTime;
        const float nextX = player->GetTransform().Position.X + movement.x * step;
        const float nextZ = player->GetTransform().Position.Z + movement.z * step;
        const int bodyY = static_cast<int>(std::floor(player->GetTransform().Position.Y + 0.55f));
        const bool blocked = VoxelWorld::IsSolid(m_VoxelWorld.GetBlock(
            static_cast<int>(std::floor(nextX)), bodyY,
            static_cast<int>(std::floor(nextZ)))) ||
            VoxelWorld::IsSolid(m_VoxelWorld.GetBlock(
            static_cast<int>(std::floor(nextX)), bodyY + 1,
            static_cast<int>(std::floor(nextZ))));
        const int nextSurface = m_VoxelWorld.GetSurfaceHeight(
            static_cast<int>(std::floor(nextX)),
            static_cast<int>(std::floor(nextZ)));
        const float currentSurface = player->GetTransform().Position.Y;
        if (!blocked && std::abs(static_cast<float>(nextSurface) - currentSurface) <= 1.05f)
        {
            player->GetTransform().Position.X = nextX;
            player->GetTransform().Position.Z = nextZ;
            player->GetTransform().Position.Y = static_cast<float>(nextSurface);
        }
        m_BobTime += deltaTime * (sprint ? 13.0f : 9.0f);
    }
    player->GetTransform().Position.X = std::clamp(
        player->GetTransform().Position.X, -30.0f, 30.0f);
    player->GetTransform().Position.Z = std::clamp(
        player->GetTransform().Position.Z, -30.0f, 30.0f);
    player->GetTransform().Position.Y = static_cast<float>(
        m_VoxelWorld.GetSurfaceHeight(
            static_cast<int>(std::floor(player->GetTransform().Position.X)),
            static_cast<int>(std::floor(player->GetTransform().Position.Z))));
    player->Velocity = Vector3(0.0f, 0.0f, 0.0f);

    if (moving)
    {
        ctx.FlyCamera.Position.X = player->GetTransform().Position.X;
        ctx.FlyCamera.Position.Z = player->GetTransform().Position.Z;
        ctx.FlyCamera.Position.Y = player->GetTransform().Position.Y +
            0.62f + std::sin(m_BobTime) * 0.035f;
    }
    else
    {
        ctx.FlyCamera.Position.X = player->GetTransform().Position.X;
        ctx.FlyCamera.Position.Y = player->GetTransform().Position.Y + 0.62f;
        ctx.FlyCamera.Position.Z = player->GetTransform().Position.Z;
    }

    m_Hunger = std::max(0.0f, m_Hunger - deltaTime * 0.55f);
    if (m_Hunger <= 0.0f)
        m_Health = std::max(0.0f, m_Health - deltaTime * 4.0f);

    const bool interactDown = ctx.Window.IsKeyPressed(GLFW_KEY_E);
    const bool interactPressed = interactDown && !m_InteractWasDown;
    m_InteractWasDown = interactDown;
    if (toolbox)
    {
        const float dx = toolbox->GetTransform().Position.X - player->GetTransform().Position.X;
        const float dz = toolbox->GetTransform().Position.Z - player->GetTransform().Position.Z;
        const bool nearToolbox = dx * dx + dz * dz <= 4.0f * 4.0f;
        if (nearToolbox && (interactPressed || mouseClicked) && !rightMouse)
            m_ToolboxOpen = !m_ToolboxOpen;
    }

    m_ToolSwing = std::max(0.0f, m_ToolSwing - deltaTime * 5.0f);
    if (m_ToolboxOpen)
    {
        // A caixa aberta congela as ações de coleta, mas mantém a animação
        // voltando suavemente ao repouso.
    }
    else
    {
        VoxelWorld::Hit hit;
        const glm::vec3 cameraOrigin(
            ctx.FlyCamera.Position.X, ctx.FlyCamera.Position.Y, ctx.FlyCamera.Position.Z);

        if (mouseClicked && !rightMouse && m_SelectedTool == 5 &&
            m_Apples > 0)
        {
            --m_Apples;
            m_Hunger = std::min(100.0f, m_Hunger + 28.0f);
            m_ToolSwing = 1.0f;
            AudioSystem::Get().PlayEat();
        }
        else if (mouseClicked && !rightMouse && m_SelectedTool != 5 &&
                 m_VoxelWorld.Raycast(cameraOrigin, cameraForward, 6.0f, hit))
        {
            const bool isTree = hit.Type == VoxelWorld::BlockType::Wood ||
                                hit.Type == VoxelWorld::BlockType::Leaves;
            const bool isRock = hit.Type == VoxelWorld::BlockType::Stone;
            const bool isApple = hit.Type == VoxelWorld::BlockType::Apple;
            const bool isGround = hit.Type == VoxelWorld::BlockType::Grass ||
                                  hit.Type == VoxelWorld::BlockType::Dirt;
            const bool hasAxe = m_Axes > 0;
            const bool hasPickaxe = m_Pickaxes > 0;
            const bool canHarvest = (isTree &&
                                     ((m_SelectedTool == 0 && hasAxe) ||
                                      m_SelectedTool == 4)) ||
                                    (isRock &&
                                     ((m_SelectedTool == 1 && hasPickaxe) ||
                                      m_SelectedTool == 4)) ||
                                    (isApple && m_SelectedTool == 4) ||
                                    (isGround && m_SelectedTool == 4);
            if (canHarvest)
            {
                if (isApple)
                    ++m_Apples;
                else if (isTree)
                    ++m_Wood;
                else if (isRock)
                    ++m_Stone;
                else if (isGround)
                    ++m_Dirt;

                m_VoxelWorld.SetBlock(hit.X, hit.Y, hit.Z,
                                      VoxelWorld::BlockType::Air);
                m_VoxelWorld.BuildMeshes();
                RebuildVoxelEntities(ctx);
                m_ToolSwing = 1.0f;
                AudioSystem::Get().PlayPickup();
            }
        }

        if (placePressed && !rightMouse &&
            m_VoxelWorld.Raycast(cameraOrigin, cameraForward, 6.0f, hit))
        {
            const VoxelWorld::BlockType block = static_cast<VoxelWorld::BlockType>(
                std::clamp(m_SelectedBlock, 2, 4));
            int* inventory = nullptr;
            if (block == VoxelWorld::BlockType::Dirt) inventory = &m_Dirt;
            if (block == VoxelWorld::BlockType::Stone) inventory = &m_Stone;
            if (block == VoxelWorld::BlockType::Wood) inventory = &m_Planks;
            if (inventory && *inventory > 0 &&
                m_VoxelWorld.SetBlock(hit.PreviousX, hit.PreviousY,
                                      hit.PreviousZ, block))
            {
                --(*inventory);
                m_VoxelWorld.BuildMeshes();
                RebuildVoxelEntities(ctx);
                AudioSystem::Get().PlayToolSwing();
            }
        }
    }

    if (m_Health <= 0.0f)
    {
        player->GetTransform().Position = Vector3(
            0.0f, static_cast<float>(m_VoxelWorld.GetSurfaceHeight(0, 0)), 0.0f);
        m_Hunger = 100.0f;
        m_Health = 100.0f;
        m_Apples = 0;
    }

    m_AutosaveTimer += deltaTime;
    if (m_AutosaveTimer >= 10.0f)
    {
        Save(ctx);
        m_AutosaveTimer = 0.0f;
    }

    heldTool = ctx.Scene.FindEntityByID(m_HeldToolID);
    Entity* arm = ctx.Scene.FindEntityByID(m_ArmID);
    if (heldTool)
    {
        const glm::vec3 cameraRight = right;
        const glm::vec3 toolPosition(
            ctx.FlyCamera.Position.X + cameraForward.x * 0.72f + cameraRight.x * 0.38f,
            ctx.FlyCamera.Position.Y + cameraForward.y * 0.72f - 0.28f,
            ctx.FlyCamera.Position.Z + cameraForward.z * 0.72f + cameraRight.z * 0.38f);
        const float swing = std::sin(m_ToolSwing * glm::pi<float>()) * 0.22f;
        heldTool->GetTransform().Position = Vector3(
            toolPosition.x, toolPosition.y - swing, toolPosition.z);
        heldTool->GetTransform().Scale = m_SelectedTool == 4
            ? Vector3(0.20f, 0.20f, 0.20f)
            : Vector3(0.14f, 0.14f, 0.75f);
        const Vector3 toolColor[] = {
            Vector3(0.64f, 0.24f, 0.06f), Vector3(0.35f, 0.38f, 0.42f),
            Vector3(0.25f, 0.55f, 0.12f), Vector3(1.0f, 0.60f, 0.04f),
            Vector3(0.75f, 0.46f, 0.28f), Vector3(0.88f, 0.04f, 0.03f)
        };
        const Vector3 color = toolColor[std::clamp(m_SelectedTool, 0, 5)];
        heldTool->GetColorComponent().Color = color;
        heldTool->GetMaterialComponent().MaterialData.Color[0] = color.X;
        heldTool->GetMaterialComponent().MaterialData.Color[1] = color.Y;
        heldTool->GetMaterialComponent().MaterialData.Color[2] = color.Z;
        heldTool->GetTransform().Rotation = glm::angleAxis(
            yaw + glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        if (arm)
        {
            const float armSwing = std::sin(m_ToolSwing * glm::pi<float>()) * 0.30f;
            const glm::vec3 armPosition(
                ctx.FlyCamera.Position.X + cameraForward.x * 0.62f + cameraRight.x * 0.28f,
                ctx.FlyCamera.Position.Y + cameraForward.y * 0.62f - 0.35f - armSwing,
                ctx.FlyCamera.Position.Z + cameraForward.z * 0.62f + cameraRight.z * 0.28f);
            arm->GetTransform().Position = Vector3(
                armPosition.x, armPosition.y, armPosition.z);
            arm->GetTransform().Rotation = glm::angleAxis(
                yaw + glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
}

void SurvivalGame::DrawHUD(const DemoGameContext& ctx)
{
    if (!m_Active || ctx.EditMode)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 screenCenter = viewport->GetCenter();
    drawList->AddLine({ screenCenter.x - 7.0f, screenCenter.y },
                      { screenCenter.x + 7.0f, screenCenter.y },
                      IM_COL32(255, 255, 255, 220), 1.5f);
    drawList->AddLine({ screenCenter.x, screenCenter.y - 7.0f },
                      { screenCenter.x, screenCenter.y + 7.0f },
                      IM_COL32(255, 255, 255, 220), 1.5f);

    ImGui::SetNextWindowPos({ viewport->WorkPos.x + 18.0f,
                              viewport->WorkPos.y + viewport->WorkSize.y - 112.0f },
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    const ImGuiWindowFlags statusFlags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin(UI("SurvivalStatus##hud"), nullptr, statusFlags))
    {
        ImGui::TextUnformatted(UI("MARAE WILDLANDS"));
        ImGui::TextUnformatted(L("runtime.health"));
        ImGui::SameLine();
        ImGui::ProgressBar(m_Health / 100.0f, { 150.0f, 12.0f });
        ImGui::TextUnformatted(L("runtime.hunger"));
        ImGui::SameLine();
        ImGui::ProgressBar(m_Hunger / 100.0f, { 150.0f, 12.0f });
        ImGui::Text(UI("%s: %d"), L("runtime.apples"), m_Apples);
        ImGui::Text(UI("%s: %d  %s: %d  %s: %d"), L("runtime.wood"), m_Wood,
                    L("runtime.stone"), m_Stone, L("runtime.dirt"), m_Dirt);
        ImGui::Text(UI("%s: %d  %s: %d  %s: %d"), L("runtime.planks"), m_Planks,
                    L("runtime.axes"), m_Axes, L("runtime.pickaxes"), m_Pickaxes);
    }
    ImGui::End();

    Entity* player = ctx.Scene.FindEntityByID(m_PlayerID);
    Entity* toolbox = ctx.Scene.FindEntityByID(m_ToolboxID);
    bool nearToolbox = false;
    if (player && toolbox)
    {
        const float dx = toolbox->GetTransform().Position.X - player->GetTransform().Position.X;
        const float dz = toolbox->GetTransform().Position.Z - player->GetTransform().Position.Z;
        nearToolbox = dx * dx + dz * dz <= 4.0f * 4.0f;
    }

    if (nearToolbox && !m_ToolboxOpen)
    {
        ImGui::SetNextWindowPos({ viewport->GetCenter().x, viewport->WorkPos.y + 88.0f },
                                ImGuiCond_Always, { 0.5f, 0.0f });
        ImGui::SetNextWindowBgAlpha(0.65f);
        if (ImGui::Begin(UI("ToolboxHint##survival"), nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted(L("runtime.toolbox_hint"));
        }
        ImGui::End();
    }

    if (!m_ToolboxOpen)
        return;

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, { 0.5f, 0.5f });
    ImGui::SetNextWindowBgAlpha(0.94f);
    const ImGuiWindowFlags toolboxFlags = ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin((std::string(L("runtime.toolbox")) + "##survival").c_str(), nullptr, toolboxFlags))
    {
        ImGui::TextUnformatted(L("runtime.toolbox"));
        ImGui::TextUnformatted(L("runtime.choose_tool"));
        ImGui::Separator();
        const char* slots[] = { L("runtime.axes"), L("runtime.pickaxes"), "Hoe", "Torch", L("runtime.hand"), L("runtime.apple") };
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 2; ++column)
            {
                const int slot = row * 2 + column;
                if (column > 0)
                    ImGui::SameLine(0.0f, 8.0f);
                ImGui::PushID(slot);
                if (slot == m_SelectedTool)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.42f, 0.78f, 1.0f));
                if (ImGui::Button(slots[slot], { 84.0f, 84.0f }))
                {
                    if ((slot != 0 || m_Axes > 0) &&
                        (slot != 1 || m_Pickaxes > 0))
                    {
                        m_SelectedTool = slot;
                    }
                }
                if (slot == m_SelectedTool)
                    ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted(L("runtime.create_tools"));
        if (ImGui::Button(L("runtime.planks_recipe"), { 176.0f, 28.0f }) &&
            m_Wood >= 1)
        {
            --m_Wood;
            m_Planks += 4;
            AudioSystem::Get().PlayCraft();
        }
        if (ImGui::Button(L("runtime.axe_recipe"), { 176.0f, 28.0f }) &&
            m_Planks >= 3)
        {
            m_Planks -= 3;
            ++m_Axes;
            AudioSystem::Get().PlayCraft();
        }
        if (ImGui::Button(L("runtime.pickaxe_recipe"), { 176.0f, 28.0f }) &&
            m_Planks >= 2 && m_Stone >= 3)
        {
            m_Planks -= 2;
            m_Stone -= 3;
            ++m_Pickaxes;
            AudioSystem::Get().PlayCraft();
        }
        ImGui::Separator();
        ImGui::TextUnformatted(L("runtime.place_blocks"));
        if (ImGui::Button(L("runtime.dirt"), { 54.0f, 26.0f }))
            m_SelectedBlock = static_cast<int>(VoxelWorld::BlockType::Dirt);
        ImGui::SameLine();
        if (ImGui::Button(L("runtime.stone"), { 54.0f, 26.0f }))
            m_SelectedBlock = static_cast<int>(VoxelWorld::BlockType::Stone);
        ImGui::SameLine();
        if (ImGui::Button(L("runtime.wood"), { 68.0f, 26.0f }))
            m_SelectedBlock = static_cast<int>(VoxelWorld::BlockType::Wood);
        ImGui::Separator();
        if (ImGui::Button(L("common.close"), { 176.0f, 28.0f }))
            m_ToolboxOpen = false;
    }
    ImGui::End();
}

void SurvivalGame::OnExitPlay(const DemoGameContext& ctx)
{
    Save(ctx);
    if (Entity* player = ctx.Scene.FindEntityByID(m_PlayerID))
    {
        player->GetMeshComponent().MeshData = m_PlayerEditorMesh;
    }
}
