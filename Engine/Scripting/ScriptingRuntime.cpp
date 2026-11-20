#include "ScriptingRuntime.h"
#include "../Navigation/NavigationSystem.h"
#include "ScriptingAPI.h"
#include "ScriptDebugger.h"
#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Transform/Transform.h"
#include "../../Scene/Components/ScriptComponent.h"
#include "../../Scene/Components/LightComponent.h"
#include "../../Scene/Components/MaterialComponent.h"
#include "../../Scene/Components/MeshComponent.h"
#include "../../Scene/Components/ColorComponent.h"
#include "../../Scene/SceneCommands.h"
#include "../../Math/Vector3.h"
#include "../../Core/Application.h"
#include "../../Core/Diagnostics/DiagnosticLog.h"
#include "../../Core/Diagnostics/PerformanceMonitor.h"
#include "../../Window/Window.h"
#include "../Input/InputMap.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <array>
#include <sstream>
#include <iomanip>
#include <locale>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GUID Windows_SDK_GUID
#include <windows.h>
#undef GUID
#endif

#include <GLFW/glfw3.h>

static Scene* g_Scene = nullptr;
static float g_DeltaTime = 0.0f;
static float g_TimeScale = 1.0f;
static float g_FixedDelta = 0.01666f;
static double g_TotalTime = 0.0;
static std::array<bool, GLFW_KEY_LAST + 1> g_KeyState{};
static std::array<bool, GLFW_KEY_LAST + 1> g_PreviousKeyState{};
static std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> g_MouseState{};
static std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> g_PreviousMouseState{};
static double g_MouseDeltaX = 0.0;
static double g_MouseDeltaY = 0.0;
static double g_PreviousMouseX = 0.0;
static double g_PreviousMouseY = 0.0;
static bool g_HasMouseSnapshot = false;
static std::vector<uint64_t> g_PendingDestroy;

extern void* g_CurrentApplication;

static Window* GetInputWindow()
{
    auto* application = static_cast<Application*>(g_CurrentApplication);
    return application ? &application->GetWindow() : nullptr;
}

static void RefreshInputState()
{
    Window* window = GetInputWindow();
    GLFWwindow* nativeWindow = window ? window->GetNativeWindow() : nullptr;
    if (!nativeWindow)
        return;

    g_PreviousKeyState = g_KeyState;
    g_PreviousMouseState = g_MouseState;
    const double mouseX = window->GetMouseX();
    const double mouseY = window->GetMouseY();
    g_MouseDeltaX = g_HasMouseSnapshot ? mouseX - g_PreviousMouseX : 0.0;
    g_MouseDeltaY = g_HasMouseSnapshot ? mouseY - g_PreviousMouseY : 0.0;
    g_PreviousMouseX = mouseX;
    g_PreviousMouseY = mouseY;
    g_HasMouseSnapshot = true;
    for (int key = 0; key <= GLFW_KEY_LAST; ++key)
        g_KeyState[static_cast<size_t>(key)] = glfwGetKey(nativeWindow, key) == GLFW_PRESS;
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button)
        g_MouseState[static_cast<size_t>(button)] =
            glfwGetMouseButton(nativeWindow, button) == GLFW_PRESS;

    MaraeInput::InputMap::Instance().Update(
        g_KeyState, g_PreviousKeyState,
        g_MouseState, g_PreviousMouseState,
        static_cast<float>(g_MouseDeltaX), static_cast<float>(g_MouseDeltaY));

    // A entrada do RMB pode restaurar/recapturar o cursor que o editor
    // escondeu. A mudança de coordenadas não é movimento intencional do
    // jogador; descarta o salto inicial e limita teletransportes do cursor.
    const size_t rightMouse = static_cast<size_t>(GLFW_MOUSE_BUTTON_RIGHT);
    if (g_MouseState[rightMouse] && !g_PreviousMouseState[rightMouse])
    {
        g_MouseDeltaX = 0.0;
        g_MouseDeltaY = 0.0;
    }
    else
    {
        constexpr double maxMouseDeltaPerFrame = 250.0;
        g_MouseDeltaX = std::clamp(g_MouseDeltaX, -maxMouseDeltaPerFrame, maxMouseDeltaPerFrame);
        g_MouseDeltaY = std::clamp(g_MouseDeltaY, -maxMouseDeltaPerFrame, maxMouseDeltaPerFrame);
    }
}

static thread_local std::wstring s_WStrRet;
static std::wstring Utf8ToWide(const std::string& str);

static const wchar_t* Native_NavigationFindPath(float sx, float sy, float sz, float dx, float dy, float dz)
{
    s_WStrRet = Utf8ToWide(ScriptingRuntime::Instance().FindNavigationPath(sx, sy, sz, dx, dy, dz));
    return s_WStrRet.c_str();
}
static thread_local std::vector<uint64_t> s_TagResults;

static std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

static std::string WideToUtf8(const wchar_t* wstr)
{
    if (!wstr || !*wstr) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return std::string();
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], size - 1, nullptr, nullptr);
    return str;
}

static Entity* FindEntity(uint64_t id)
{
    if (!g_Scene) return nullptr;
    return g_Scene->FindEntityByID(id);
}

static int Native_EntityCount()
{
    if (!g_Scene) return 0;
    return static_cast<int>(g_Scene->GetEntities().size());
}

static uint64_t Native_EntityGetID(uint64_t entityIndexOrId)
{
    if (!g_Scene) return 0;
    if (Entity* e = g_Scene->FindEntityByID(entityIndexOrId))
        return e->GetID();
    auto& entities = g_Scene->GetEntities();
    if (entityIndexOrId < entities.size())
        return entities[static_cast<size_t>(entityIndexOrId)].GetID();
    return 0;
}

static const wchar_t* Native_EntityGetName(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return L"";
    s_WStrRet = Utf8ToWide(e->GetName());
    return s_WStrRet.c_str();
}

static void Native_EntitySetName(uint64_t entityId, const wchar_t* name)
{
    Entity* e = FindEntity(entityId);
    if (!e || !name) return;
    e->SetName(WideToUtf8(name));
}

static bool Native_EntityGetActive(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return false;
    return e->Active;
}

static void Native_EntitySetActive(uint64_t entityId, bool active)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->Active = active;
}

static const wchar_t* Native_EntityGetTag(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return L"";
    s_WStrRet = Utf8ToWide(e->Tag);
    return s_WStrRet.c_str();
}

static void Native_EntitySetTag(uint64_t entityId, const wchar_t* tag)
{
    Entity* e = FindEntity(entityId);
    if (!e || !tag) return;
    e->Tag = WideToUtf8(tag);
}

static int Native_EntityGetLayer(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return 0;
    return e->Layer;
}

static void Native_EntitySetLayer(uint64_t entityId, int layer)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->Layer = layer;
}

static uint64_t Native_EntityGetTransform(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return 0;
    return entityId;
}

static void Native_TransformGetLocalPosition(uint64_t h, float* ox, float* oy, float* oz)
{
    Entity* e = FindEntity(h);
    if (!e || !ox || !oy || !oz) return;
    Transform& t = e->GetTransform();
    *ox = t.LocalPosition.X;
    *oy = t.LocalPosition.Y;
    *oz = t.LocalPosition.Z;
}

static void Native_TransformSetLocalPosition(uint64_t h, float x, float y, float z)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    t.LocalPosition = Vector3(x, y, z);
    t.Position = t.LocalPosition;
}

static void Native_TransformGetWorldPosition(uint64_t h, float* ox, float* oy, float* oz)
{
    Entity* e = FindEntity(h);
    if (!e || !ox || !oy || !oz) return;
    Transform& t = e->GetTransform();
    *ox = t.Position.X;
    *oy = t.Position.Y;
    *oz = t.Position.Z;
}

static void Native_TransformSetWorldPosition(uint64_t h, float x, float y, float z)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    t.Position = Vector3(x, y, z);
    t.LocalPosition = t.Position;
}

static void Native_TransformGetLocalRotation(uint64_t h, float* ox, float* oy, float* oz, float* ow)
{
    Entity* e = FindEntity(h);
    if (!e || !ox || !oy || !oz || !ow) return;
    Transform& t = e->GetTransform();
    *ox = t.LocalRotation.x;
    *oy = t.LocalRotation.y;
    *oz = t.LocalRotation.z;
    *ow = t.LocalRotation.w;
}

static void Native_TransformSetLocalRotation(uint64_t h, float x, float y, float z, float w)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    t.LocalRotation = glm::quat(w, x, y, z);
    t.Rotation = t.LocalRotation;
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(t.LocalRotation));
    t.LocalEulerRotation = Vector3(euler.x, euler.y, euler.z);
    t.EulerRotation = t.LocalEulerRotation;
}

static void Native_TransformGetWorldRotation(uint64_t h, float* ox, float* oy, float* oz, float* ow)
{
    Entity* e = FindEntity(h);
    if (!e || !ox || !oy || !oz || !ow) return;
    Transform& t = e->GetTransform();
    *ox = t.Rotation.x;
    *oy = t.Rotation.y;
    *oz = t.Rotation.z;
    *ow = t.Rotation.w;
}

static void Native_TransformSetWorldRotation(uint64_t h, float x, float y, float z, float w)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    t.Rotation = glm::quat(w, x, y, z);
    t.LocalRotation = t.Rotation;
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(t.Rotation));
    t.EulerRotation = Vector3(euler.x, euler.y, euler.z);
    t.LocalEulerRotation = t.EulerRotation;
}

static void Native_TransformGetLocalScale(uint64_t h, float* ox, float* oy, float* oz)
{
    Entity* e = FindEntity(h);
    if (!e || !ox || !oy || !oz) return;
    Transform& t = e->GetTransform();
    *ox = t.LocalScale.X;
    *oy = t.LocalScale.Y;
    *oz = t.LocalScale.Z;
}

static void Native_TransformSetLocalScale(uint64_t h, float x, float y, float z)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    t.LocalScale = Vector3(x, y, z);
    t.Scale = t.LocalScale;
}

static void Native_TransformGetEuler(uint64_t h, float* ox, float* oy, float* oz)
{
    Entity* e = FindEntity(h);
    if (!e || !ox || !oy || !oz) return;
    Transform& t = e->GetTransform();
    const glm::vec3 euler = glm::eulerAngles(t.LocalRotation);
    *ox = euler.x;
    *oy = euler.y;
    *oz = euler.z;
}

static void Native_TransformSetEuler(uint64_t h, float x, float y, float z)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    glm::quat q = glm::normalize(glm::quat(glm::vec3(x, y, z)));
    t.Rotation = q;
    t.LocalRotation = q;
    const glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(q));
    t.EulerRotation = Vector3(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
    t.LocalEulerRotation = t.EulerRotation;
}

static void Native_TransformTranslateDelta(uint64_t h, float dx, float dy, float dz, int space)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    if (space == 0)
    {
        glm::vec3 localDelta(dx, dy, dz);
        glm::vec3 worldDelta = t.LocalRotation * localDelta;
        t.LocalPosition.X += worldDelta.x;
        t.LocalPosition.Y += worldDelta.y;
        t.LocalPosition.Z += worldDelta.z;
    }
    else
    {
        t.LocalPosition.X += dx;
        t.LocalPosition.Y += dy;
        t.LocalPosition.Z += dz;
    }
    t.Position = t.LocalPosition;
}

static void Native_TransformRotateEuler(uint64_t h, float rx, float ry, float rz, int space)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    // A API C# Transform.Rotate recebe radianos; não converter novamente
    // para graus aqui (isso deixava qualquer rotação de script 57x menor).
    const glm::quat delta = glm::quat(glm::vec3(rx, ry, rz));
    if (space == 0)
        t.LocalRotation = glm::normalize(t.LocalRotation * delta);
    else
        t.LocalRotation = glm::normalize(delta * t.LocalRotation);
    t.Rotation = t.LocalRotation;
    const glm::vec3 eulerOut = glm::degrees(glm::eulerAngles(t.LocalRotation));
    t.LocalEulerRotation = Vector3(eulerOut.x, eulerOut.y, eulerOut.z);
    t.EulerRotation = t.LocalEulerRotation;
}

static void Native_TransformLookAt(uint64_t h, float tx, float ty, float tz)
{
    Entity* e = FindEntity(h);
    if (!e) return;
    Transform& t = e->GetTransform();
    glm::vec3 pos(t.Position.X, t.Position.Y, t.Position.Z);
    glm::vec3 target(tx, ty, tz);
    glm::vec3 dir = glm::normalize(target - pos);
    if (glm::length(dir) <= 0.0001f) return;
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(dir, up)) > 0.999f)
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::mat4 lookMat = glm::lookAt(pos, target, up);
    glm::quat q = glm::quat_cast(glm::inverse(lookMat));
    t.Rotation = q;
    t.LocalRotation = q;
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
    t.EulerRotation = Vector3(euler.x, euler.y, euler.z);
    t.LocalEulerRotation = t.EulerRotation;
}

static void Native_TransformSetParent(uint64_t transformHandle, uint64_t parentEntityId, bool worldPositionStays)
{
    Entity* child = FindEntity(transformHandle);
    if (!child || !g_Scene) return;
    if (parentEntityId == 0)
        g_Scene->RemoveParent(transformHandle, worldPositionStays);
    else
        g_Scene->SetParent(transformHandle, parentEntityId, worldPositionStays);
}

static int Native_TransformGetChildCount(uint64_t transformHandle)
{
    Entity* e = FindEntity(transformHandle);
    if (!e) return 0;
    return static_cast<int>(e->GetChildrenIDs().size());
}

static uint64_t Native_TransformGetChild(uint64_t transformHandle, int index)
{
    Entity* e = FindEntity(transformHandle);
    if (!e) return 0;
    auto& children = e->GetChildrenIDs();
    if (index < 0 || index >= static_cast<int>(children.size())) return 0;
    return children[static_cast<size_t>(index)];
}

static uint64_t Native_TransformGetParent(uint64_t transformHandle)
{
    Entity* e = FindEntity(transformHandle);
    if (!e) return 0;
    return e->GetParentID();
}

static uint64_t Native_EntityFindByName(const wchar_t* name)
{
    if (!g_Scene || !name) return 0;
    std::string nameStr = WideToUtf8(name);
    for (auto& e : g_Scene->GetEntities())
    {
        if (e.GetName() == nameStr)
            return e.GetID();
    }
    return 0;
}

static uint64_t Native_EntityFindByTag(const wchar_t* tag)
{
    if (!g_Scene || !tag) return 0;
    std::string tagStr = WideToUtf8(tag);
    for (auto& e : g_Scene->GetEntities())
    {
        if (e.Tag == tagStr)
            return e.GetID();
    }
    return 0;
}

static uint64_t Native_EntityFindById(uint64_t id)
{
    Entity* e = FindEntity(id);
    return e ? id : 0;
}

static void* Native_EntityFindAllByTag(const wchar_t* tag, int* outCount)
{
    if (!outCount) return nullptr;
    *outCount = 0;
    if (!g_Scene || !tag) return nullptr;
    std::string tagStr = WideToUtf8(tag);
    s_TagResults.clear();
    for (auto& e : g_Scene->GetEntities())
    {
        if (e.Tag == tagStr)
        {
            s_TagResults.push_back(e.GetID());
        }
    }
    *outCount = static_cast<int>(s_TagResults.size());
    return s_TagResults.empty() ? nullptr : s_TagResults.data();
}

static void Native_EntityDestroy(uint64_t entityId)
{
    if (!g_Scene || !FindEntity(entityId)) return;
    if (std::find(g_PendingDestroy.begin(), g_PendingDestroy.end(), entityId) == g_PendingDestroy.end())
        g_PendingDestroy.push_back(entityId);
}

static void QueueDestroyHierarchy(uint64_t entityId)
{
    Entity* entity = FindEntity(entityId);
    if (!entity) return;
    // Copiar os IDs antes de enfileirar: a destruição só ocorre depois do
    // update e nunca invalida o vetor de entidades durante o script.
    const auto children = entity->GetChildrenIDs();
    for (uint64_t childID : children)
        QueueDestroyHierarchy(childID);
    Native_EntityDestroy(entityId);
}

static void Native_EntityDestroyHierarchy(uint64_t entityId)
{
    QueueDestroyHierarchy(entityId);
}

static void FlushPendingDestroy()
{
    if (!g_Scene || g_PendingDestroy.empty()) return;
    const auto pending = g_PendingDestroy;
    g_PendingDestroy.clear();
    for (uint64_t id : pending)
    {
        auto& entities = g_Scene->GetEntities();
        for (size_t i = 0; i < entities.size(); ++i)
        {
            if (entities[i].GetID() == id)
            {
                g_Scene->DeleteEntity(static_cast<int>(i));
                break;
            }
        }
    }
}

static uint64_t Native_EntityInstantiate(uint64_t sourceEntityId)
{
    if (!g_Scene) return 0;
    auto& entities = g_Scene->GetEntities();
    for (size_t i = 0; i < entities.size(); ++i)
    {
        if (entities[i].GetID() == sourceEntityId)
        {
            int cloneIndex = SceneCommands::DuplicateEntity(*g_Scene, static_cast<int>(i));
            if (cloneIndex >= 0 && cloneIndex < static_cast<int>(entities.size()))
            {
                return entities[static_cast<size_t>(cloneIndex)].GetID();
            }
            break;
        }
    }
    return 0;
}

static bool Native_EntityHasUI(uint64_t entityId)
{
    Entity* entity = FindEntity(entityId);
    return entity && entity->HasUI();
}

static int Native_UIGetType(uint64_t entityId)
{
    Entity* entity = FindEntity(entityId);
    return entity && entity->HasUI() ? static_cast<int>(entity->GetUIComponent().Type) : -1;
}

static bool Native_UIGetEnabled(uint64_t entityId)
{
    Entity* entity = FindEntity(entityId);
    return entity && entity->HasUI() && entity->GetUIComponent().Enabled;
}

static void Native_UISetEnabled(uint64_t entityId, bool enabled)
{
    Entity* entity = FindEntity(entityId);
    if (entity && entity->HasUI()) entity->GetUIComponent().Enabled = enabled;
}

static void Native_UIGetRect(uint64_t entityId, float* x, float* y, float* width, float* height)
{
    Entity* entity = FindEntity(entityId);
    if (!entity || !entity->HasUI()) return;
    const UIComponent& ui = entity->GetUIComponent();
    if (x) *x = ui.PositionX;
    if (y) *y = ui.PositionY;
    if (width) *width = ui.Width;
    if (height) *height = ui.Height;
}

static bool Native_UIIsPointerOver(uint64_t entityId)
{
    Entity* entity = FindEntity(entityId);
    if (!entity || !entity->HasUI()) return false;
    const UIComponent& ui = entity->GetUIComponent();
    const int type = static_cast<int>(ui.Type);
    if (!ui.Enabled || (type != 3 && type != 5)) return false; // TextButton/ImageButton

    auto* application = static_cast<Application*>(g_CurrentApplication);
    Window* window = GetInputWindow();
    if (!window) return false;
    // No editor, usa coordenadas de janela. No editor, usa as coordenadas
    // locais do framebuffer do Viewport (não as coordenadas da janela inteira).
    const bool hasViewportPointer = application && application->HasViewportPointer();
    const float mouseX = hasViewportPointer ? application->GetViewportPointerX()
                                            : static_cast<float>(window->GetMouseX());
    const float mouseY = hasViewportPointer ? application->GetViewportPointerY()
                                            : static_cast<float>(window->GetMouseY());
    const float canvasWidth = application ? static_cast<float>(application->GetViewportWidth()) : 960.0f;
    const float canvasHeight = application ? static_cast<float>(application->GetViewportHeight()) : 540.0f;
    return ui.ContainsPoint(mouseX, mouseY, canvasWidth, canvasHeight);
}

static void Native_EntityApplyImpulse(uint64_t entityId, float x, float y, float z)
{
    Entity* entity = FindEntity(entityId);
    if (!entity || !entity->PhysicsEnabled || entity->IsStatic) return;
    const float mass = std::max(0.0001f, entity->Mass);
    entity->Velocity.X += x / mass;
    entity->Velocity.Y += y / mass;
    entity->Velocity.Z += z / mass;
}

static bool Native_InputGetKey(int keycode)
{
    return keycode >= 0 && keycode <= GLFW_KEY_LAST &&
        g_KeyState[static_cast<size_t>(keycode)];
}

static bool Native_InputGetKeyDown(int keycode)
{
    return keycode >= 0 && keycode <= GLFW_KEY_LAST &&
        g_KeyState[static_cast<size_t>(keycode)] &&
        !g_PreviousKeyState[static_cast<size_t>(keycode)];
}

static bool Native_InputGetKeyUp(int keycode)
{
    return keycode >= 0 && keycode <= GLFW_KEY_LAST &&
        !g_KeyState[static_cast<size_t>(keycode)] &&
        g_PreviousKeyState[static_cast<size_t>(keycode)];
}

static bool Native_InputGetMouseButton(int button)
{
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST &&
        g_MouseState[static_cast<size_t>(button)];
}

static bool Native_InputGetMouseButtonDown(int button)
{
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST &&
        g_MouseState[static_cast<size_t>(button)] &&
        !g_PreviousMouseState[static_cast<size_t>(button)];
}

static bool Native_InputGetMouseButtonUp(int button)
{
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST &&
        !g_MouseState[static_cast<size_t>(button)] &&
        g_PreviousMouseState[static_cast<size_t>(button)];
}

static void Native_InputGetMousePos(float* ox, float* oy, float* oz)
{
    Window* window = GetInputWindow();
    if (ox) *ox = window ? static_cast<float>(window->GetMouseX()) : 0.0f;
    if (oy) *oy = window ? static_cast<float>(window->GetMouseY()) : 0.0f;
    if (oz) *oz = 0.0f;
}

static float Native_InputGetAxis(const wchar_t* axisName)
{
    if (!axisName) return 0.0f;
    const std::string axis = WideToUtf8(axisName);
    // Mouse X/Y são deltas produzidos pelo Window. Ler a fonte nativa evita
    // depender de uma cópia do InputMap quando o estado é atualizado fora do
    // ciclo normal de BeginFrame (por exemplo, em testes e hot-reload).
    if (axis == "Mouse X" || axis == "mouse x")
        return static_cast<float>(g_MouseDeltaX);
    if (axis == "Mouse Y" || axis == "mouse y")
        return static_cast<float>(g_MouseDeltaY);
    return MaraeInput::InputMap::Instance().GetAxis(axis, true);
}

static bool Native_InputGetAction(const wchar_t* actionName)
{
    return actionName && MaraeInput::InputMap::Instance().GetAction(WideToUtf8(actionName));
}

static bool Native_InputGetActionDown(const wchar_t* actionName)
{
    return actionName && MaraeInput::InputMap::Instance().GetActionDown(WideToUtf8(actionName));
}

static bool Native_InputGetActionUp(const wchar_t* actionName)
{
    return actionName && MaraeInput::InputMap::Instance().GetActionUp(WideToUtf8(actionName));
}

static void Native_InputBindKey(const wchar_t* actionName, int keyCode, float scale)
{
    if (actionName) MaraeInput::InputMap::Instance().BindKey(WideToUtf8(actionName), keyCode, scale);
}

static void Native_InputBindMouseButton(const wchar_t* actionName, int button, float scale)
{
    if (actionName) MaraeInput::InputMap::Instance().BindMouseButton(WideToUtf8(actionName), button, scale);
}

static void Native_InputClearBindings(const wchar_t* actionName)
{
    if (actionName) MaraeInput::InputMap::Instance().ClearBindings(WideToUtf8(actionName));
}

static void Native_InputSetDeadZone(const wchar_t* axisName, float deadZone)
{
    if (axisName) MaraeInput::InputMap::Instance().SetDeadZone(WideToUtf8(axisName), deadZone);
}

static float Native_TimeGetDelta()
{
    return g_DeltaTime;
}

static float Native_TimeGetUnscaledDelta()
{
    return g_TimeScale > 0.0f ? g_DeltaTime / g_TimeScale : g_DeltaTime;
}

static float Native_TimeGetTime()
{
    return static_cast<float>(g_TotalTime);
}

static float Native_TimeGetFixedDelta()
{
    return g_FixedDelta;
}

static float Native_TimeGetTimeScale()
{
    return g_TimeScale;
}

static void Native_TimeSetTimeScale(float value)
{
    g_TimeScale = std::max(0.0f, value);
}

static void Native_Log(int level, const wchar_t* utf8msg)
{
    if (!utf8msg) return;
    std::string msg = WideToUtf8(utf8msg);

    std::string file;
    const size_t sourceBegin = msg.find("SourceFile(");
    if (sourceBegin != std::string::npos)
    {
        const size_t pathBegin = sourceBegin + 11;
        const size_t pathEnd = msg.find('[', pathBegin);
        if (pathEnd != std::string::npos && pathEnd > pathBegin)
            file = msg.substr(pathBegin, pathEnd - pathBegin);
    }

    std::string object;
    std::string scene;
    if (auto* application = static_cast<Application*>(g_CurrentApplication))
    {
        const auto& entities = application->GetScene().GetEntities();
        const uint64_t selectedId = application->GetSelectedID();
        for (const auto& entity : entities)
        {
            if (entity.GetID() == selectedId)
            {
                object = entity.GetName();
                break;
            }
        }
        scene = application->GetProjectConfig().ScenePath;
    }
    MaraeDiagnostics::DiagnosticLog::Get().AddNative(
        level, msg, std::move(file), 0, std::move(object), std::move(scene));

    switch (level)
    {
    case 0: std::cout << "[Debug] " << msg << std::endl; break;
    case 1: std::cout << "[Info]  " << msg << std::endl; break;
    case 2: std::cerr << "[Warn]  " << msg << std::endl; break;
    case 3: std::cerr << "[Error] " << msg << std::endl; break;
    default: std::cout << "[Log]   " << msg << std::endl; break;
    }
}

static bool Native_EntityHasComponent(uint64_t entityId, int componentType)
{
    Entity* e = FindEntity(entityId);
    if (!e) return false;
    ComponentType ct = static_cast<ComponentType>(componentType);
    switch (ct)
    {
    case ComponentType::Mesh: return true;
    case ComponentType::Audio: return e->HasAudio();
    case ComponentType::Light: return e->HasLight();
    case ComponentType::Material: return true;
    case ComponentType::Script: return true;
    // Physics settings live on every Entity in the current ECS layout.
    case ComponentType::Physics: return true;
    case ComponentType::UI: return e->HasUI();
    case ComponentType::Animation: return true;
    case ComponentType::Camera: return e->HasCamera();
    case ComponentType::GlobalLight: return e->IsGlobalLight();
    }
    return false;
}

static uint64_t Native_EntityGetComponent(uint64_t entityId, int componentType)
{
    Entity* e = FindEntity(entityId);
    if (!e) return 0;
    if (!Native_EntityHasComponent(entityId, componentType)) return 0;
    return entityId;
}

// ---------------------------------------------------------------------------
// Mesh Component Callbacks
// ---------------------------------------------------------------------------
static const wchar_t* Native_ComponentMeshGetName(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return L"";
    s_WStrRet = Utf8ToWide(e->GetMeshComponent().MeshPath);
    return s_WStrRet.c_str();
}

static void Native_ComponentMeshSetName(uint64_t entityId, const wchar_t* name)
{
    Entity* e = FindEntity(entityId);
    if (!e || !name) return;
    e->GetMeshComponent().MeshPath = WideToUtf8(name);
}

static bool Native_ComponentMeshGetVisible(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return false;
    return e->Active;
}

static void Native_ComponentMeshSetVisible(uint64_t entityId, bool visible)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->Active = visible;
}

static void Native_ComponentMeshGetColor(uint64_t entityId, float* r, float* g, float* b, float* a)
{
    Entity* e = FindEntity(entityId);
    if (!e || !r || !g || !b || !a) return;
    *r = e->GetColorComponent().Color.X;
    *g = e->GetColorComponent().Color.Y;
    *b = e->GetColorComponent().Color.Z;
    *a = e->GetMaterialComponent().MaterialData.Color[3];
}

static void Native_ComponentMeshSetColor(uint64_t entityId, float r, float g, float b, float a)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->GetColorComponent().Color = Vector3(r, g, b);
    e->GetMaterialComponent().MaterialData.Color[0] = r;
    e->GetMaterialComponent().MaterialData.Color[1] = g;
    e->GetMaterialComponent().MaterialData.Color[2] = b;
    e->GetMaterialComponent().MaterialData.Color[3] = a;
}

// ---------------------------------------------------------------------------
// Audio Component Callbacks
// ---------------------------------------------------------------------------
static void Native_ComponentAudioPlay(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return;
    e->GetAudioComponent().PlayState = AudioPlayState::Playing;
}

static void Native_ComponentAudioStop(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return;
    e->GetAudioComponent().PlayState = AudioPlayState::Stopped;
}

static bool Native_ComponentAudioGetPlaying(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return false;
    return e->GetAudioComponent().PlayState == AudioPlayState::Playing;
}

static float Native_ComponentAudioGetVolume(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return 0.0f;
    return e->GetAudioComponent().Volume;
}

static void Native_ComponentAudioSetVolume(uint64_t entityId, float volume)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return;
    e->GetAudioComponent().Volume = std::clamp(volume, 0.0f, 10.0f);
}

static bool Native_ComponentAudioGetLoop(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return false;
    return e->GetAudioComponent().Loop;
}

static void Native_ComponentAudioSetLoop(uint64_t entityId, bool loop)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return;
    e->GetAudioComponent().Loop = loop;
}

static float Native_ComponentAudioGetPitch(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return 1.0f;
    return e->GetAudioComponent().Pitch;
}

static void Native_ComponentAudioSetPitch(uint64_t entityId, float pitch)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasAudio()) return;
    e->GetAudioComponent().Pitch = std::clamp(pitch, 0.01f, 10.0f);
}

static bool Native_AudioPlayBySourceID(const wchar_t* sourceID)
{
    if (!sourceID || !*sourceID || !g_Scene || !g_CurrentApplication)
        return false;

    const std::string requestedID = WideToUtf8(sourceID);
    auto* application = static_cast<Application*>(g_CurrentApplication);
    for (Entity& entity : g_Scene->GetEntities())
    {
        if (!entity.HasAudio())
            continue;
        AudioComponent& audio = entity.GetAudioComponent();
        // Aceita tanto o ID público da fonte quanto o GUID do AudioClip.
        // Isso permite que scripts usem o mesmo identificador mostrado no
        // AssetDatabase, sem depender do nome da entidade.
        if ((audio.SourceID != requestedID && audio.ClipID != requestedID) ||
            !audio.Enabled)
            continue;

        std::filesystem::path clipPath = audio.ResolveClipPath();
        if (clipPath.empty())
            return false;
        if (clipPath.is_relative())
            clipPath = std::filesystem::path(application->GetProjectConfig().ProjectPath) / clipPath;
        std::error_code clipPathError;
        clipPath = std::filesystem::weakly_canonical(clipPath, clipPathError);
        if (clipPathError || !std::filesystem::is_regular_file(clipPath, clipPathError))
            return false;

        if (audio.RuntimeVoiceID != AUDIO_INVALID_VOICE)
            application->GetAudioSystem().Stop(audio.RuntimeVoiceID);

        const Transform& transform = entity.GetTransform();
        AudioSourceParams params;
        params.ClipPath = clipPath.string();
        params.Position = glm::vec3(transform.Position.X, transform.Position.Y,
                                    transform.Position.Z);
        params.Velocity = glm::vec3(entity.Velocity.X, entity.Velocity.Y,
                                    entity.Velocity.Z);
        params.Volume = std::clamp(audio.Volume, 0.0f, 1.0f);
        params.Pitch = std::clamp(audio.Pitch, 0.25f, 4.0f);
        params.SpatialBlend = std::clamp(audio.SpatialBlend, 0.0f, 1.0f);
        params.MinDistance = std::max(0.01f, audio.MinDistance);
        params.MaxDistance = std::max(params.MinDistance + 0.01f, audio.MaxDistance);
        params.DopplerFactor = std::clamp(audio.DopplerFactor, 0.0f, 4.0f);
        params.Loop = audio.Loop;
        params.GroupID = audio.MixerGroupID;
        audio.RuntimeVoiceID = application->GetAudioSystem().Play(params);
        audio.PlayState = audio.RuntimeVoiceID == AUDIO_INVALID_VOICE
            ? AudioPlayState::Stopped : AudioPlayState::Playing;
        return audio.RuntimeVoiceID != AUDIO_INVALID_VOICE;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Light Component Callbacks
// ---------------------------------------------------------------------------
static int Native_ComponentLightGetType(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return 1;
    switch (e->GetLightComponent().Type)
    {
    case LightType::Directional: return 0;
    case LightType::Point: return 1;
    case LightType::Spot: return 2;
    case LightType::Area: return 3;
    default: return 1;
    }
}

static void Native_ComponentLightSetType(uint64_t entityId, int type)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return;
    switch (type)
    {
    case 0: e->GetLightComponent().Type = LightType::Directional; break;
    case 1: e->GetLightComponent().Type = LightType::Point; break;
    case 2: e->GetLightComponent().Type = LightType::Spot; break;
    case 3: e->GetLightComponent().Type = LightType::Area; break;
    default: break;
    }
}

static float Native_ComponentLightGetIntensity(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return 0.0f;
    return e->GetLightComponent().Intensity;
}

static void Native_ComponentLightSetIntensity(uint64_t entityId, float intensity)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return;
    e->GetLightComponent().Intensity = std::max(0.0f, intensity);
}

static void Native_ComponentLightGetColor(uint64_t entityId, float* r, float* g, float* b, float* a)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight() || !r || !g || !b || !a) return;
    *r = e->GetLightComponent().Color[0];
    *g = e->GetLightComponent().Color[1];
    *b = e->GetLightComponent().Color[2];
    *a = 1.0f;
}

static void Native_ComponentLightSetColor(uint64_t entityId, float r, float g, float b, float a)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return;
    e->GetLightComponent().Color[0] = r;
    e->GetLightComponent().Color[1] = g;
    e->GetLightComponent().Color[2] = b;
}

static float Native_ComponentLightGetRange(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return 0.0f;
    return e->GetLightComponent().Range;
}

static void Native_ComponentLightSetRange(uint64_t entityId, float range)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return;
    e->GetLightComponent().Range = std::clamp(range, 0.001f, 10000.0f);
}

static float Native_ComponentLightGetSpotAngle(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return 0.0f;
    return glm::radians(e->GetLightComponent().OuterAngleDeg);
}

static void Native_ComponentLightSetSpotAngle(uint64_t entityId, float angleRad)
{
    Entity* e = FindEntity(entityId);
    if (!e || !e->HasLight()) return;
    float deg = glm::clamp(glm::degrees(angleRad), 1.0f, 89.0f);
    e->GetLightComponent().OuterAngleDeg = deg;
    e->GetLightComponent().InnerAngleDeg = std::min(deg * 0.6f, deg - 0.5f);
}

// ---------------------------------------------------------------------------
// Typed component property bridge used by the public C# SDK.
// ---------------------------------------------------------------------------
static bool Native_ComponentGetBool(uint64_t entityId, int componentType, int property)
{
    Entity* e = FindEntity(entityId);
    if (!e) return false;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        switch (property)
        {
        case 0: return e->PhysicsEnabled;
        case 1: return e->UseGravity;
        case 2: return e->IsStatic;
        case 3: return e->ColliderEnabled;
        case 4: return e->ContinuousCollision;
        case 5: return e->FreezeRotation;
        case 6: return e->IsTrigger;
        }
        break;
    case ComponentType::UI:
        if (!e->HasUI()) return false;
        switch (property)
        {
        case 0: return e->GetUIComponent().Enabled;
        case 1: return e->GetUIComponent().ClipsContent;
        case 2: return e->GetUIComponent().UseResponsiveAnchors;
        case 3: return e->GetUIComponent().Checked;
        case 4: return e->GetUIComponent().DropdownOpen;
        case 5: return e->GetUIComponent().NineSlice;
        case 6: return e->GetUIComponent().ScrollViewEnabled;
        case 7: return e->GetUIComponent().IsHovered;
        case 8: return e->GetUIComponent().IsPressed;
        case 9: return e->GetUIComponent().IsFocused;
        }
        break;
    case ComponentType::Light:
        if (!e->HasLight()) return false;
        switch (property)
        {
        case 0: return e->GetLightComponent().Enabled;
        case 1: return e->GetLightComponent().CastShadows;
        case 2: return e->GetLightComponent().UseTemperature;
        case 3: return e->GetLightComponent().DistanceSoftening;
        case 4: return e->GetLightComponent().IndirectContribution;
        }
        break;
    case ComponentType::Animation:
        switch (property)
        {
        case 0: return e->GetAnimationComponent().Playing;
        case 1: return e->GetAnimationComponent().Enabled;
        case 2: return e->GetAnimationComponent().PlayOnStart;
        }
        break;
    case ComponentType::Camera:
        if (!e->HasCamera()) return false;
        switch (property)
        {
        case 0: return e->GetCameraComponent().Enabled;
        case 1: return e->GetCameraComponent().PreviewEnabled;
        case 2: return e->GetCameraComponent().Orthographic;
        }
        break;
    case ComponentType::GlobalLight:
        if (!e->IsGlobalLight()) return false;
        switch (property)
        {
        case 0: return e->GetGlobalLightComponent().Enabled;
        case 1: return e->GetGlobalLightComponent().FogEnabled;
        case 2: return e->GetGlobalLightComponent().Lighting.CloudsEnabled;
        case 3: return e->GetGlobalLightComponent().Lighting.CloudMovementEnabled;
        }
        break;
    default: break;
    }
    return false;
}

static void Native_ComponentSetBool(uint64_t entityId, int componentType, int property, bool value)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        switch (property)
        {
        case 0: e->PhysicsEnabled = value; break;
        case 1: e->UseGravity = value; break;
        case 2: e->IsStatic = value; break;
        case 3: e->ColliderEnabled = value; break;
        case 4: e->ContinuousCollision = value; break;
        case 5: e->FreezeRotation = value; break;
        case 6: e->IsTrigger = value; break;
        }
        break;
    case ComponentType::UI:
        if (!e->HasUI()) return;
        {
            UIComponent& ui = e->GetUIComponent();
            switch (property)
            {
            case 0: ui.Enabled = value; break;
            case 1: ui.ClipsContent = value; break;
            case 2: ui.UseResponsiveAnchors = value; break;
            case 3: ui.Checked = value; break;
            case 4: ui.DropdownOpen = value; break;
            case 5: ui.NineSlice = value; break;
            case 6: ui.ScrollViewEnabled = value; break;
            }
        }
        break;
    case ComponentType::Light:
        if (!e->HasLight()) return;
        switch (property)
        {
        case 0: e->GetLightComponent().Enabled = value; break;
        case 1: e->GetLightComponent().CastShadows = value; break;
        case 2: e->GetLightComponent().UseTemperature = value; break;
        case 3: e->GetLightComponent().DistanceSoftening = value; break;
        case 4: e->GetLightComponent().IndirectContribution = value; break;
        }
        break;
    case ComponentType::Animation:
        switch (property)
        {
        case 0: e->GetAnimationComponent().Playing = value; break;
        case 1: e->GetAnimationComponent().Enabled = value; break;
        case 2: e->GetAnimationComponent().PlayOnStart = value; break;
        }
        break;
    case ComponentType::Camera:
        if (!e->HasCamera()) return;
        switch (property)
        {
        case 0: e->GetCameraComponent().Enabled = value; break;
        case 1: e->GetCameraComponent().PreviewEnabled = value; break;
        case 2: e->GetCameraComponent().Orthographic = value; break;
        }
        break;
    case ComponentType::GlobalLight:
        if (!e->IsGlobalLight()) return;
        switch (property)
        {
        case 0: e->GetGlobalLightComponent().Enabled = value; break;
        case 1: e->GetGlobalLightComponent().FogEnabled = value; break;
        case 2: e->GetGlobalLightComponent().Lighting.CloudsEnabled = value; break;
        case 3: e->GetGlobalLightComponent().Lighting.CloudMovementEnabled = value; break;
        }
        break;
    default: break;
    }
}

static int Native_ComponentGetInt(uint64_t entityId, int componentType, int property)
{
    Entity* e = FindEntity(entityId);
    if (!e) return 0;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        return property == 0 ? static_cast<int>(e->Collider) : 0;
    case ComponentType::UI:
        if (!e->HasUI()) return 0;
        switch (property)
        {
        case 0: return static_cast<int>(e->GetUIComponent().Type);
        case 1: return e->GetUIComponent().ZIndex;
        case 2: return static_cast<int>(e->GetUIComponent().TextAlignment);
        case 3: return e->GetUIComponent().SelectedIndex;
        case 4: return e->GetUIComponent().HoverOptionIndex;
        case 5: return e->GetUIComponent().CaretIndex;
        }
        break;
    case ComponentType::Light:
        if (property == 0 && e->HasLight()) return Native_ComponentLightGetType(entityId);
        break;
    case ComponentType::GlobalLight:
        if (property == 0 && e->IsGlobalLight()) return e->GetGlobalLightComponent().Lighting.FogMode;
        break;
    default: break;
    }
    return 0;
}

static void Native_ComponentSetInt(uint64_t entityId, int componentType, int property, int value)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        if (property == 0 && value >= 0 && value <= 6)
            e->Collider = static_cast<Entity::ColliderShape>(value);
        break;
    case ComponentType::UI:
        if (!e->HasUI()) return;
        {
            UIComponent& ui = e->GetUIComponent();
            switch (property)
            {
            case 0:
                if (value >= 0 && value <= 11)
                    ui.Type = static_cast<UIElementType>(value);
                break;
            case 1: ui.ZIndex = value; break;
            case 2:
                if (value >= 0 && value <= 2)
                    ui.TextAlignment = static_cast<UITextAlignment>(value);
                break;
            case 3: ui.SelectedIndex = std::max(0, value); break;
            case 5: ui.CaretIndex = std::max(0, value); break;
            }
        }
        break;
    case ComponentType::Light:
        if (property == 0 && e->HasLight() && value >= 0 && value <= 3)
            Native_ComponentLightSetType(entityId, value);
        break;
    case ComponentType::GlobalLight:
        if (property == 0 && e->IsGlobalLight())
            e->GetGlobalLightComponent().Lighting.FogMode = std::clamp(value, 0, 2);
        break;
    default: break;
    }
}

static float Native_ComponentGetFloat(uint64_t entityId, int componentType, int property)
{
    Entity* e = FindEntity(entityId);
    if (!e) return 0.0f;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        switch (property)
        {
        case 0: return e->Mass;
        case 1: return e->Restitution;
        case 2: return e->Friction;
        }
        break;
    case ComponentType::UI:
        if (!e->HasUI()) return 0.0f;
        {
            const UIComponent& ui = e->GetUIComponent();
            switch (property)
            {
            case 0: return ui.PositionX;
            case 1: return ui.PositionY;
            case 2: return ui.Width;
            case 3: return ui.Height;
            case 4: return ui.AnchorX;
            case 5: return ui.AnchorY;
            case 6: return ui.Rotation;
            case 7: return ui.Transparency;
            case 8: return ui.CornerRadius;
            case 9: return ui.BorderThickness;
            case 10: return ui.FontSize;
            case 11: return ui.AnchorMinX;
            case 12: return ui.AnchorMinY;
            case 13: return ui.AnchorMaxX;
            case 14: return ui.AnchorMaxY;
            case 15: return ui.OffsetMinX;
            case 16: return ui.OffsetMinY;
            case 17: return ui.OffsetMaxX;
            case 18: return ui.OffsetMaxY;
            case 19: return ui.Value;
            case 20: return ui.MinValue;
            case 21: return ui.MaxValue;
            case 22: return ui.ScrollOffsetY;
            case 23: return ui.SliceLeft;
            case 24: return ui.SliceTop;
            case 25: return ui.SliceRight;
            case 26: return ui.SliceBottom;
            }
        }
        break;
    case ComponentType::Light:
        if (!e->HasLight()) return 0.0f;
        {
            const LightComponent& light = e->GetLightComponent();
            switch (property)
            {
            case 0: return light.Intensity;
            case 1: return light.Range;
            case 2: return glm::radians(light.OuterAngleDeg);
            case 3: return light.SourceRadius;
            case 4: return light.ShadowSoftness;
            case 5: return light.ShadowStrength;
            case 6: return light.ContactShadowStrength;
            case 7: return light.TemperatureKelvin;
            }
        }
        break;
    case ComponentType::Animation:
        switch (property)
        {
        case 0: return e->GetAnimationComponent().Time;
        case 1: return e->GetAnimationComponent().Speed;
        case 2: return e->GetAnimationComponent().Weight;
        }
        break;
    case ComponentType::Camera:
        if (!e->HasCamera()) return 0.0f;
        {
            const CameraComponent& camera = e->GetCameraComponent();
            switch (property)
            {
            case 0: return camera.FieldOfView;
            case 1: return camera.NearClip;
            case 2: return camera.FarClip;
            case 3: return camera.OrthographicSize;
            }
        }
        break;
    case ComponentType::GlobalLight:
        if (!e->IsGlobalLight()) return 0.0f;
        {
            const GlobalLightComponent& global = e->GetGlobalLightComponent();
            switch (property)
            {
            case 0: return global.Lighting.AmbientIntensity;
            case 1: return global.Lighting.FogDensity;
            case 2: return global.Lighting.FogStart;
            case 3: return global.Lighting.FogEnd;
            case 4: return global.ShadowStrength;
            case 5: return global.Lighting.SceneExposure;
            case 6: return global.Lighting.CloudAmount;
            case 7: return global.Lighting.CloudSpeed;
            }
        }
        break;
    default: break;
    }
    return 0.0f;
}

static void Native_ComponentSetFloat(uint64_t entityId, int componentType, int property, float value)
{
    Entity* e = FindEntity(entityId);
    if (!e || !std::isfinite(value)) return;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        switch (property)
        {
        case 0: e->Mass = std::max(0.0001f, value); break;
        case 1: e->Restitution = std::clamp(value, 0.0f, 1.0f); break;
        case 2: e->Friction = std::clamp(value, 0.0f, 1.0f); break;
        }
        break;
    case ComponentType::UI:
        if (!e->HasUI()) return;
        {
            UIComponent& ui = e->GetUIComponent();
            switch (property)
            {
            case 0: ui.PositionX = value; break;
            case 1: ui.PositionY = value; break;
            case 2: ui.Width = std::max(0.0f, value); break;
            case 3: ui.Height = std::max(0.0f, value); break;
            case 4: ui.AnchorX = std::clamp(value, 0.0f, 1.0f); break;
            case 5: ui.AnchorY = std::clamp(value, 0.0f, 1.0f); break;
            case 6: ui.Rotation = value; break;
            case 7: ui.Transparency = std::clamp(value, 0.0f, 1.0f); break;
            case 8: ui.CornerRadius = std::max(0.0f, value); break;
            case 9: ui.BorderThickness = std::max(0.0f, value); break;
            case 10: ui.FontSize = std::max(1.0f, value); break;
            case 11: ui.AnchorMinX = std::clamp(value, 0.0f, 1.0f); break;
            case 12: ui.AnchorMinY = std::clamp(value, 0.0f, 1.0f); break;
            case 13: ui.AnchorMaxX = std::clamp(value, 0.0f, 1.0f); break;
            case 14: ui.AnchorMaxY = std::clamp(value, 0.0f, 1.0f); break;
            case 15: ui.OffsetMinX = std::clamp(value, -100000.0f, 100000.0f); break;
            case 16: ui.OffsetMinY = std::clamp(value, -100000.0f, 100000.0f); break;
            case 17: ui.OffsetMaxX = std::clamp(value, -100000.0f, 100000.0f); break;
            case 18: ui.OffsetMaxY = std::clamp(value, -100000.0f, 100000.0f); break;
            case 19: ui.Value = std::clamp(value, ui.MinValue, ui.MaxValue); break;
            case 20: ui.MinValue = value; break;
            case 21: ui.MaxValue = value; break;
            case 22: ui.ScrollOffsetY = std::max(0.0f, value); break;
            case 23: ui.SliceLeft = std::max(0.0f, value); break;
            case 24: ui.SliceTop = std::max(0.0f, value); break;
            case 25: ui.SliceRight = std::max(0.0f, value); break;
            case 26: ui.SliceBottom = std::max(0.0f, value); break;
            }
        }
        break;
    case ComponentType::Light:
        if (!e->HasLight()) return;
        {
            LightComponent& light = e->GetLightComponent();
            switch (property)
            {
            case 0: light.Intensity = std::max(0.0f, value); break;
            case 1: light.Range = std::clamp(value, 0.001f, 10000.0f); break;
            case 2:
                light.OuterAngleDeg = std::clamp(glm::degrees(value), 1.0f, 89.0f);
                light.InnerAngleDeg = std::clamp(
                    std::min(light.InnerAngleDeg, light.OuterAngleDeg - 0.5f),
                    1.0f, light.OuterAngleDeg - 0.5f);
                break;
            case 3: light.SourceRadius = std::max(0.0f, value); break;
            case 4: light.ShadowSoftness = std::clamp(value, 0.0f, 1.0f); break;
            case 5: light.ShadowStrength = std::clamp(value, 0.0f, 1.0f); break;
            case 6: light.ContactShadowStrength = std::clamp(value, 0.0f, 1.0f); break;
            case 7: light.TemperatureKelvin = std::clamp(value, 1000.0f, 40000.0f); break;
            }
        }
        break;
    case ComponentType::Animation:
        switch (property)
        {
        case 0: e->GetAnimationComponent().Time = std::max(0.0f, value); break;
        case 1: e->GetAnimationComponent().Speed = value; break;
        case 2: e->GetAnimationComponent().Weight = std::clamp(value, 0.0f, 1.0f); break;
        }
        break;
    case ComponentType::Camera:
        if (!e->HasCamera()) return;
        {
            CameraComponent& camera = e->GetCameraComponent();
            switch (property)
            {
            case 0: camera.FieldOfView = std::clamp(value, 1.0f, 179.0f); break;
            case 1: camera.NearClip = std::max(0.001f, value); break;
            case 2: camera.FarClip = std::max(camera.NearClip + 0.01f, value); break;
            case 3: camera.OrthographicSize = std::max(0.001f, value); break;
            }
        }
        break;
    case ComponentType::GlobalLight:
        if (!e->IsGlobalLight()) return;
        {
            GlobalLightComponent& global = e->GetGlobalLightComponent();
            switch (property)
            {
            case 0: global.Lighting.AmbientIntensity = std::max(0.0f, value); break;
            case 1: global.Lighting.FogDensity = std::max(0.0f, value); break;
            case 2: global.Lighting.FogStart = std::max(0.0f, value); break;
            case 3: global.Lighting.FogEnd = std::max(global.Lighting.FogStart + 0.01f, value); break;
            case 4: global.ShadowStrength = std::clamp(value, 0.0f, 1.0f); break;
            case 5: global.Lighting.SceneExposure = value; break;
            case 6: global.Lighting.CloudAmount = std::clamp(value, 0.0f, 1.0f); break;
            case 7: global.Lighting.CloudSpeed = value; break;
            }
        }
        break;
    default: break;
    }
}

static void Native_ComponentGetVector4(uint64_t entityId, int componentType, int property,
                                       float* x, float* y, float* z, float* w)
{
    if (!x || !y || !z || !w) return;
    *x = *y = *z = 0.0f;
    *w = 1.0f;
    Entity* e = FindEntity(entityId);
    if (!e) return;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        if (property == 0) { *x = e->Velocity.X; *y = e->Velocity.Y; *z = e->Velocity.Z; *w = 0.0f; }
        else if (property == 1) { *x = e->AngularVelocity.X; *y = e->AngularVelocity.Y; *z = e->AngularVelocity.Z; *w = 0.0f; }
        break;
    case ComponentType::UI:
        if (!e->HasUI()) return;
        {
            const UIComponent& ui = e->GetUIComponent();
            const float* color = nullptr;
            switch (property)
            {
            case 0: color = ui.BackgroundColor; break;
            case 1: color = ui.TextColor; break;
            case 2: color = ui.BorderColor; break;
            case 3: color = ui.HoverColor; break;
            case 4: color = ui.PressedColor; break;
            case 5: color = ui.FillColor; break;
            case 6: color = ui.TrackColor; break;
            default: break;
            }
            if (color) { *x = color[0]; *y = color[1]; *z = color[2]; *w = color[3]; }
        }
        break;
    case ComponentType::Light:
        if (property == 0 && e->HasLight())
        {
            const auto& color = e->GetLightComponent().Color;
            *x = color[0]; *y = color[1]; *z = color[2]; *w = 1.0f;
        }
        break;
    case ComponentType::Material:
        if (property == 0)
        {
            const auto& color = e->GetMaterialComponent().MaterialData.Color;
            *x = color[0]; *y = color[1]; *z = color[2]; *w = color[3];
        }
        break;
    case ComponentType::GlobalLight:
        if (!e->IsGlobalLight()) return;
        {
            const Vector3 color = property == 0 ? e->GetGlobalLightComponent().Lighting.AmbientColor
                : property == 1 ? e->GetGlobalLightComponent().Lighting.FogColor : Vector3(0.0f, 0.0f, 0.0f);
            *x = color.X; *y = color.Y; *z = color.Z; *w = 1.0f;
        }
        break;
    default: break;
    }
}

static void Native_ComponentSetVector4(uint64_t entityId, int componentType, int property,
                                       float x, float y, float z, float w)
{
    Entity* e = FindEntity(entityId);
    if (!e || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) return;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::Physics:
        if (property == 0) e->Velocity = Vector3(x, y, z);
        else if (property == 1) e->AngularVelocity = Vector3(x, y, z);
        break;
    case ComponentType::UI:
        if (!e->HasUI()) return;
        {
            UIComponent& ui = e->GetUIComponent();
            float* color = nullptr;
            switch (property)
            {
            case 0: color = ui.BackgroundColor; break;
            case 1: color = ui.TextColor; break;
            case 2: color = ui.BorderColor; break;
            case 3: color = ui.HoverColor; break;
            case 4: color = ui.PressedColor; break;
            case 5: color = ui.FillColor; break;
            case 6: color = ui.TrackColor; break;
            default: break;
            }
            if (color)
            {
                color[0] = std::clamp(x, 0.0f, 1.0f);
                color[1] = std::clamp(y, 0.0f, 1.0f);
                color[2] = std::clamp(z, 0.0f, 1.0f);
                color[3] = std::clamp(w, 0.0f, 1.0f);
                if (property <= 2)
                    ui.Theme = 0;
            }
        }
        break;
    case ComponentType::Light:
        if (property == 0 && e->HasLight())
        {
            auto& color = e->GetLightComponent().Color;
            color[0] = std::max(0.0f, x); color[1] = std::max(0.0f, y); color[2] = std::max(0.0f, z);
        }
        break;
    case ComponentType::Material:
        if (property == 0)
        {
            auto& color = e->GetMaterialComponent().MaterialData.Color;
            color[0] = x; color[1] = y; color[2] = z; color[3] = w;
            e->GetColorComponent().Color = Vector3(x, y, z);
        }
        break;
    case ComponentType::GlobalLight:
        if (!e->IsGlobalLight()) return;
        if (property == 0) e->GetGlobalLightComponent().Lighting.AmbientColor = Vector3(x, y, z);
        else if (property == 1) e->GetGlobalLightComponent().Lighting.FogColor = Vector3(x, y, z);
        break;
    default: break;
    }
}

static const wchar_t* Native_ComponentGetString(uint64_t entityId, int componentType, int property)
{
    Entity* e = FindEntity(entityId);
    if (!e) return L"";
    std::string value;
    switch (static_cast<ComponentType>(componentType))
    {
    case ComponentType::UI:
        if (!e->HasUI()) return L"";
        if (property == 0) value = e->GetUIComponent().Text;
        else if (property == 1) value = e->GetUIComponent().Font;
        else if (property == 2) value = e->GetUIComponent().ImagePath;
        else if (property == 3)
        {
            // Opções do Dropdown, uma por linha.
            const std::vector<std::string>& options = e->GetUIComponent().Options;
            for (size_t i = 0; i < options.size(); ++i)
            {
                if (i > 0) value += '\n';
                value += options[i];
            }
        }
        break;
    case ComponentType::Animation:
        if (property == 2) value = e->GetAnimationComponent().CurrentState;
        if (e->GetAnimationComponent().Clip)
        {
            if (property == 0) value = e->GetAnimationComponent().Clip->Name;
            else if (property == 1) value = e->GetAnimationComponent().Clip->ID;
        }
        break;
    default: break;
    }
    s_WStrRet = Utf8ToWide(value);
    return s_WStrRet.c_str();
}

static void Native_ComponentSetString(uint64_t entityId, int componentType, int property, const wchar_t* value)
{
    Entity* e = FindEntity(entityId);
    if (!e || !value) return;
    if (static_cast<ComponentType>(componentType) != ComponentType::UI || !e->HasUI()) return;
    const std::string utf8 = WideToUtf8(value);
    if (property == 0) e->GetUIComponent().Text = utf8;
    else if (property == 1) e->GetUIComponent().Font = utf8;
    else if (property == 2) e->GetUIComponent().ImagePath = utf8;
    else if (property == 3)
    {
        // Opções do Dropdown, uma por linha.
        std::vector<std::string> options;
        if (!utf8.empty())
        {
            size_t start = 0;
            while (true)
            {
                const size_t pos = utf8.find('\n', start);
                options.push_back(utf8.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
                if (pos == std::string::npos) break;
                start = pos + 1;
            }
        }
        e->GetUIComponent().Options = std::move(options);
    }
}

static void Native_AnimationSetBool(uint64_t entityId, const wchar_t* parameter, bool value)
{
    Entity* entity = FindEntity(entityId);
    if (!entity || !parameter) return;
    const std::string name = WideToUtf8(parameter);
    if (name.empty()) return;
    AnimationComponent& animation = entity->GetAnimationComponent();
    const auto found = animation.BoolParameters.find(name);
    if (found != animation.BoolParameters.end()) found->second = value;
}

static void Native_ComponentInvoke(uint64_t entityId, int componentType, int action)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    if (static_cast<ComponentType>(componentType) == ComponentType::Animation)
    {
        AnimationComponent& animation = e->GetAnimationComponent();
        if (action == 0) animation.Play();
        else if (action == 1) animation.Pause();
        else if (action == 2) animation.Stop();
    }
}

// ---------------------------------------------------------------------------
// Material Component Callbacks
// ---------------------------------------------------------------------------
static void Native_ComponentMaterialGetAlbedo(uint64_t entityId, float* r, float* g, float* b, float* a)
{
    Entity* e = FindEntity(entityId);
    if (!e || !r || !g || !b || !a) return;
    *r = e->GetMaterialComponent().MaterialData.Color[0];
    *g = e->GetMaterialComponent().MaterialData.Color[1];
    *b = e->GetMaterialComponent().MaterialData.Color[2];
    *a = e->GetMaterialComponent().MaterialData.Color[3];
}

static void Native_ComponentMaterialSetAlbedo(uint64_t entityId, float r, float g, float b, float a)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->GetMaterialComponent().MaterialData.Color[0] = r;
    e->GetMaterialComponent().MaterialData.Color[1] = g;
    e->GetMaterialComponent().MaterialData.Color[2] = b;
    e->GetMaterialComponent().MaterialData.Color[3] = a;
}

static float Native_ComponentMaterialGetMetallic(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return 0.0f;
    return e->GetMaterialComponent().MaterialData.Metallic;
}

static void Native_ComponentMaterialSetMetallic(uint64_t entityId, float metallic)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->GetMaterialComponent().MaterialData.Metallic = std::clamp(metallic, 0.0f, 1.0f);
    e->GetMaterialComponent().MaterialData.PBRParametersEnabled = true;
}

static float Native_ComponentMaterialGetSmoothness(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return 0.5f;
    return 1.0f - e->GetMaterialComponent().MaterialData.Roughness;
}

static void Native_ComponentMaterialSetSmoothness(uint64_t entityId, float smoothness)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->GetMaterialComponent().MaterialData.Roughness = 1.0f - std::clamp(smoothness, 0.0f, 1.0f);
    e->GetMaterialComponent().MaterialData.PBRParametersEnabled = true;
}

// ---------------------------------------------------------------------------
// Script Component Callbacks
// ---------------------------------------------------------------------------
static bool Native_ComponentScriptGetEnabled(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return false;
    return e->GetScriptComponent().Enabled;
}

static void Native_ComponentScriptSetEnabled(uint64_t entityId, bool enabled)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    e->GetScriptComponent().Enabled = enabled;
}

static const wchar_t* Native_ComponentScriptGetTypeName(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return L"";
    s_WStrRet = Utf8ToWide(e->GetScriptComponent().ScriptAssemblyQualifiedTypeName);
    return s_WStrRet.c_str();
}

static const wchar_t* Native_ComponentScriptGetFieldsJson(uint64_t entityId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return L"{}";
    std::string json = "{";
    bool first = true;
    for (auto& [k, v] : e->GetScriptComponent().SerializedFields)
    {
        if (!first) json += ",";
        json += "\"" + k + "\":" + v;
        first = false;
    }
    json += "}";
    s_WStrRet = Utf8ToWide(json);
    return s_WStrRet.c_str();
}

static void Native_ComponentScriptSetFieldsJson(uint64_t entityId, const wchar_t* jsonW)
{
}

static bool Native_ScriptDebugShouldBreak(uint64_t entityId, const wchar_t* typeName,
                                          const wchar_t* methodName)
{
    return MaraeScripting::ScriptDebugger::Instance().ShouldBreak(
        entityId, WideToUtf8(typeName), WideToUtf8(methodName));
}

static void Native_ScriptDebugReportException(uint64_t entityId, const wchar_t* typeName,
                                              const wchar_t* methodName, const wchar_t* message,
                                              const wchar_t* stackTrace,
                                              const wchar_t* variablesJson)
{
    MaraeScripting::ScriptDebugger::Instance().ReportException(
        entityId, WideToUtf8(typeName), WideToUtf8(methodName), WideToUtf8(message),
        WideToUtf8(stackTrace), WideToUtf8(variablesJson));
}

static void Native_ScriptDebugReportBreakpoint(uint64_t entityId, const wchar_t* typeName,
                                               const wchar_t* methodName,
                                               const wchar_t* variablesJson)
{
    MaraeScripting::ScriptDebugger::Instance().ReportBreakpoint(
        entityId, WideToUtf8(typeName), WideToUtf8(methodName), WideToUtf8(variablesJson));
}

ScriptingRuntime& ScriptingRuntime::Instance()
{
    static ScriptingRuntime s_Instance;
    return s_Instance;
}

void ScriptingRuntime::PopulateNativeAPI()
{
    NativeAPI& api = m_NativeAPI;
    api.Log = Native_Log;
    api.EntityCount = Native_EntityCount;
    api.EntityGetID = Native_EntityGetID;
    api.EntityGetName = Native_EntityGetName;
    api.EntitySetName = Native_EntitySetName;
    api.EntityGetActive = Native_EntityGetActive;
    api.EntitySetActive = Native_EntitySetActive;
    api.EntityGetTag = Native_EntityGetTag;
    api.EntitySetTag = Native_EntitySetTag;
    api.EntityGetLayer = Native_EntityGetLayer;
    api.EntitySetLayer = Native_EntitySetLayer;
    api.EntityGetTransform = Native_EntityGetTransform;
    api.TransformGetLocalPosition = Native_TransformGetLocalPosition;
    api.TransformSetLocalPosition = Native_TransformSetLocalPosition;
    api.TransformGetWorldPosition = Native_TransformGetWorldPosition;
    api.TransformSetWorldPosition = Native_TransformSetWorldPosition;
    api.TransformGetLocalRotation = Native_TransformGetLocalRotation;
    api.TransformSetLocalRotation = Native_TransformSetLocalRotation;
    api.TransformGetWorldRotation = Native_TransformGetWorldRotation;
    api.TransformSetWorldRotation = Native_TransformSetWorldRotation;
    api.TransformGetLocalScale = Native_TransformGetLocalScale;
    api.TransformSetLocalScale = Native_TransformSetLocalScale;
    api.TransformGetEuler = Native_TransformGetEuler;
    api.TransformSetEuler = Native_TransformSetEuler;
    api.TransformTranslateDelta = Native_TransformTranslateDelta;
    api.TransformRotateEuler = Native_TransformRotateEuler;
    api.TransformLookAt = Native_TransformLookAt;
    api.TransformSetParent = Native_TransformSetParent;
    api.TransformGetChildCount = Native_TransformGetChildCount;
    api.TransformGetChild = Native_TransformGetChild;
    api.TransformGetParent = Native_TransformGetParent;
    api.EntityFindByName = Native_EntityFindByName;
    api.EntityFindByTag = Native_EntityFindByTag;
    api.EntityFindById = Native_EntityFindById;
    api.EntityFindAllByTag = Native_EntityFindAllByTag;
    api.EntityDestroy = Native_EntityDestroy;
    api.EntityDestroyHierarchy = Native_EntityDestroyHierarchy;
    api.EntityInstantiate = Native_EntityInstantiate;
    api.EntityHasUI = Native_EntityHasUI;
    api.UIGetType = Native_UIGetType;
    api.UIGetEnabled = Native_UIGetEnabled;
    api.UISetEnabled = Native_UISetEnabled;
    api.UIGetRect = Native_UIGetRect;
    api.UIIsPointerOver = Native_UIIsPointerOver;
    api.EntityApplyImpulse = Native_EntityApplyImpulse;
    api.InputGetKey = Native_InputGetKey;
    api.InputGetKeyDown = Native_InputGetKeyDown;
    api.InputGetKeyUp = Native_InputGetKeyUp;
    api.InputGetMouseButton = Native_InputGetMouseButton;
    api.InputGetMouseButtonDown = Native_InputGetMouseButtonDown;
    api.InputGetMouseButtonUp = Native_InputGetMouseButtonUp;
    api.InputGetMousePos = Native_InputGetMousePos;
    api.InputGetAxis = Native_InputGetAxis;
    api.InputGetAction = Native_InputGetAction;
    api.InputGetActionDown = Native_InputGetActionDown;
    api.InputGetActionUp = Native_InputGetActionUp;
    api.InputBindKey = Native_InputBindKey;
    api.InputBindMouseButton = Native_InputBindMouseButton;
    api.InputClearBindings = Native_InputClearBindings;
    api.InputSetDeadZone = Native_InputSetDeadZone;
    api.TimeGetDelta = Native_TimeGetDelta;
    api.TimeGetUnscaledDelta = Native_TimeGetUnscaledDelta;
    api.TimeGetTime = Native_TimeGetTime;
    api.TimeGetFixedDelta = Native_TimeGetFixedDelta;
    api.TimeGetTimeScale = Native_TimeGetTimeScale;
    api.TimeSetTimeScale = Native_TimeSetTimeScale;
    api.EntityHasComponent = Native_EntityHasComponent;
    api.EntityGetComponent = Native_EntityGetComponent;

    // Mesh
    api.ComponentMeshGetName = Native_ComponentMeshGetName;
    api.ComponentMeshSetName = Native_ComponentMeshSetName;
    api.ComponentMeshGetVisible = Native_ComponentMeshGetVisible;
    api.ComponentMeshSetVisible = Native_ComponentMeshSetVisible;
    api.ComponentMeshGetColor = Native_ComponentMeshGetColor;
    api.ComponentMeshSetColor = Native_ComponentMeshSetColor;

    // Audio
    api.ComponentAudioPlay = Native_ComponentAudioPlay;
    api.ComponentAudioStop = Native_ComponentAudioStop;
    api.ComponentAudioGetPlaying = Native_ComponentAudioGetPlaying;
    api.ComponentAudioGetVolume = Native_ComponentAudioGetVolume;
    api.ComponentAudioSetVolume = Native_ComponentAudioSetVolume;
    api.ComponentAudioGetLoop = Native_ComponentAudioGetLoop;
    api.ComponentAudioSetLoop = Native_ComponentAudioSetLoop;
    api.ComponentAudioGetPitch = Native_ComponentAudioGetPitch;
    api.ComponentAudioSetPitch = Native_ComponentAudioSetPitch;
    api.AudioPlayBySourceID = Native_AudioPlayBySourceID;

    // Light
    api.ComponentLightGetType = Native_ComponentLightGetType;
    api.ComponentLightSetType = Native_ComponentLightSetType;
    api.ComponentLightGetIntensity = Native_ComponentLightGetIntensity;
    api.ComponentLightSetIntensity = Native_ComponentLightSetIntensity;
    api.ComponentLightGetColor = Native_ComponentLightGetColor;
    api.ComponentLightSetColor = Native_ComponentLightSetColor;
    api.ComponentLightGetRange = Native_ComponentLightGetRange;
    api.ComponentLightSetRange = Native_ComponentLightSetRange;
    api.ComponentLightGetSpotAngle = Native_ComponentLightGetSpotAngle;
    api.ComponentLightSetSpotAngle = Native_ComponentLightSetSpotAngle;

    // Material
    api.ComponentMaterialGetAlbedo = Native_ComponentMaterialGetAlbedo;
    api.ComponentMaterialSetAlbedo = Native_ComponentMaterialSetAlbedo;
    api.ComponentMaterialGetMetallic = Native_ComponentMaterialGetMetallic;
    api.ComponentMaterialSetMetallic = Native_ComponentMaterialSetMetallic;
    api.ComponentMaterialGetSmoothness = Native_ComponentMaterialGetSmoothness;
    api.ComponentMaterialSetSmoothness = Native_ComponentMaterialSetSmoothness;

    // Script
    api.ComponentScriptGetEnabled = Native_ComponentScriptGetEnabled;
    api.ComponentScriptSetEnabled = Native_ComponentScriptSetEnabled;
    api.ComponentScriptGetTypeName = Native_ComponentScriptGetTypeName;
    api.ComponentScriptGetFieldsJson = Native_ComponentScriptGetFieldsJson;
    api.ComponentScriptSetFieldsJson = Native_ComponentScriptSetFieldsJson;

    api.ComponentGetBool = Native_ComponentGetBool;
    api.ComponentSetBool = Native_ComponentSetBool;
    api.ComponentGetInt = Native_ComponentGetInt;
    api.ComponentSetInt = Native_ComponentSetInt;
    api.ComponentGetFloat = Native_ComponentGetFloat;
    api.ComponentSetFloat = Native_ComponentSetFloat;
    api.ComponentGetVector4 = Native_ComponentGetVector4;
    api.ComponentSetVector4 = Native_ComponentSetVector4;
    api.ComponentGetString = Native_ComponentGetString;
    api.ComponentSetString = Native_ComponentSetString;
    api.ComponentInvoke = Native_ComponentInvoke;
    api.NavigationFindPath = Native_NavigationFindPath;
    api.AnimationSetBool = Native_AnimationSetBool;
    api.ScriptDebugShouldBreak = Native_ScriptDebugShouldBreak;
    api.ScriptDebugReportException = Native_ScriptDebugReportException;
    api.ScriptDebugReportBreakpoint = Native_ScriptDebugReportBreakpoint;
}

#ifdef _WIN32

#ifndef CORECLR_DELEGATE_CALLTYPE
#define CORECLR_DELEGATE_CALLTYPE __stdcall
#endif

#ifndef UNMANAGEDCALLERSONLY_METHOD
#define UNMANAGEDCALLERSONLY_METHOD ((const wchar_t*)-1)
#endif

typedef int32_t(CORECLR_DELEGATE_CALLTYPE* get_hostfxr_path_fn)(
    wchar_t* buffer,
    size_t* buffer_size,
    const void* parameters);

typedef int32_t(CORECLR_DELEGATE_CALLTYPE* hostfxr_initialize_for_runtime_config_fn)(
    const wchar_t* runtime_config_path,
    const void* parameters,
    void** hostfxr_handle);

typedef int32_t(CORECLR_DELEGATE_CALLTYPE* hostfxr_get_runtime_delegate_fn)(
    void* hostfxr_handle,
    int32_t type,
    void** delegate);

typedef int32_t(CORECLR_DELEGATE_CALLTYPE* hostfxr_close_fn)(
    void* hostfxr_handle);

typedef int32_t(CORECLR_DELEGATE_CALLTYPE* load_assembly_and_get_function_pointer_fn)(
    const wchar_t* assembly_path,
    const wchar_t* type_name,
    const wchar_t* method_name,
    const wchar_t* delegate_type_name,
    void* reserved,
    void** function_pointer);

// Valores oficiais do hostfxr.h do dotnet/runtime:
// hdt_com_activation = 0
// hdt_load_in_memory_assembly = 1
// hdt_winrt_activation = 2
// hdt_com_register = 3
// hdt_com_unregister = 4
// hdt_load_assembly_and_get_function_pointer = 5  <-- CORRETO
// hdt_get_function_pointer = 6
// hdt_load_assembly = 7
// hdt_load_assembly_bytes = 8
enum hostfxr_delegate_type
{
    hdt_com_activation = 0,
    hdt_load_in_memory_assembly = 1,
    hdt_winrt_activation = 2,
    hdt_com_register = 3,
    hdt_com_unregister = 4,
    hdt_load_assembly_and_get_function_pointer = 5,
    hdt_get_function_pointer = 6,
    hdt_load_assembly = 7,
    hdt_load_assembly_bytes = 8,
};

static std::filesystem::path GetExeDirectory()
{
    wchar_t buffer[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

static std::filesystem::path GetProjectScriptsDirectory(const std::string& projectRoot)
{
    const std::filesystem::path root(projectRoot);
    // Passa a raiz para o compilador gerenciado, que prioriza Assets/** e só
    // usa Scripts/** na raiz como fallback para projetos legados. Compilar as
    // duas árvores juntas causa definições duplicadas de classes de script.
    if (std::filesystem::is_directory(root / "Assets") ||
        std::filesystem::is_directory(root / "Scripts"))
        return root;
    return {};
}

static std::filesystem::path GetProjectScriptOutputDirectory(const std::string& projectRoot)
{
    return std::filesystem::path(projectRoot) / "Library" / "ScriptRuntime";
}

#endif

bool ScriptingRuntime::InitializeHostFxr(const std::string& projectRoot)
{
#ifdef _WIN32
    const std::filesystem::path exeDir = GetExeDirectory();

    const std::vector<std::filesystem::path> nethostCandidates = {
        exeDir / "nethost.dll",
        exeDir.parent_path() / "nethost.dll",
        std::filesystem::path(projectRoot) / "nethost.dll"
    };

    for (const auto& candidate : nethostCandidates)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error) || error)
            continue;
        const std::filesystem::path path = std::filesystem::canonical(candidate, error);
        if (error) continue;
        m_NetHostModule = (void*)LoadLibraryW(path.c_str());
        if (m_NetHostModule) break;
    }

    HMODULE hostfxr_lib = nullptr;
    if (m_NetHostModule)
    {
        auto get_hostfxr_path = (get_hostfxr_path_fn)GetProcAddress(
            (HMODULE)m_NetHostModule, "get_hostfxr_path");
        if (get_hostfxr_path)
        {
            wchar_t hostfxr_path[MAX_PATH] = { 0 };
            size_t buffer_size = sizeof(hostfxr_path) / sizeof(wchar_t);
            int rc = get_hostfxr_path(hostfxr_path, &buffer_size, nullptr);
            if (rc == 0)
            {
                hostfxr_lib = LoadLibraryW(hostfxr_path);
            }
        }
    }

    if (!hostfxr_lib)
        hostfxr_lib = LoadLibraryW(L"hostfxr.dll");

    if (!hostfxr_lib)
    {
        std::cerr << "[Scripting] Nao foi possivel carregar hostfxr.dll" << std::endl;
        return false;
    }

    auto init_for_cfg_fn = (hostfxr_initialize_for_runtime_config_fn)GetProcAddress(
        hostfxr_lib, "hostfxr_initialize_for_runtime_config");
    auto get_delegate_fn = (hostfxr_get_runtime_delegate_fn)GetProcAddress(
        hostfxr_lib, "hostfxr_get_runtime_delegate");

    if (!init_for_cfg_fn || !get_delegate_fn)
    {
        std::cerr << "[Scripting] Procs hostfxr nao encontrados na DLL" << std::endl;
        return false;
    }

    std::filesystem::path runtimeConfig;
    const std::vector<std::filesystem::path> configCandidates = {
        // A project may ship a pinned scripting runtime; prefer it over the
        // editor-wide fallback so tests and exported projects use their own SDK.
        std::filesystem::path(projectRoot) / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json",
        std::filesystem::path(projectRoot) / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json",
        exeDir / ".." / ".." / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json",
        exeDir / ".." / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json",
        exeDir / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json",
        std::filesystem::path(projectRoot) / ".." / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json",
        std::filesystem::current_path() / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json",
        std::filesystem::current_path() / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json"
    };

    for (const auto& p : configCandidates)
    {
        if (std::filesystem::exists(p))
        {
            runtimeConfig = std::filesystem::canonical(p);
            break;
        }
    }

    if (runtimeConfig.empty())
    {
        std::cerr << "[Scripting] runtimeconfig.json nao encontrado!" << std::endl;
        return false;
    }

    std::wstring runtimeConfigW = runtimeConfig.wstring();
    int32_t rc = init_for_cfg_fn(runtimeConfigW.c_str(), nullptr, &m_HostFxrContext);
    if (rc != 0 || !m_HostFxrContext)
    {
        std::cerr << "[Scripting] hostfxr_initialize_for_runtime_config falhou (rc=" << rc << ")" << std::endl;
        return false;
    }

    load_assembly_and_get_function_pointer_fn loadAssemblyFn = nullptr;
    rc = get_delegate_fn(m_HostFxrContext, hdt_load_assembly_and_get_function_pointer,
        (void**)&loadAssemblyFn);
    if (rc != 0 || !loadAssemblyFn)
    {
        std::cerr << "[Scripting] get_runtime_delegate falhou (rc=" << rc << ")" << std::endl;
        return false;
    }

    std::filesystem::path assemblyPath;
    const std::vector<std::filesystem::path> assemblyCandidates = {
        std::filesystem::path(projectRoot) / "ScriptRuntime" / "MaraeEngine.Scripting.dll",
        std::filesystem::path(projectRoot) / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.dll",
        exeDir / ".." / ".." / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.dll",
        exeDir / ".." / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.dll",
        exeDir / "ScriptRuntime" / "MaraeEngine.Scripting.dll",
        std::filesystem::path(projectRoot) / ".." / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.dll",
        std::filesystem::current_path() / "ScriptRuntime" / "MaraeEngine.Scripting.dll",
        std::filesystem::current_path() / ".." / "ScriptRuntime" / "MaraeEngine.Scripting.dll"
    };

    for (const auto& p : assemblyCandidates)
    {
        if (std::filesystem::exists(p))
        {
            assemblyPath = std::filesystem::canonical(p);
            break;
        }
    }

    if (assemblyPath.empty())
    {
        std::cerr << "[Scripting] Assembly nao encontrado: MaraeEngine.Scripting.dll" << std::endl;
        return false;
    }

    std::wstring assemblyPathW = assemblyPath.wstring();
    // Bootstrap pertence ao namespace MaraeEngine.Internal. O namespace
    // MaraeEngine.Scripting era uma referência antiga e fazia o hostfxr
    // retornar COR_E_TYPELOAD, desativando todos os scripts gerenciados.
    const wchar_t* bootstrapType =
        L"MaraeEngine.Internal.Bootstrap, MaraeEngine.Scripting";
    const wchar_t* bootstrapMethod = L"InitializeBootstrap";

    void* initPtr = nullptr;
    rc = loadAssemblyFn(
        assemblyPathW.c_str(),
        bootstrapType,
        bootstrapMethod,
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        &initPtr);

    if (rc != 0 || !initPtr)
    {
        std::cerr << "[Scripting] Nao foi possivel obter InitializeBootstrap (rc=" << rc << ")" << std::endl;
        return false;
    }

    m_InitializeBootstrap = reinterpret_cast<InitializeBootstrapFn>(initPtr);
    m_HostFxrLoaded = true;
    std::cout << "[Scripting] HostFxr (.NET 8) inicializado com sucesso!" << std::endl;
    return true;
#else
    return false;
#endif
}

bool ScriptingRuntime::LoadManagedAssembly(const std::string& projectRoot)
{
    if (!m_InitializeBootstrap)
        return false;

    ManagedAPI* result = m_InitializeBootstrap(&m_NativeAPI);
    if (!result)
    {
        std::cerr << "[Scripting] InitializeBootstrap retornou nullptr" << std::endl;
        return false;
    }

    m_ManagedAPI = *result;

    if (m_ManagedAPI.ReloadScripts)
    {
        const std::filesystem::path scriptsDir = GetProjectScriptsDirectory(projectRoot);
        const std::filesystem::path outputDir = GetProjectScriptOutputDirectory(projectRoot);
        if (!scriptsDir.empty() && std::filesystem::exists(scriptsDir))
        {
            std::error_code error;
            std::filesystem::create_directories(outputDir, error);
            if (error)
            {
                std::cerr << "[Scripting] Não foi possível criar a pasta de saída dos scripts: "
                          << error.message() << std::endl;
                return true;
            }
            std::wstring sW = scriptsDir.wstring();
            std::wstring oW = outputDir.wstring();
            int compRes = m_ManagedAPI.ReloadScripts(sW.c_str(), oW.c_str());
            std::cout << "[Scripting] Compilacao de scripts de usuario: " << (compRes == 0 ? "OK" : "Status " + std::to_string(compRes)) << std::endl;
        }
    }

    return true;
}

bool ScriptingRuntime::Initialize(const std::string& projectRoot)
{
    if (m_Initialized)
        return true;

    m_ProjectRoot = projectRoot;
    MaraeInput::InputMap::Instance().Load(projectRoot);
    PopulateNativeAPI();

    if (!InitializeHostFxr(projectRoot))
    {
        std::cerr << "[Scripting] HostFxr nao disponivel; scripting managed desativado." << std::endl;
    }
    else
    {
        if (!LoadManagedAssembly(projectRoot))
        {
            std::cerr << "[Scripting] Falha ao carregar assembly gerenciado." << std::endl;
        }
    }

    m_Initialized = true;
    return true;
}

void ScriptingRuntime::Shutdown()
{
    if (!m_Initialized) return;
    MaraeScripting::ScriptDebugger::Instance().ClearSession();
    DestroyAllScriptInstances();
#ifdef _WIN32
    auto close_fn = (hostfxr_close_fn)GetProcAddress((HMODULE)m_NetHostModule, "hostfxr_close");
    if (close_fn && m_HostFxrContext)
    {
        close_fn(m_HostFxrContext);
        m_HostFxrContext = nullptr;
    }
    if (m_NetHostModule)
    {
        FreeLibrary((HMODULE)m_NetHostModule);
        m_NetHostModule = nullptr;
    }
#endif
    m_CurrentScene = nullptr;
    m_Initialized = false;
    m_HostFxrLoaded = false;
}

void ScriptingRuntime::OnEnterPlay(Scene* scene)
{
    MaraeScripting::ScriptDebugger::Instance().ClearSession();
    m_CurrentScene = scene;
    g_Scene = scene;
    g_PendingDestroy.clear();
    m_EnabledLastFrame.clear();
    if (scene && !m_NavigationSystem.Bake(*scene))
        std::cout << "[Navigation] " << m_NavigationSystem.GetLastError() << "\n";
    CreateScriptInstancesForScene();
    // OnEnable antes de OnStart para todas as instÃ¢ncias recÃ©m-criadas
    for (auto& [entityId, instanceHandle] : m_EntityToInstance)
    {
        if (instanceHandle && m_ManagedAPI.CallOnEnable)
            m_ManagedAPI.CallOnEnable(instanceHandle);
    }
    CallOnStartForNewInstances();
}

std::string ScriptingRuntime::FindNavigationPath(float sx, float sy, float sz, float dx, float dy, float dz)
{
    const NavigationPath path = m_NavigationSystem.FindPath(glm::vec3(sx, sy, sz), glm::vec3(dx, dy, dz));
    if (!path.Found) return {};
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(7);
    for (size_t i = 0; i < path.Points.size(); ++i)
    {
        if (i) output << ';';
        output << path.Points[i].x << ',' << path.Points[i].y << ',' << path.Points[i].z;
    }
    return output.str();
}

void ScriptingRuntime::OnExitPlay()
{
    // OnDisable + OnDestroy para todas as instÃ¢ncias ativas
    for (auto& [entityId, instanceHandle] : m_EntityToInstance)
    {
        if (!instanceHandle) continue;
        if (m_ManagedAPI.CallOnDisable)
            m_ManagedAPI.CallOnDisable(instanceHandle);
    }
    DestroyAllScriptInstances();
    m_CurrentScene = nullptr;
    g_Scene = nullptr;
    m_EnabledLastFrame.clear();
    g_PendingDestroy.clear();
    MaraeScripting::ScriptDebugger::Instance().ClearSession();
}

void ScriptingRuntime::BeginFrame(float dt)
{
    RefreshInputState();
    g_DeltaTime = dt;
    g_TotalTime += dt;
    PerformanceMonitor::BeginScriptProfileFrame();
}

void ScriptingRuntime::OnFixedUpdate(float fixedDt)
{
    if (MaraeScripting::ScriptDebugger::Instance().IsPaused())
        return;
    if (!m_CurrentScene || !m_ManagedAPI.CallOnFixedUpdate)
        return;

    g_FixedDelta = std::max(0.0f, fixedDt);
    CallOnStartForNewInstances();

    // Snapshot IDs: a script may instantiate/delete entities during a callback.
    auto& entityIds = m_UpdateEntityIDsScratch;
    entityIds.clear();
    entityIds.reserve(m_EntityToInstance.size());
    for (const auto& [entityId, instance] : m_EntityToInstance)
        if (instance) entityIds.push_back(entityId);

    for (const uint64_t entityId : entityIds)
    {
        Entity* entity = m_CurrentScene->FindEntityByID(entityId);
        auto instanceIt = m_EntityToInstance.find(entityId);
        if (!entity || instanceIt == m_EntityToInstance.end() || !instanceIt->second ||
            !entity->Active || !entity->GetScriptComponent().Enabled)
            continue;

        const bool profileScripts = PerformanceMonitor::IsScriptProfilingEnabled();
        const auto scriptStart = profileScripts ? std::chrono::steady_clock::now()
                                                : std::chrono::steady_clock::time_point{};
        const int result = m_ManagedAPI.CallOnFixedUpdate(instanceIt->second, g_FixedDelta);
        if (profileScripts)
        {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - scriptStart).count();
            PerformanceMonitor::RecordScriptTime(
                entity->GetScriptComponent().ScriptPath + " (OnFixedUpdate)", elapsedMs, entityId);
        }
        if (result < 0)
            std::cerr << "[Scripting] FixedUpdate falhou para a entidade " << entityId << std::endl;
    }
    FlushPendingDestroy();
}

void ScriptingRuntime::OnUpdate(float dt)
{
    g_DeltaTime = dt;

    // Alimenta o profiler com contadores do GC gerenciado (heap e coletas).
    // Uma chamada por frame; o custo é irrelevante perto dos callbacks de script.
    if (m_ManagedAPI.GetGCStats)
    {
        int gen0 = 0, gen1 = 0, gen2 = 0;
        long long heapBytes = 0;
        if (m_ManagedAPI.GetGCStats(&gen0, &gen1, &gen2, &heapBytes) == 0)
            PerformanceMonitor::RecordGCStats(gen0, gen1, gen2, heapBytes);
    }

    if (MaraeScripting::ScriptDebugger::Instance().IsPaused())
        return;
    if (!m_CurrentScene) return;

    // Cria instÃ¢ncias para entidades adicionadas durante o Play
    CallOnStartForNewInstances();

    auto& entityIds = m_UpdateEntityIDsScratch;
    entityIds.clear();
    entityIds.reserve(m_EntityToInstance.size());
    for (const auto& [entityId, instance] : m_EntityToInstance)
        if (instance) entityIds.push_back(entityId);

    for (const uint64_t entityId : entityIds)
    {
        Entity* entity = m_CurrentScene->FindEntityByID(entityId);
        if (!entity) continue;
        auto it = m_EntityToInstance.find(entityId);
        if (it == m_EntityToInstance.end()) continue;

        uint64_t instanceHandle = it->second;
        if (!instanceHandle) continue;

        ScriptComponent& sc = entity->GetScriptComponent();
        bool enabledNow = sc.Enabled && entity->Active;

        // Detecta mudanÃ§a de estado Enabled â€” chama OnEnable/OnDisable
        auto prevIt = m_EnabledLastFrame.find(entityId);
        bool enabledPrev = (prevIt == m_EnabledLastFrame.end()) ? true : prevIt->second;

        if (enabledNow != enabledPrev)
        {
            if (enabledNow && m_ManagedAPI.CallOnEnable)
                m_ManagedAPI.CallOnEnable(instanceHandle);
            else if (!enabledNow && m_ManagedAPI.CallOnDisable)
                m_ManagedAPI.CallOnDisable(instanceHandle);
        }
        m_EnabledLastFrame[entityId] = enabledNow;

        if (!enabledNow) continue;

        if (m_ManagedAPI.CallOnUpdate)
        {
            const bool profileScripts = PerformanceMonitor::IsScriptProfilingEnabled();
            const auto scriptStart = profileScripts ? std::chrono::steady_clock::now()
                                                    : std::chrono::steady_clock::time_point{};
            const int managedResult = m_ManagedAPI.CallOnUpdate(instanceHandle, dt);
            if (profileScripts)
            {
                const double elapsedMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - scriptStart).count();
                PerformanceMonitor::RecordScriptTime(
                    sc.ScriptPath + " (OnUpdate)", elapsedMs, entityId);
            }
            if (managedResult < 0)
            {
                std::cerr << "[Scripting] OnUpdate falhou para a entidade "
                          << entityId << "; script ignorado após exceção gerenciada.\n";
            }
        }
    }

    FlushPendingDestroy();
    PerformanceMonitor::FinalizeScriptProfileFrame();
}

void ScriptingRuntime::ReloadAll()
{
    if (!m_HostFxrLoaded) return;
    DestroyAllScriptInstances();
    if (m_ManagedAPI.ReloadScripts)
    {
        const std::filesystem::path scriptsDir = GetProjectScriptsDirectory(m_ProjectRoot);
        const std::filesystem::path outputDir = GetProjectScriptOutputDirectory(m_ProjectRoot);
        std::error_code error;
        std::filesystem::create_directories(outputDir, error);
        if (error || scriptsDir.empty())
            return;
        std::wstring sW = scriptsDir.wstring();
        std::wstring oW = outputDir.wstring();
        m_ManagedAPI.ReloadScripts(sW.c_str(), oW.c_str());
    }
    CreateScriptInstancesForScene();
    CallOnStartForNewInstances();
}

std::string ScriptingRuntime::CompileProjectScripts()
{
    if (!m_HostFxrLoaded || !m_ManagedAPI.CompileScriptsFolder)
        return "ERRO: runtime C# (.NET) indisponível. Confira a instalação do runtime da Marae.";

    const std::filesystem::path scriptsDir = GetProjectScriptsDirectory(m_ProjectRoot);
    const std::filesystem::path outputDir = GetProjectScriptOutputDirectory(m_ProjectRoot);
    if (scriptsDir.empty())
        return "ERRO: pasta de scripts do projeto não encontrada.";

    std::error_code error;
    std::filesystem::create_directories(outputDir, error);
    if (error)
        return "ERRO: não foi possível preparar a pasta de compilação: " + error.message();

    void* diagnostics = nullptr;
    int errorCount = 0;
    const std::wstring scriptsWide = scriptsDir.wstring();
    const std::wstring outputWide = outputDir.wstring();
    const int result = m_ManagedAPI.CompileScriptsFolder(
        scriptsWide.c_str(), outputWide.c_str(), &diagnostics, &errorCount);

    std::string output;
    if (diagnostics)
    {
        output = WideToUtf8(static_cast<const wchar_t*>(diagnostics));
        if (m_ManagedAPI.FreeManagedBuffer)
            m_ManagedAPI.FreeManagedBuffer(diagnostics);
    }
    if (result == 0 && output.empty())
        return "OK: compilação concluída sem erros ou avisos.";
    if (result == 0)
        return "AVISOS DE COMPILAÇÃO:\n" + output;
    if (output.empty())
        output = "O compilador retornou status " + std::to_string(result) +
            " e " + std::to_string(errorCount) + " erro(s), sem detalhes.";
    return "ERROS DE COMPILAÇÃO:\n" + output;
}

int ScriptingRuntime::RunWASDInputRegressionTests(Scene& scene, const std::string& entityName)
{
    if (!m_Initialized || !m_HostFxrLoaded)
    {
        std::cerr << "[WASD-Test] Runtime .NET indisponível.\n";
        return 1;
    }

    Entity* car = nullptr;
    for (Entity& entity : scene.GetEntities())
        if (entity.GetName() == entityName) { car = &entity; break; }
    if (!car || car->GetScriptComponent().ScriptPath.empty())
    {
        std::cerr << "[WASD-Test] Entidade/script não encontrado: " << entityName << "\n";
        return 1;
    }

    const Vector3 startPosition = car->GetTransform().Position;
    const glm::quat startRotation = car->GetTransform().Rotation;
    OnEnterPlay(&scene);
    if (m_EntityToInstance.find(car->GetID()) == m_EntityToInstance.end())
    {
        std::cerr << "[WASD-Test] Script não criou instância gerenciada.\n";
        OnExitPlay();
        return 1;
    }

    constexpr float testDelta = 0.25f;
    int failures = 0;
    auto runCase = [&](int primaryKey, int steeringKey, const char* label,
                       bool expectForward, int expectedTurnSign)
    {
        Transform& transform = car->GetTransform();
        transform.Position = transform.LocalPosition = startPosition;
        transform.Rotation = transform.LocalRotation = startRotation;
        transform.EulerRotation = transform.LocalEulerRotation = Vector3(0.0f, 0.0f, 0.0f);
        g_KeyState.fill(false);
        g_PreviousKeyState.fill(false);
        if (primaryKey >= 0) g_KeyState[static_cast<size_t>(primaryKey)] = true;
        if (steeringKey >= 0) g_KeyState[static_cast<size_t>(steeringKey)] = true;

        OnUpdate(testDelta);
        const Vector3 moved = car->GetTransform().Position;
        const glm::quat turned = car->GetTransform().Rotation;
        const bool translationPassed = expectForward
            ? moved.Z > startPosition.Z + 0.1f
            : moved.Z < startPosition.Z - 0.1f;
        bool turnPassed = true;
        if (expectedTurnSign != 0)
            turnPassed = expectedTurnSign < 0 ? turned.y < -0.05f : turned.y > 0.05f;

        const bool passed = translationPassed && turnPassed;
        std::cout << "[WASD-Test] " << label << ": " << (passed ? "PASS" : "FAIL")
                  << " (z=" << moved.Z << ", qy=" << turned.y << ")\n";
        if (!passed) ++failures;
    };

    runCase(GLFW_KEY_W, -1, "W avança", true, 0);
    runCase(GLFW_KEY_S, -1, "S recua", false, 0);
    runCase(GLFW_KEY_W, GLFW_KEY_A, "A vira à esquerda", true, -1);
    runCase(GLFW_KEY_W, GLFW_KEY_D, "D vira à direita", true, 1);

    g_KeyState.fill(false);
    g_PreviousKeyState.fill(false);
    Transform& transform = car->GetTransform();
    transform.Position = transform.LocalPosition = startPosition;
    transform.Rotation = transform.LocalRotation = startRotation;
    transform.EulerRotation = transform.LocalEulerRotation = Vector3(0.0f, 0.0f, 0.0f);
    OnExitPlay();
    std::cout << "[WASD-Test] Resultado: " << (failures == 0 ? "4/4 PASS" : "FALHAS") << "\n";
    return failures == 0 ? 0 : 1;
}

int ScriptingRuntime::RunCameraFlyRegressionTests(Scene& scene)
{
    if (!m_Initialized || !m_HostFxrLoaded)
    {
        std::cerr << "[CameraFly-Test] Runtime .NET indisponível.\n";
        return 1;
    }

    Entity& camera = scene.CreateEntity();
    camera.SetName("__CameraFlyRegressionCamera");
    camera.GetScriptComponent().Enabled = true;
    camera.GetScriptComponent().ScriptPath = "Scripts/CameraFlyController.cs";
    camera.GetTransform().Position = Vector3(0.0f, 0.0f, 0.0f);
    camera.GetTransform().LocalPosition = camera.GetTransform().Position;
    camera.GetTransform().Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    camera.GetTransform().LocalRotation = camera.GetTransform().Rotation;
    const uint64_t cameraId = camera.GetID();

    OnEnterPlay(&scene);
    if (m_EntityToInstance.find(cameraId) == m_EntityToInstance.end())
    {
        std::cerr << "[CameraFly-Test] Não foi possível instanciar CameraFlyController. "
                     "Confirme que o arquivo está em Assets/Scripts.\n";
        OnExitPlay();
        return 1;
    }

    const Vector3 origin(0.0f, 0.0f, 0.0f);
    constexpr float dt = 0.1f;
    int failures = 0;
    auto runMovementCase = [&](int key, const char* label, const auto& passed)
    {
        Entity* testCamera = scene.FindEntityByID(cameraId);
        if (!testCamera)
        {
            std::cerr << "[CameraFly-Test] A câmera de teste foi destruída.\n";
            ++failures;
            return;
        }
        Transform& transform = testCamera->GetTransform();
        transform.Position = transform.LocalPosition = origin;
        g_KeyState.fill(false);
        g_PreviousKeyState.fill(false);
        g_MouseState.fill(false);
        g_MouseDeltaX = g_MouseDeltaY = 0.0;
        g_KeyState[static_cast<size_t>(key)] = true;
        OnUpdate(dt);
        testCamera = scene.FindEntityByID(cameraId);
        if (!testCamera)
        {
            std::cerr << "[CameraFly-Test] A câmera de teste sumiu após OnUpdate.\n";
            ++failures;
            return;
        }
        const Vector3 result = testCamera->GetTransform().Position;
        const bool ok = passed(result);
        std::cout << "[CameraFly-Test] " << label << ": "
                  << (ok ? "PASS" : "FAIL") << " ("
                  << result.X << ", " << result.Y << ", " << result.Z << ")\n";
        if (!ok) ++failures;
    };

    runMovementCase(GLFW_KEY_W, "W move para frente", [](const Vector3& p) { return p.Z < -0.1f; });
    runMovementCase(GLFW_KEY_S, "S move para trás", [](const Vector3& p) { return p.Z > 0.1f; });
    runMovementCase(GLFW_KEY_A, "A move para esquerda", [](const Vector3& p) { return p.X < -0.1f; });
    runMovementCase(GLFW_KEY_D, "D move para direita", [](const Vector3& p) { return p.X > 0.1f; });

    g_KeyState.fill(false);
    g_PreviousKeyState.fill(false);
    g_MouseState.fill(false);
    g_MouseState[static_cast<size_t>(GLFW_MOUSE_BUTTON_RIGHT)] = true;
    g_MouseDeltaX = 100.0;
    g_MouseDeltaY = 0.0;
    OnUpdate(dt);
    Entity* testCamera = scene.FindEntityByID(cameraId);
    if (!testCamera)
    {
        std::cerr << "[CameraFly-Test] A câmera de teste sumiu após OnUpdate.\n";
        OnExitPlay();
        return 1;
    }
    glm::vec3 entityForward = testCamera->GetTransform().Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    bool yawPassed = entityForward.x < -0.1f;
    std::cout << "[CameraFly-Test] Mouse para direita gira a visão para direita: "
              << (yawPassed ? "PASS" : "FAIL") << " (forward.x=" << entityForward.x << ")\n";
    if (!yawPassed) ++failures;

    g_MouseDeltaX = 0.0;
    g_MouseDeltaY = 100.0;
    OnUpdate(dt);
    testCamera = scene.FindEntityByID(cameraId);
    if (!testCamera)
    {
        std::cerr << "[CameraFly-Test] A câmera de teste sumiu após OnUpdate.\n";
        OnExitPlay();
        return 1;
    }
    entityForward = testCamera->GetTransform().Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    bool pitchPassed = entityForward.y > 0.1f;
    std::cout << "[CameraFly-Test] Mouse para baixo inclina a visão para baixo: "
              << (pitchPassed ? "PASS" : "FAIL") << " (forward.y=" << entityForward.y << ")\n";
    if (!pitchPassed) ++failures;

    testCamera->GetTransform().Position = testCamera->GetTransform().LocalPosition = origin;
    g_KeyState[static_cast<size_t>(GLFW_KEY_W)] = true;
    g_MouseState.fill(false);
    OnUpdate(dt);
    testCamera = scene.FindEntityByID(cameraId);
    if (!testCamera)
    {
        std::cerr << "[CameraFly-Test] A câmera de teste sumiu após OnUpdate.\n";
        OnExitPlay();
        return 1;
    }
    const Vector3 diagonalMove = testCamera->GetTransform().Position;
    const bool relativeMovePassed = diagonalMove.X > 0.1f && diagonalMove.Z < -0.1f;
    std::cout << "[CameraFly-Test] W segue a direção atual da câmera: "
              << (relativeMovePassed ? "PASS" : "FAIL") << " ("
              << diagonalMove.X << ", " << diagonalMove.Y << ", " << diagonalMove.Z << ")\n";
    if (!relativeMovePassed) ++failures;

    g_KeyState.fill(false);
    g_PreviousKeyState.fill(false);
    g_MouseState.fill(false);
    g_MouseDeltaX = g_MouseDeltaY = 0.0;
    OnExitPlay();
    std::cout << "[CameraFly-Test] Resultado: " << (failures == 0 ? "7/7 PASS" : "FALHAS") << "\n";
    return failures == 0 ? 0 : 1;
}

int ScriptingRuntime::RunComponentApiRegressionTests(Scene& scene, bool world2D)
{
    const char* mode = world2D ? "2D" : "3D";
    if (!m_Initialized || !m_HostFxrLoaded)
    {
        std::cerr << "[ScriptAPI-Test] Runtime .NET indisponível.\n";
        return 1;
    }

    // Isola este ensaio dos objetos padrão do editor; o processo de teste é
    // descartável e nenhuma cena do usuário é salva ou modificada.
    scene.Clear();
    auto attachScript = [](Entity& entity, const char* name, const char* file)
    {
        entity.SetName(name);
        ScriptComponent& script = entity.GetScriptComponent();
        script.Enabled = true;
        script.ScriptPath = std::string("Assets/Scripts/") + file;
    };

    Entity& visual = scene.CreateEntity();
    attachScript(visual, "ScriptAPI Visual", "ScriptApiTransform.cs");
    const uint64_t visualId = visual.GetID();

    Entity& physics = scene.CreateEntity();
    attachScript(physics, "ScriptAPI Physics", "ScriptApiPhysics.cs");
    physics.PhysicsEnabled = true;
    physics.ColliderEnabled = true;
    const uint64_t physicsId = physics.GetID();

    Entity& ui = scene.CreateEntity();
    ui.AddUI(UIElementType::TextButton);
    attachScript(ui, "ScriptAPI UI", "ScriptApiUI.cs");
    const uint64_t uiId = ui.GetID();

    Entity& lightCameraAnimation = scene.CreateEntity();
    lightCameraAnimation.AddLight(LightType::Point);
    lightCameraAnimation.AddCamera();
    lightCameraAnimation.GetAnimationComponent().SetClip(AnimationClip::CreateDefault(
        "script-api-animation", 2.0f,
        glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)));
    attachScript(lightCameraAnimation, "ScriptAPI Light Camera Animation",
                 "ScriptApiLightCameraAnimation.cs");
    const uint64_t lightId = lightCameraAnimation.GetID();

    Entity& globalLight = scene.CreateEntity();
    globalLight.SetName("ScriptAPI Global Light");
    globalLight.SetGlobalLight(true);
    globalLight.GetGlobalLightComponent().Lighting.CloudsEnabled = true;
    globalLight.GetGlobalLightComponent().Lighting.CloudMovementEnabled = false;
    attachScript(globalLight, "ScriptAPI Global Light", "ScriptApiGlobalLight.cs");
    const uint64_t globalLightId = globalLight.GetID();

    Entity& inputEvents = scene.CreateEntity();
    attachScript(inputEvents, "ScriptAPI Input Events", "ScriptApiInputEvents.cs");
    const uint64_t inputEventsId = inputEvents.GetID();

    Entity& audio = scene.CreateEntity();
    audio.AddAudio();
    attachScript(audio, "ScriptAPI Audio", "ScriptApiAudio.cs");
    const uint64_t audioId = audio.GetID();

    Entity& eventPeer = scene.CreateEntity();
    eventPeer.SetName("ScriptAPI Event Peer");
    const uint64_t eventPeerId = eventPeer.GetID();

    int failures = 0;
    int passed = 0;
    auto check = [&](const char* name, bool condition)
    {
        std::cout << "[ScriptAPI-Test " << mode << "] " << name << ": "
                  << (condition ? "PASS" : "FAIL") << '\n';
        if (condition) ++passed;
        else ++failures;
    };

    OnEnterPlay(&scene);
    const uint64_t expectedScripts[] = {
        visualId, physicsId, uiId, lightId, globalLightId, inputEventsId, audioId
    };
    bool allScriptsCreated = true;
    for (uint64_t id : expectedScripts)
        allScriptsCreated = allScriptsCreated && m_EntityToInstance.contains(id);
    check("7 scripts diferentes compilados e instanciados", allScriptsCreated);
    if (!allScriptsCreated)
    {
        OnExitPlay();
        g_TimeScale = 1.0f;
        return 1;
    }

    Entity* visualResult = scene.FindEntityByID(visualId);
    Entity* physicsResult = scene.FindEntityByID(physicsId);
    Entity* uiResult = scene.FindEntityByID(uiId);
    Entity* lightResult = scene.FindEntityByID(lightId);
    Entity* globalResult = scene.FindEntityByID(globalLightId);
    Entity* audioResult = scene.FindEntityByID(audioId);
#pragma push_macro("near")
#undef near
    auto near = [](float a, float b, float eps = 0.001f)
    {
        return std::isfinite(a) && std::abs(a - b) <= eps;
    };

    check("Transform, nome e tag", visualResult &&
        visualResult->GetName() == "Transform API passed" &&
        visualResult->Tag == "script-api-test" &&
        near(visualResult->GetTransform().Position.X, 1.25f) &&
        near(visualResult->GetTransform().Position.Y, 2.5f) &&
        near(visualResult->GetTransform().Position.Z, -3.75f));
    check("Malha/cor e material PBR", visualResult &&
        !visualResult->Active &&
        near(visualResult->GetColorComponent().Color.X, 0.2f) &&
        near(visualResult->GetMaterialComponent().MaterialData.Color[1], 0.3f) &&
        near(visualResult->GetMaterialComponent().MaterialData.Metallic, 0.65f) &&
        near(visualResult->GetMaterialComponent().MaterialData.Roughness, 0.15f));
    check("Rigidbody: propriedades, collider e impulso", physicsResult &&
        physicsResult->PhysicsEnabled && !physicsResult->UseGravity &&
        !physicsResult->IsStatic && physicsResult->ColliderEnabled &&
        physicsResult->IsTrigger && physicsResult->ContinuousCollision && !physicsResult->FreezeRotation &&
        physicsResult->Collider == Entity::ColliderShape::Sphere &&
        near(physicsResult->Mass, 2.0f) && near(physicsResult->Restitution, 0.45f) &&
        near(physicsResult->Friction, 0.3f) && near(physicsResult->Velocity.X, 3.0f) &&
        near(physicsResult->Velocity.Y, 2.0f) && near(physicsResult->AngularVelocity.Z, 0.3f));
    check("UI 2D: tipo, retângulo, texto, fonte e cores", uiResult && uiResult->HasUI() &&
        uiResult->GetUIComponent().Type == UIElementType::TextButton &&
        near(uiResult->GetUIComponent().PositionX, 24.0f) &&
        near(uiResult->GetUIComponent().PositionY, 48.0f) &&
        near(uiResult->GetUIComponent().Width, 320.0f) &&
        near(uiResult->GetUIComponent().Height, 72.0f) &&
        uiResult->GetUIComponent().Text == "C# UI conectado" &&
        uiResult->GetUIComponent().ImagePath == "ui/test-icon.png" &&
        near(uiResult->GetUIComponent().FontSize, 28.0f) &&
        near(uiResult->GetUIComponent().BackgroundColor[2], 0.8f) &&
        near(uiResult->GetUIComponent().Transparency, 0.2f));
    check("Luz local: tipo Area, cor, alcance e sombras", lightResult && lightResult->HasLight() &&
        lightResult->GetLightComponent().Type == LightType::Area &&
        !lightResult->GetLightComponent().Enabled && !lightResult->GetLightComponent().CastShadows &&
        lightResult->GetLightComponent().UseTemperature &&
        near(lightResult->GetLightComponent().Intensity, 35.0f) &&
        near(lightResult->GetLightComponent().Color[1], 0.5f) &&
        near(lightResult->GetLightComponent().Range, 18.0f) &&
        near(lightResult->GetLightComponent().ShadowSoftness, 0.7f) &&
        near(lightResult->GetLightComponent().TemperatureKelvin, 4200.0f));
    check("Câmera e AnimationComponent", lightResult && lightResult->HasCamera() &&
        lightResult->GetCameraComponent().Orthographic &&
        !lightResult->GetCameraComponent().PreviewEnabled &&
        near(lightResult->GetCameraComponent().FieldOfView, 75.0f) &&
        near(lightResult->GetCameraComponent().FarClip, 800.0f) &&
        lightResult->GetAnimationComponent().Playing &&
        lightResult->GetAnimationComponent().PlayOnStart &&
        !lightResult->GetAnimationComponent().Clip->ID.empty() &&
        near(lightResult->GetAnimationComponent().Time, 0.5f) &&
        near(lightResult->GetAnimationComponent().Speed, 1.5f) &&
        near(lightResult->GetAnimationComponent().Weight, 0.7f));
    check("Global Light: ambiente, névoa e nuvens", globalResult && globalResult->IsGlobalLight() &&
        globalResult->GetGlobalLightComponent().Enabled &&
        globalResult->GetGlobalLightComponent().FogEnabled &&
        !globalResult->GetGlobalLightComponent().Lighting.CloudsEnabled &&
        globalResult->GetGlobalLightComponent().Lighting.CloudMovementEnabled &&
        near(globalResult->GetGlobalLightComponent().Lighting.AmbientIntensity, 0.55f) &&
        near(globalResult->GetGlobalLightComponent().Lighting.FogDensity, 0.02f) &&
        near(globalResult->GetGlobalLightComponent().Lighting.CloudAmount, 0.65f) &&
        near(globalResult->GetGlobalLightComponent().Lighting.CloudSpeed, 1.25f));
    check("Áudio: volume, pitch, loop e estado de reprodução", audioResult && audioResult->HasAudio() &&
        near(audioResult->GetAudioComponent().Volume, 0.6f) &&
        near(audioResult->GetAudioComponent().Pitch, 1.25f) &&
        audioResult->GetAudioComponent().Loop &&
        audioResult->GetAudioComponent().PlayState == AudioPlayState::Playing);
    check("Time.timeScale", near(g_TimeScale, 0.75f));

    g_KeyState.fill(false);
    g_PreviousKeyState.fill(false);
    g_MouseState.fill(false);
    g_PreviousMouseState.fill(false);
    g_KeyState[GLFW_KEY_SPACE] = true;
    g_KeyState[GLFW_KEY_D] = true;
    MaraeInput::InputMap::Instance().Update(
        g_KeyState, g_PreviousKeyState, g_MouseState, g_PreviousMouseState, 0.0f, 0.0f);
    PerformanceMonitor::SetScriptProfilingEnabled(true);
    PerformanceMonitor::BeginScriptProfileFrame();
    OnUpdate(0.25f);
    const auto& scriptTimings = PerformanceMonitor::GetScriptTimings();
    const bool measuredInputScript = std::any_of(scriptTimings.begin(), scriptTimings.end(),
        [](const PerformanceMonitor::ScriptTiming& timing)
        {
            return timing.Name.find("ScriptApiInputEvents.cs (OnUpdate)") != std::string::npos &&
                   timing.Calls == 1 && std::isfinite(timing.FrameTimeMs) && timing.FrameTimeMs >= 0.0;
        });
    check("Profiler mede custo de OnUpdate por script", measuredInputScript);
    Entity* inputResult = scene.FindEntityByID(inputEventsId);
    check("Input.GetKeyDown/GetAxisRaw e OnUpdate", inputResult &&
        near(inputResult->GetTransform().Position.X, 1.0f) &&
        near(inputResult->GetTransform().Position.Y, 1.0f) &&
        near(inputResult->GetTransform().Position.Z, 0.25f));

    g_PreviousKeyState[GLFW_KEY_SPACE] = true;
    g_KeyState.fill(false);
    g_PreviousMouseState[GLFW_MOUSE_BUTTON_LEFT] = false;
    g_MouseState[GLFW_MOUSE_BUTTON_LEFT] = true;
    MaraeInput::InputMap::Instance().Update(
        g_KeyState, g_PreviousKeyState, g_MouseState, g_PreviousMouseState, 0.0f, 0.0f);
    OnUpdate(0.25f);
    inputResult = scene.FindEntityByID(inputEventsId);
    check("MouseButtonDown não repete keyDown", inputResult &&
        near(inputResult->GetTransform().Position.X, 11.0f) &&
        near(inputResult->GetTransform().Position.Y, 1.0f));

    OnFixedUpdate(0.02f);
    inputResult = scene.FindEntityByID(inputEventsId);
    check("OnFixedUpdate e fixedDelta", inputResult &&
        near(inputResult->GetTransform().Position.Z, 0.52f));

    g_KeyState.fill(false);
    g_PreviousKeyState.fill(false);
    g_KeyState[GLFW_KEY_B] = true;
    OnUpdate(0.01f);
    lightResult = scene.FindEntityByID(lightId);
    check("Animation Pause pelo script", lightResult && !lightResult->GetAnimationComponent().Playing);

    g_KeyState.fill(false);
    g_PreviousKeyState.fill(false);
    g_KeyState[GLFW_KEY_C] = true;
    OnUpdate(0.01f);
    lightResult = scene.FindEntityByID(lightId);
    check("Animation Stop zera o tempo", lightResult &&
        !lightResult->GetAnimationComponent().Playing && near(lightResult->GetAnimationComponent().Time, 0.0f));

    FireCollisionEvent(inputEventsId, eventPeerId, 0, 5.0f, 6.0f, 7.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    inputResult = scene.FindEntityByID(inputEventsId);
    check("Callback OnCollisionEnter", inputResult &&
        near(inputResult->GetTransform().Position.X, 5.0f) &&
        near(inputResult->GetTransform().Position.Y, 6.0f) &&
        near(inputResult->GetTransform().Position.Z, 7.0f));
    FireCollisionEvent(inputEventsId, eventPeerId, 3);
    inputResult = scene.FindEntityByID(inputEventsId);
    check("Callback OnTriggerEnter", inputResult &&
        near(inputResult->GetTransform().Position.X, 9.0f) &&
        near(inputResult->GetTransform().Position.Y, 8.0f) &&
        near(inputResult->GetTransform().Position.Z, 7.0f));

    g_KeyState.fill(false);
    g_PreviousKeyState.fill(false);
    g_MouseState.fill(false);
    g_PreviousMouseState.fill(false);
    g_MouseDeltaX = g_MouseDeltaY = 0.0;
    OnExitPlay();
    PerformanceMonitor::SetScriptProfilingEnabled(false);
    g_TimeScale = 1.0f;
    std::cout << "[ScriptAPI-Test " << mode << "] Resultado: " << passed << "/"
              << (passed + failures) << " PASS";
    if (failures != 0) std::cout << " (" << failures << " falha(s))";
    std::cout << '\n';
    return failures == 0 ? 0 : 1;
}
#pragma pop_macro("near")

void ScriptingRuntime::CreateScriptInstancesForScene()
{
    if (!m_CurrentScene || !m_ManagedAPI.CreateScriptInstance) return;

    for (Entity& entity : m_CurrentScene->GetEntities())
    {
        // Editor-only entities dont run scripts, EXCEPT the Global Script controller
        if (entity.IsEditorOnly() && !entity.IsGlobalScript()) continue;

        ScriptComponent& sc = entity.GetScriptComponent();
        if (!sc.Enabled) continue;

        std::string typeName = sc.ScriptAssemblyQualifiedTypeName;
        if (typeName.empty() && !sc.ScriptPath.empty())
        {
            std::filesystem::path sourcePath(sc.ScriptPath);
            if (sourcePath.is_relative())
            {
                const std::filesystem::path projectRoot(m_ProjectRoot);
                const std::string relativeText = sourcePath.generic_string();
                const bool legacyScriptsPath = relativeText.rfind("Scripts/", 0) == 0;
                const std::filesystem::path directPath = projectRoot / sourcePath;
                const std::filesystem::path assetsPath = projectRoot / "Assets" / sourcePath;
                const std::filesystem::path scriptsPath = projectRoot / "Scripts" / sourcePath;
                std::error_code pathError;

                // Assets/** is the compiled source tree whenever it contains
                // scripts; do not bind a component to the excluded legacy copy.
                if (legacyScriptsPath && std::filesystem::exists(assetsPath, pathError) && !pathError)
                    sourcePath = assetsPath;
                else if (std::filesystem::exists(directPath, pathError) && !pathError)
                    sourcePath = directPath;
                else
                {
                    pathError.clear();
                    sourcePath = std::filesystem::exists(assetsPath, pathError) && !pathError
                        ? assetsPath
                        : (std::filesystem::exists(scriptsPath, pathError) && !pathError
                            ? scriptsPath : directPath);
                }
            }
            typeName = "@marae-script-file:" + sourcePath.lexically_normal().generic_string();
        }

        if (typeName.empty()) continue;

        std::wstring typeNameW = Utf8ToWide(typeName);
        uint64_t instance = m_ManagedAPI.CreateScriptInstance(
            entity.GetID(), typeNameW.c_str());

        // Resolve automaticamente scripts no namespace MaraeScripts quando o componente guarda somente o nome da classe.
        if (instance == 0 && typeName.find('.') == std::string::npos)
        {
            const std::string namespacedType = "MaraeScripts." + typeName;
            const std::wstring namespacedTypeW = Utf8ToWide(namespacedType);
            instance = m_ManagedAPI.CreateScriptInstance(entity.GetID(), namespacedTypeW.c_str());
        }
        if (instance != 0)
        {
            m_EntityToInstance[entity.GetID()] = instance;
            sc.ScriptInstanceId = instance;

            for (auto& [fieldName, jsonValue] : sc.SerializedFields)
            {
                if (m_ManagedAPI.SetFieldJson)
                {
                    std::wstring fnW = Utf8ToWide(fieldName);
                    std::wstring jvW = Utf8ToWide(jsonValue);
                    m_ManagedAPI.SetFieldJson(instance, fnW.c_str(), jvW.c_str());
                }
            }
        }
        else
        {
            std::cerr << "[Scripting] Não foi possível instanciar '" << typeName
                      << "' na entidade '" << entity.GetName()
                      << "'. A classe deve herdar de ScriptBehaviour; confira também os erros de compilação."
                      << std::endl;
        }
    }
}

void ScriptingRuntime::DestroyAllScriptInstances()
{
    for (auto& [entityId, instanceHandle] : m_EntityToInstance)
    {
        if (m_ManagedAPI.CallOnDestroy && instanceHandle != 0)
            m_ManagedAPI.CallOnDestroy(instanceHandle);
        if (m_ManagedAPI.DestroyScriptInstance && instanceHandle != 0)
            m_ManagedAPI.DestroyScriptInstance(instanceHandle);
    }
    m_EntityToInstance.clear();
    m_StartedInstances.clear();

    if (m_CurrentScene)
    {
        for (Entity& entity : m_CurrentScene->GetEntities())
        {
            entity.GetScriptComponent().ScriptInstanceId = 0;
        }
    }
}

void ScriptingRuntime::CallOnStartForNewInstances()
{
    if (!m_ManagedAPI.CallOnStart) return;

    for (auto& [entityId, instanceHandle] : m_EntityToInstance)
    {
        if (instanceHandle == 0) continue;
        if (m_StartedInstances.count(entityId)) continue;
        m_ManagedAPI.CallOnStart(instanceHandle);
        m_StartedInstances.insert(entityId);
    }
}

#if 0 // Desativado: parser textual legado não executava C# e gerava comportamento enganoso.
void ScriptingRuntime::RunLegacyScriptForEntity(Entity& entity, float deltaTime,
                                                const std::string& projectPath,
                                                bool forceFallback)
{
    if (deltaTime <= 0.0f) return;

    if (entity.IsEditorOnly() || !entity.Active)
        return;

    ScriptComponent& script = entity.GetScriptComponent();
    if (!script.Enabled || script.ScriptPath.empty())
        return;

    // Um script gerenciado que funcionou já movimenta a entidade. O fallback
    // só deve atuar quando não existe instância gerenciada ou quando ela falhou.
    if (!forceFallback && m_EntityToInstance.find(entity.GetID()) != m_EntityToInstance.end())
        return;

    namespace fs = std::filesystem;
    fs::path input(script.ScriptPath);
    fs::path scriptPath;
    if (input.is_absolute())
        scriptPath = input;
    else
    {
        const fs::path projectRoot = !projectPath.empty()
            ? fs::path(projectPath)
            : fs::current_path();
        const fs::path inProject = projectRoot / input;
        if (fs::exists(inProject))
            scriptPath = fs::weakly_canonical(inProject);
        else
            scriptPath = inProject;
    }

    std::ifstream file(scriptPath);
    if (!file.is_open())
    {
        const uint64_t errorKey = entity.GetID() ^ 0x8000000000000000ULL;
        if (m_ScriptTimes.find(errorKey) == m_ScriptTimes.end())
        {
            std::cerr << "[Script] Arquivo nao encontrado: "
                      << scriptPath.string() << "\n";
            m_ScriptTimes[errorKey] = 0.0f;
        }
        return;
    }

    const std::string source(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    const float safeDeltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    m_ScriptTimes[entity.GetID()] += safeDeltaTime;

    const bool usesKeyboardInput =
        source.find("Input.IsKeyDown") != std::string::npos ||
        source.find("Input.GetKeyDown") != std::string::npos ||
        source.find("Input.GetKey(") != std::string::npos ||
        source.find("Input.GetAxis") != std::string::npos;

    if (usesKeyboardInput)
    {
        Window* inputWindow = GetInputWindow();
        if (inputWindow)
        {
            const bool wantsSpaceDown =
                source.find("Input.GetKeyDown(KeyCode.Space)") != std::string::npos ||
                source.find("Input.IsKeyPressed(KeyCode.Space)") != std::string::npos;
            const bool wantsSpaceHeld =
                source.find("Input.GetKey(KeyCode.Space)") != std::string::npos ||
                source.find("Input.IsKeyDown(KeyCode.Space)") != std::string::npos;
            const bool spacePressed = wantsSpaceDown
                ? Native_InputGetKeyDown(GLFW_KEY_SPACE)
                : (wantsSpaceHeld && Native_InputGetKey(GLFW_KEY_SPACE));

            if (spacePressed && entity.PhysicsEnabled && !entity.IsStatic)
                entity.Velocity.Y += 6.0f / std::max(0.0001f, entity.Mass);
            glm::vec3 direction(0.0f);
            if (inputWindow->IsKeyPressed(GLFW_KEY_W)) direction.z -= 1.0f;
            if (inputWindow->IsKeyPressed(GLFW_KEY_S)) direction.z += 1.0f;
            if (inputWindow->IsKeyPressed(GLFW_KEY_A)) direction.x -= 1.0f;
            if (inputWindow->IsKeyPressed(GLFW_KEY_D)) direction.x += 1.0f;
            if (glm::length(direction) > 0.0001f)
            {
                direction = glm::normalize(direction);
                const Vector3 delta(direction.x * 5.0f * safeDeltaTime, direction.y * 5.0f * safeDeltaTime, direction.z * 5.0f * safeDeltaTime);
                entity.GetTransform().Position = entity.GetTransform().Position + delta;
                entity.GetTransform().LocalPosition = entity.GetTransform().Position;
            }
        }
    }

    if (source.find("transform.Rotate") == std::string::npos)
        return;

    auto readFloat = [&](const std::initializer_list<const char*>& names,
                         float fallback) -> float
    {
        for (const char* name : names)
        {
            const std::regex expression(
                std::string(name) + R"(\s*=\s*(-?[0-9]+(?:\.[0-9]+)?))",
                std::regex_constants::icase);
            std::smatch match;
            if (std::regex_search(source, match, expression) && match.size() > 1)
            {
                try { return std::stof(match[1].str()); }
                catch (...) {}
            }
        }
        return fallback;
    };

    auto readBool = [&](const char* name, bool fallback) -> bool
    {
        const std::regex expression(
            std::string(name) + R"(\s*=\s*(true|false))",
            std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(source, match, expression) && match.size() > 1)
            return match[1].str() == "true" || match[1].str() == "True";
        return fallback;
    };

    const float speedDegrees = readFloat(
        {"velocidadeRotacao", "RotationSpeed", "rotationSpeed", "Speed"},
        90.0f);
    const bool rotateX = readBool("rotacionarX", false);
    const bool rotateY = readBool("rotacionarY", true);
    const bool rotateZ = readBool("rotacionarZ", false);

    glm::vec3 axis(
        rotateX ? 1.0f : 0.0f,
        rotateY ? 1.0f : 0.0f,
        rotateZ ? 1.0f : 0.0f);
    if (glm::length(axis) <= 0.0001f)
        return;

    axis = glm::normalize(axis);
    const glm::quat deltaRotation = glm::angleAxis(
        glm::radians(speedDegrees) * safeDeltaTime, axis);
    Transform& transform = entity.GetTransform();
    transform.Rotation = glm::normalize(deltaRotation * transform.Rotation);
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.Rotation));
    transform.EulerRotation = Vector3(euler.x, euler.y, euler.z);
    transform.LocalRotation = transform.Rotation;
    transform.LocalEulerRotation = transform.EulerRotation;
}

void ScriptingRuntime::RunLegacyScripts(float deltaTime, Scene* scene, const std::string& projectPath)
{
    if (!scene) return;
    g_Scene = scene;
    g_DeltaTime = deltaTime;
    g_TotalTime += deltaTime;
    for (Entity& entity : scene->GetEntities())
    {
        RunLegacyScriptForEntity(entity, deltaTime, projectPath);
    }
}
#endif

void ScriptingRuntime::FireCollisionEvent(uint64_t entityA, uint64_t entityB, int eventType,
                                          float cx, float cy, float cz,
                                          float nx, float ny, float nz, float impulse)
{
    auto itA = m_EntityToInstance.find(entityA);
    if (itA != m_EntityToInstance.end() && itA->second != 0)
    {
        uint64_t instA = itA->second;
        switch (eventType)
        {
        case 0:
            if (m_ManagedAPI.SafeInvokeOnCollisionEnter)
                m_ManagedAPI.SafeInvokeOnCollisionEnter(instA, entityB, cx, cy, cz, nx, ny, nz, impulse);
            break;
        case 1:
            if (m_ManagedAPI.SafeInvokeOnCollisionStay)
                m_ManagedAPI.SafeInvokeOnCollisionStay(instA, entityB, cx, cy, cz, nx, ny, nz, impulse);
            break;
        case 2:
            if (m_ManagedAPI.SafeInvokeOnCollisionExit)
                m_ManagedAPI.SafeInvokeOnCollisionExit(instA, entityB, 0, 0, 0, 0, 1, 0, 0);
            break;
        case 3:
            if (m_ManagedAPI.SafeInvokeOnTriggerEnter)
                m_ManagedAPI.SafeInvokeOnTriggerEnter(instA, entityB);
            break;
        case 4:
            if (m_ManagedAPI.SafeInvokeOnTriggerStay)
                m_ManagedAPI.SafeInvokeOnTriggerStay(instA, entityB);
            break;
        case 5:
            if (m_ManagedAPI.SafeInvokeOnTriggerExit)
                m_ManagedAPI.SafeInvokeOnTriggerExit(instA, entityB);
            break;
        }
    }
}

void ScriptingRuntime::FireUIClicked(uint64_t entityId)
{
    if (!m_Initialized || !m_ManagedAPI.SafeInvokeOnUIClicked) return;
    auto it = m_EntityToInstance.find(entityId);
    if (it != m_EntityToInstance.end() && it->second != 0)
        m_ManagedAPI.SafeInvokeOnUIClicked(it->second, entityId);
}

void ScriptingRuntime::FireUIValueChanged(uint64_t entityId, float value)
{
    if (!m_Initialized || !m_ManagedAPI.SafeInvokeOnUIValueChanged) return;
    auto it = m_EntityToInstance.find(entityId);
    if (it != m_EntityToInstance.end() && it->second != 0)
        m_ManagedAPI.SafeInvokeOnUIValueChanged(it->second, entityId, value);
}

void ScriptingRuntime::FireUITextChanged(uint64_t entityId, const std::string& text)
{
    if (!m_Initialized || !m_ManagedAPI.SafeInvokeOnUITextChanged) return;
    auto it = m_EntityToInstance.find(entityId);
    if (it != m_EntityToInstance.end() && it->second != 0)
    {
        const std::wstring wide = Utf8ToWide(text);
        m_ManagedAPI.SafeInvokeOnUITextChanged(it->second, entityId, wide.c_str());
    }
}

std::string ScriptingRuntime::GetPublicFieldsJson(uint64_t entityId) const
{
    auto it = m_EntityToInstance.find(entityId);
    if (it == m_EntityToInstance.end() || it->second == 0) return "";
    if (m_ManagedAPI.GetPublicFieldsJson)
    {
        const wchar_t* wjson = m_ManagedAPI.GetPublicFieldsJson(it->second);
        if (wjson) return WideToUtf8(wjson);
    }
    return "";
}

void ScriptingRuntime::ApplyFieldValue(uint64_t entityId, const std::string& fieldName, const std::string& jsonValue)
{
    auto it = m_EntityToInstance.find(entityId);
    if (it == m_EntityToInstance.end() || it->second == 0) return;
    if (m_ManagedAPI.SetFieldJson)
    {
        std::wstring fW = Utf8ToWide(fieldName);
        std::wstring vW = Utf8ToWide(jsonValue);
        m_ManagedAPI.SetFieldJson(it->second, fW.c_str(), vW.c_str());
    }
}






