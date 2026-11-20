#include "../../../Engine/Localization/Localization.h"
#include "TimelinePanel.h"
#include "../../../Core/Application.h"
#include "../../../Scene/Entity/Entity.h"
#include "../../../Scene/Components/AnimationComponent.h"
#include "../../../Scene/Transform/Transform.h"
#include "../../../Engine/Systems/AnimationSystem.h"
#include "../Toolbar/ToolbarPanel.h"
#include <imgui.h>
#include "../../ThirdParty/ImGuizmo/ImGuizmo/src/ImGuizmo.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>

float TimelinePanel::s_PlaybackTime = 0.0f;
bool TimelinePanel::s_IsPlaying = false;
float TimelinePanel::s_TimeScale = 1.0f;

bool TimelinePanel::s_ShowPosX = true;
bool TimelinePanel::s_ShowPosY = true;
bool TimelinePanel::s_ShowPosZ = true;
bool TimelinePanel::s_ShowRotX = true;
bool TimelinePanel::s_ShowRotY = true;
bool TimelinePanel::s_ShowRotZ = true;
bool TimelinePanel::s_ShowSclX = true;
bool TimelinePanel::s_ShowSclY = true;
bool TimelinePanel::s_ShowSclZ = true;
bool TimelinePanel::s_PositionExpanded = true;
bool TimelinePanel::s_RotationExpanded = true;
bool TimelinePanel::s_ScaleExpanded = true;

int TimelinePanel::s_SelectedKeyType = -1;
int TimelinePanel::s_SelectedKeyIndex = -1;

static float s_LastPlaybackTime = -9999.0f;
static uint64_t s_LastSelectedEntityID = 0;
static AnimationClip* s_LastSelectedClip = nullptr;
static bool s_SnapEnabled = false;
static float s_SnapInterval = 0.1f;

void TimelinePanel::AdvanceRecording(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
        return;
    s_PlaybackTime = std::clamp(s_PlaybackTime + deltaTime * s_TimeScale, 0.0f, 3600.0f);
}

static ImU32 KeyColorPos = IM_COL32(100, 220, 100, 255);
static ImU32 KeyColorRot = IM_COL32(100, 140, 255, 255);
static ImU32 KeyColorScl = IM_COL32(255, 120, 120, 255);
static ImU32 KeyColorSelected = IM_COL32(255, 230, 50, 255);
static ImU32 TrackBgColor = IM_COL32(35, 35, 40, 255);
static ImU32 TrackBgAltColor = IM_COL32(42, 42, 48, 255);
static ImU32 TrackBorderColor = IM_COL32(80, 80, 90, 255);
static ImU32 PlayheadColor = IM_COL32(255, 80, 80, 255);

enum KeyTrackType
{
    TRACK_POS_ALL = 0,
    TRACK_POS_X,
    TRACK_POS_Y,
    TRACK_POS_Z,
    TRACK_ROT_ALL,
    TRACK_ROT_X,
    TRACK_ROT_Y,
    TRACK_ROT_Z,
    TRACK_SCL_ALL,
    TRACK_SCL_X,
    TRACK_SCL_Y,
    TRACK_SCL_Z,
    TRACK_COUNT
};

static void DrawDiamondKey(ImDrawList* dl, float cx, float cy, ImU32 color, float size = 5.0f)
{
    ImVec2 top(cx, cy - size);
    ImVec2 right(cx + size, cy);
    ImVec2 bottom(cx, cy + size);
    ImVec2 left(cx - size, cy);
    dl->AddTriangleFilled(top, left, ImVec2(cx, cy), color);
    dl->AddTriangleFilled(top, ImVec2(cx, cy), right, color);
    dl->AddTriangleFilled(bottom, ImVec2(cx, cy), left, color);
    dl->AddTriangleFilled(bottom, right, ImVec2(cx, cy), color);
}

static bool PointNearKey(float mx, float my, float kx, float ky, float threshold = 6.0f)
{
    return (std::abs(mx - kx) <= threshold) && (std::abs(my - ky) <= threshold);
}

static float WrapDuration(float t, float dur, bool loop)
{
    if (!std::isfinite(t) || !std::isfinite(dur) || dur <= 0.0f) return 0.0f;
    if (loop) {
        t = fmod(t, dur);
        if (t < 0.0f) t += dur;
    } else {
        t = std::max(0.0f, std::min(dur, t));
    }
    return t;
}

static float SnapTimelineTime(float time)
{
    if (!s_SnapEnabled || !std::isfinite(time) || s_SnapInterval <= 0.0001f)
        return time;
    return std::round(time / s_SnapInterval) * s_SnapInterval;
}

template <typename KeyType>
static float ConstrainKeyTime(const std::vector<KeyType>& keys, int index,
                              float proposedTime, float duration)
{
    if (index < 0 || index >= static_cast<int>(keys.size()))
        return 0.0f;
    const float minimum = index == 0 ? 0.0f : keys[index - 1].Time + 0.001f;
    const float maximum = index + 1 >= static_cast<int>(keys.size())
        ? duration : keys[index + 1].Time - 0.001f;
    if (maximum < minimum)
        return keys[index].Time;
    return std::clamp(SnapTimelineTime(proposedTime), minimum, maximum);
}

static void InsertOrReplaceKeyVec3(std::vector<KeyframeVec3>& keys, float time, glm::vec3 value)
{
    for (size_t i = 0; i < keys.size(); ++i) {
        if (std::abs(keys[i].Time - time) < 0.001f) {
            keys[i].Value = value;
            return;
        }
    }
    KeyframeVec3 k;
    k.Time = time;
    k.Value = value;
    auto it = std::lower_bound(keys.begin(), keys.end(), k,
        [](const KeyframeVec3& a, const KeyframeVec3& b) { return a.Time < b.Time; });
    keys.insert(it, k);
}

static void InsertOrReplaceKeyQuat(std::vector<KeyframeQuat>& keys, float time, glm::quat value)
{
    for (size_t i = 0; i < keys.size(); ++i) {
        if (std::abs(keys[i].Time - time) < 0.001f) {
            keys[i].Value = value;
            return;
        }
    }
    KeyframeQuat k;
    k.Time = time;
    k.Value = value;
    auto it = std::lower_bound(keys.begin(), keys.end(), k,
        [](const KeyframeQuat& a, const KeyframeQuat& b) { return a.Time < b.Time; });
    keys.insert(it, k);
}

void TimelinePanel::Draw(Application& app)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float width = viewport->WorkSize.x;
    float height = viewport->WorkSize.y;

    ImGui::SetNextWindowPos(
        ImVec2(0, height - 340.0f),
        ImGuiCond_FirstUseEver
    );

    ImGui::SetNextWindowSize(
        ImVec2(width, 340.0f),
        ImGuiCond_FirstUseEver
    );

    // A janela pode ser redimensionada para menos de 600 px (por exemplo,
    // quando o editor está em uma janela estreita). O ImGui exige que o
    // tamanho mínimo nunca seja maior que o máximo; caso contrário, a build
    // Debug dispara um assert e chama abort().
    const float minTimelineWidth = std::min(600.0f, std::max(320.0f, width));
    const float minTimelineHeight = std::min(280.0f, std::max(180.0f, height));
    const float maxTimelineWidth = std::max(minTimelineWidth, width);
    const float maxTimelineHeight = std::max(minTimelineHeight, height * 0.7f);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(minTimelineWidth, minTimelineHeight),
        ImVec2(maxTimelineWidth, maxTimelineHeight)
    );

    ImGui::Begin(L("panel.timeline"), nullptr, ImGuiWindowFlags_NoCollapse);

    auto& entities = app.GetScene().GetEntities();
    Entity* selectedEntity = nullptr;
    const int selectedEntityIndex = app.GetSelectedEntityIndex();
    if (!entities.empty() &&
        selectedEntityIndex >= 0 &&
        selectedEntityIndex < static_cast<int>(entities.size())) {
        // Acesse diretamente o vetor validado. Isso evita o fallback de
        // GetSelectedEntity() durante a troca entre entidade/componente.
        selectedEntity = &entities[static_cast<size_t>(selectedEntityIndex)];
    }

    AnimationClip* selectedClip = selectedEntity
        ? selectedEntity->GetAnimationComponent().Clip.get() : nullptr;
    const uint64_t selectedEntityID = selectedEntity ? selectedEntity->GetID() : 0;
    if (selectedEntityID != s_LastSelectedEntityID || selectedClip != s_LastSelectedClip)
    {
        s_LastSelectedEntityID = selectedEntityID;
        s_LastSelectedClip = selectedClip;
        s_SelectedKeyType = -1;
        s_SelectedKeyIndex = -1;
        s_IsPlaying = false;
        s_PlaybackTime = selectedEntity && selectedClip
            ? selectedEntity->GetAnimationComponent().Time : 0.0f;
        // Force one preview sample when switching entity/clip, even if its
        // saved playhead happens to be at the same time as the previous clip.
        s_LastPlaybackTime = -9999.0f;
    }

    // ==================== TOP CONTROLS BAR ====================
    ImGui::BeginChild("##TopBar", ImVec2(0, 112), true);
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 btnSize = ImVec2(26, 26);

        // --- Playback Controls Row ---
        // Play/Pause
        if (s_IsPlaying) {
            if (ImGui::Button(UI("##pause"), btnSize)) s_IsPlaying = false;
            ImVec2 rmin = ImGui::GetItemRectMin(); ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 c = ImVec2((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
            float s = std::min((rmax.x - rmin.x), (rmax.y - rmin.y)) * 0.14f;
            draw->AddRectFilled(ImVec2(c.x - s*1.6f, c.y - s*0.8f), ImVec2(c.x - s*0.2f, c.y + s*0.8f), IM_COL32(255,255,255,255));
            draw->AddRectFilled(ImVec2(c.x + s*0.2f, c.y - s*0.8f), ImVec2(c.x + s*1.6f, c.y + s*0.8f), IM_COL32(255,255,255,255));
        } else {
            if (ImGui::Button(UI("##play"), btnSize)) s_IsPlaying = true;
            ImVec2 rmin = ImGui::GetItemRectMin(); ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 c = ImVec2((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
            float s = std::min((rmax.x - rmin.x), (rmax.y - rmin.y)) * 0.35f;
            draw->AddTriangleFilled(ImVec2(c.x - s*0.6f, c.y - s), ImVec2(c.x - s*0.6f, c.y + s), ImVec2(c.x + s*0.8f, c.y), IM_COL32(255,255,255,255));
        }

        ImGui::SameLine();
        // Stop
        if (ImGui::Button(UI("##stop"), btnSize))
        {
            s_IsPlaying = false;
            s_PlaybackTime = 0.0f;
        }
        ImVec2 rmin = ImGui::GetItemRectMin(); ImVec2 rmax = ImGui::GetItemRectMax();
        draw->AddRectFilled(ImVec2(rmin.x + 5, rmin.y + 5), ImVec2(rmax.x - 5, rmax.y - 5), IM_COL32(255,255,255,255));

        ImGui::SameLine();
        // Record button in timeline too
        bool isRec = ToolbarPanel::IsRecording();
        if (ImGui::Button(UI("##rectl"), btnSize)) {
            ToolbarPanel::SetRecording(!isRec);
        }
        ImVec2 recMin = ImGui::GetItemRectMin(); ImVec2 recMax = ImGui::GetItemRectMax();
        ImVec2 c = ImVec2((recMin.x + recMax.x) * 0.5f, (recMin.y + recMax.y) * 0.5f);
        float rr = std::min((recMax.x - recMin.x), (recMax.y - recMin.y)) * 0.32f;
        if (ToolbarPanel::IsRecording()) {
            float pulse = (sinf((float)ImGui::GetTime() * 6.0f) + 1.0f) * 0.5f;
            draw->AddCircleFilled(c, rr, IM_COL32(255, 50 + (int)(pulse*60), 50, 255));
        } else {
            draw->AddCircleFilled(c, rr, IM_COL32(200, 60, 60, 255));
        }

        ImGui::SameLine();
        // Keyframe button
        if (ImGui::Button(UI("##kftl"), btnSize)) {
            app.AddKeyframeAtCurrentTime();
        }
        rmin = ImGui::GetItemRectMin(); rmax = ImGui::GetItemRectMax();
        c = ImVec2((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
        float ks = std::min((rmax.x - rmin.x), (rmax.y - rmin.y)) * 0.32f;
        DrawDiamondKey(draw, c.x, c.y + 1, IM_COL32(255,255,255,255), ks);

        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        // Time display + scrubber numeric
        ImGui::Text(UI("%.2fs"), s_PlaybackTime);
        ImGui::SameLine(120);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat(UI("##time"), &s_PlaybackTime, 0.01f, 0.0f, 1000.0f, "%.2f s")) {
            if (selectedEntity) {
                AnimationComponent& anim = selectedEntity->GetAnimationComponent();
                if (anim.Clip) {
                    s_PlaybackTime = WrapDuration(s_PlaybackTime, anim.Clip->Duration, anim.Clip->Loop);
                }
            }
        }
        ImGui::SameLine();
        ImGui::Text(UI("/"));
        ImGui::SameLine();
        if (selectedEntity) {
            AnimationComponent& anim = selectedEntity->GetAnimationComponent();
            if (anim.Clip) {
                float tmpDur = anim.Clip->Duration;
                ImGui::SetNextItemWidth(70);
                if (ImGui::DragFloat(UI("##dur"), &tmpDur, 0.1f, 0.1f, 3600.0f, "%.1f s")) {
                    anim.Clip->Duration = std::clamp(tmpDur, 0.1f, 3600.0f);
                    s_PlaybackTime = WrapDuration(s_PlaybackTime, anim.Clip->Duration, anim.Clip->Loop);
                    app.MarkDirty();
                }
            } else {
                ImGui::Text(UI("-- s"));
            }
        } else {
            ImGui::Text(UI("-- s"));
        }

        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::SliderFloat(L("timeline.speed"), &s_TimeScale, 0.05f, 4.0f, "%.1fx");

        // --- Second row: clip settings ---
        ImGui::NewLine();
        if (selectedEntity) {
            AnimationComponent& anim = selectedEntity->GetAnimationComponent();

            if (anim.Clip) {
                // Enabled
                if (ImGui::Checkbox(L("timeline.enabled"), &anim.Enabled)) app.MarkDirty();
                ImGui::SameLine();

                // Clip name
                static char nameBuf[128];
                static uint64_t nameEntityID = 0;
                static AnimationClip* nameClip = nullptr;
                if (ImGui::IsWindowAppearing() || nameEntityID != selectedEntity->GetID() ||
                    nameClip != anim.Clip.get()) {
                    // Não use strncpy_s com o tamanho total como contagem:
                    // nomes maiores podem acionar o invalid-parameter handler
                    // do CRT na build Debug e encerrar o editor com abort().
                    const std::string& clipName = anim.Clip->Name;
                    const size_t copyLength = std::min(
                        clipName.size(), sizeof(nameBuf) - 1);
                    std::memcpy(nameBuf, clipName.data(), copyLength);
                    nameBuf[copyLength] = '\0';
                    nameEntityID = selectedEntity->GetID();
                    nameClip = anim.Clip.get();
                }
                ImGui::SetNextItemWidth(180);
                if (ImGui::InputText(L("timeline.clip"), nameBuf, sizeof(nameBuf))) {
                    anim.Clip->Name = nameBuf;
                    app.MarkDirty();
                }
                ImGui::SameLine();

                if (ImGui::Checkbox(L("inspector.loop"), &anim.Clip->Loop)) app.MarkDirty();
                ImGui::SameLine();

                ImGui::SetNextItemWidth(70);
                if (ImGui::DragFloat(L("timeline.weight"), &anim.Weight, 0.01f, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
                ImGui::SameLine();

                ImGui::SetNextItemWidth(70);
                if (ImGui::DragFloat(L("timeline.play_speed"), &anim.Speed, 0.05f, 0.0f, 10.0f, "%.1fx")) app.MarkDirty();

                ImGui::NewLine();
                ImGui::Separator();
                ImGui::TextUnformatted(UI("Suavização e easing"));
                ImGui::SameLine();
                if (ImGui::Checkbox(UI("Ativar##TimelineSmoothing"), &anim.UseSmoothing))
                    app.MarkDirty();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(105.0f);
                if (ImGui::DragFloat(UI("Intensidade##TimelineSmoothness"), &anim.Smoothness,
                    0.25f, 0.0f, 50.0f, "%.2f"))
                    app.MarkDirty();
                ImGui::SameLine();
                const char* easingNames[] = {
                    "Linear", "SmoothStep", "Ease In", "Ease Out", "Ease In Out"
                };
                int easing = static_cast<int>(anim.Easing);
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo(UI("Easing##TimelineEasing"), &easing, easingNames, IM_ARRAYSIZE(easingNames)))
                {
                    anim.Easing = static_cast<AnimationComponent::EasingType>(easing);
                    app.MarkDirty();
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::DragFloat(UI("Blend In##TimelineBlendIn"), &anim.BlendIn,
                    0.05f, 0.0f, 60.0f, "%.2fs"))
                    app.MarkDirty();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::DragFloat(UI("Blend Out##TimelineBlendOut"), &anim.BlendOut,
                    0.05f, 0.0f, 60.0f, "%.2fs"))
                    app.MarkDirty();
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.3f, 1), "%s", L("timeline.no_clip"));
                ImGui::SameLine();
                if (ImGui::Button(L("timeline.create_clip"))) {
                    Transform& t = selectedEntity->GetTransform();
                    anim.Clip = AnimationClip::CreateDefault(
                        selectedEntity->GetName() + "Anim",
                        3.0f,
                        glm::vec3(t.Position.X, t.Position.Y, t.Position.Z),
                        t.Rotation,
                        glm::vec3(t.Scale.X, t.Scale.Y, t.Scale.Z));
                    app.MarkDirty();
                }
            }
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", L("timeline.select_entity"));
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    // ==================== TRACKS AREA ====================
    if (selectedEntity) {
        AnimationComponent& anim = selectedEntity->GetAnimationComponent();
        if (anim.Clip) {
            AnimationClip* clip = anim.Clip.get();
            float duration = std::max(clip->Duration, 0.1f);

            // Layout: left panel (track names) + right panel (ruler + tracks)
            float leftW = 230.0f;
            float availX = ImGui::GetContentRegionAvail().x;
            float rightW = std::max(400.0f, availX - leftW - ImGui::GetStyle().ItemSpacing.x);

            ImGui::BeginChild("##TrackNames", ImVec2(leftW, 0), true);
            {
                // Header
                ImGui::AlignTextToFramePadding();
                ImGui::Text(UI("  %s"), L("timeline.track"));
                ImGui::Separator();

                int rowCounter = 0;
                auto TrackLine = [&](const char* label, const char* id, bool* pVisible,
                                     bool isGroup, const ImVec4& color, float lineH = 22.0f) {
                    ImVec2 csr = ImGui::GetCursorScreenPos();
                    ImVec2 wsz = ImGui::GetContentRegionAvail();
                    ImVec2 bgMin = csr;
                    ImVec2 bgMax = ImVec2(csr.x + wsz.x, csr.y + lineH);
                    ImU32 bg = (rowCounter % 2 == 0) ? TrackBgColor : TrackBgAltColor;
                    rowCounter++;
                    ImGui::GetWindowDrawList()->AddRectFilled(bgMin, bgMax, bg);
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(bgMax.x, bgMin.y), ImVec2(bgMax.x, bgMax.y), TrackBorderColor);

                    ImGui::PushID(id);
                    ImGui::SetCursorScreenPos(ImVec2(csr.x, csr.y + 3));
                    if (isGroup) {
                        if (ImGui::ArrowButton("##arr", *pVisible ? ImGuiDir_Down : ImGuiDir_Right)) {
                            *pVisible = !*pVisible;
                        }
                        ImGui::SameLine();
                    } else {
                        ImGui::SetCursorScreenPos(ImVec2(csr.x + 16, csr.y + 3));
                        ImGui::SetNextItemWidth(14.0f);
                        ImGui::Checkbox("##visible", pVisible);
                        ImGui::SameLine();
                    }
                    ImGui::TextColored(color, "%s", label);
                    ImGui::PopID();
                    ImGui::SetCursorScreenPos(ImVec2(csr.x, csr.y + lineH));
                };

                // Position group
                TrackLine("Position", "PositionGroup", &s_PositionExpanded, true, ImVec4(0.45f, 0.9f, 0.45f, 1.0f));
                if (s_PositionExpanded) {
                    TrackLine("X", "PositionX", &s_ShowPosX, false, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    TrackLine("Y", "PositionY", &s_ShowPosY, false, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                    TrackLine("Z", "PositionZ", &s_ShowPosZ, false, ImVec4(0.4f, 0.55f, 1.0f, 1.0f));
                }

                // Rotation group
                TrackLine("Rotation", "RotationGroup", &s_RotationExpanded, true, ImVec4(0.45f, 0.6f, 1.0f, 1.0f));
                if (s_RotationExpanded) {
                    TrackLine("X", "RotationX", &s_ShowRotX, false, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    TrackLine("Y", "RotationY", &s_ShowRotY, false, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                    TrackLine("Z", "RotationZ", &s_ShowRotZ, false, ImVec4(0.4f, 0.55f, 1.0f, 1.0f));
                }

                // Scale group
                TrackLine("Scale", "ScaleGroup", &s_ScaleExpanded, true, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                if (s_ScaleExpanded) {
                    TrackLine("X", "ScaleX", &s_ShowSclX, false, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    TrackLine("Y", "ScaleY", &s_ShowSclY, false, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                    TrackLine("Z", "ScaleZ", &s_ShowSclZ, false, ImVec4(0.4f, 0.55f, 1.0f, 1.0f));
                }

                // TrackLine usa SetCursorScreenPos para desenhar cada linha.
                // Submeta um item depois da última posição manual para que o
                // ImGui registre corretamente o limite inferior do child.
                // Sem isso, a build Debug dispara o assertion de layout.
                ImGui::Dummy(ImVec2(1.0f, 1.0f));
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("##TracksArea", ImVec2(rightW, 0), true);
            {
                ImGui::Checkbox(UI("Snap temporal##Timeline"), &s_SnapEnabled);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(92.0f);
                ImGui::DragFloat(UI("Passo##TimelineSnap"), &s_SnapInterval,
                                 0.01f, 0.01f, 10.0f, "%.2f s");
                ImGui::SameLine();
                ImGui::TextDisabled(UI("RMB remove uma chave próxima; Delete remove a selecionada."));
                ImGui::Separator();

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 tracksStart = ImGui::GetCursorScreenPos();
                float contentW = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                float pxPerSec = contentW / duration;

                const float rulerH = 28.0f;
                const float lineH = 22.0f;

                // --- RULER ---
                {
                    ImVec2 rMin = tracksStart;
                    ImVec2 rMax = ImVec2(tracksStart.x + contentW, tracksStart.y + rulerH);
                    dl->AddRectFilled(rMin, rMax, IM_COL32(28, 28, 34, 255));

                    // Tick marks
                    float idealPxPerTick = 80.0f;
                    float stepSec = idealPxPerTick / pxPerSec;
                    // snap step to nice numbers
                    float niceSteps[] = {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f};
                    float tickStep = 1.0f;
                    for (float ns : niceSteps) { if (ns >= stepSec) { tickStep = ns; break; } }
                    int minorPerMajor = 5;

                    for (float t = 0.0f; t <= duration + tickStep * 0.5f; t += tickStep / minorPerMajor) {
                        float x = tracksStart.x + t * pxPerSec;
                        bool isMajor = (std::fmod(t, tickStep) < 0.0001f || std::fmod(t, tickStep) > tickStep - 0.0001f);
                        float yTop = tracksStart.y + (isMajor ? rulerH * 0.1f : rulerH * 0.5f);
                        dl->AddLine(ImVec2(x, yTop), ImVec2(x, tracksStart.y + rulerH), isMajor ? IM_COL32(140,140,150,255) : IM_COL32(90,90,100,200));
                        if (isMajor) {
                            char tbuf[32];
                            snprintf(tbuf, sizeof(tbuf), "%.2f", t);
                            dl->AddText(ImVec2(x + 2, tracksStart.y + 2), IM_COL32(200,200,210,255), tbuf);
                        }
                    }
                    dl->AddLine(ImVec2(rMin.x, rMax.y - 1), ImVec2(rMax.x, rMax.y - 1), TrackBorderColor);
                }

                // --- TRACKS ---
                ImVec2 t0 = ImVec2(tracksStart.x, tracksStart.y + rulerH);
                float cy = t0.y;
                int rc = 0;

                struct TrackInfo { KeyTrackType type; ImU32 color; };

                std::vector<TrackInfo> trackList;
                trackList.reserve(12);
                trackList.push_back({ TRACK_POS_ALL, KeyColorPos });
                if (s_PositionExpanded) {
                    if (s_ShowPosX) trackList.push_back({ TRACK_POS_X, IM_COL32(255, 90, 90, 255) });
                    if (s_ShowPosY) trackList.push_back({ TRACK_POS_Y, IM_COL32(90, 255, 90, 255) });
                    if (s_ShowPosZ) trackList.push_back({ TRACK_POS_Z, IM_COL32(90, 140, 255, 255) });
                }
                trackList.push_back({ TRACK_ROT_ALL, KeyColorRot });
                if (s_RotationExpanded) {
                    if (s_ShowRotX) trackList.push_back({ TRACK_ROT_X, IM_COL32(255, 90, 90, 255) });
                    if (s_ShowRotY) trackList.push_back({ TRACK_ROT_Y, IM_COL32(90, 255, 90, 255) });
                    if (s_ShowRotZ) trackList.push_back({ TRACK_ROT_Z, IM_COL32(90, 140, 255, 255) });
                }
                trackList.push_back({ TRACK_SCL_ALL, KeyColorScl });
                if (s_ScaleExpanded) {
                    if (s_ShowSclX) trackList.push_back({ TRACK_SCL_X, IM_COL32(255, 90, 90, 255) });
                    if (s_ShowSclY) trackList.push_back({ TRACK_SCL_Y, IM_COL32(90, 255, 90, 255) });
                    if (s_ShowSclZ) trackList.push_back({ TRACK_SCL_Z, IM_COL32(90, 140, 255, 255) });
                }
                int numTracks = (int)trackList.size();

                // First pass: draw backgrounds (no input yet)
                float curY = cy;
                for (int i = 0; i < numTracks; ++i) {
                    ImU32 bg = (rc % 2 == 0) ? TrackBgColor : TrackBgAltColor;
                    rc++;
                    dl->AddRectFilled(ImVec2(t0.x, curY), ImVec2(t0.x + contentW, curY + lineH), bg);
                    dl->AddLine(ImVec2(t0.x, curY + lineH - 1), ImVec2(t0.x + contentW, curY + lineH - 1), TrackBorderColor);
                    // Midline dashed
                    for (float xpos = t0.x; xpos < t0.x + contentW; xpos += 8.0f) {
                        dl->AddLine(ImVec2(xpos, curY + lineH*0.5f), ImVec2(xpos + 4.0f, curY + lineH*0.5f), IM_COL32(70,70,80,255));
                    }
                    curY += lineH;
                }

                // Vertical time grid lines aligned with ruler major ticks
                {
                    float niceSteps[] = {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f};
                    float idealPxPerTick = 80.0f;
                    float stepSec = idealPxPerTick / pxPerSec;
                    float tickStep = 1.0f;
                    for (float ns : niceSteps) { if (ns >= stepSec) { tickStep = ns; break; } }
                    for (float t = 0.0f; t <= duration + tickStep*0.5f; t += tickStep) {
                        float x = t0.x + t * pxPerSec;
                        dl->AddLine(ImVec2(x, t0.y), ImVec2(x, curY), IM_COL32(60,60,70,140));
                    }
                }

                // Draw playhead (behind keys, above backgrounds)
                float phX = t0.x + s_PlaybackTime * pxPerSec;
                dl->AddLine(ImVec2(phX, tracksStart.y), ImVec2(phX, curY + 2), PlayheadColor, 2.0f);
                // Playhead triangle on top
                dl->AddTriangleFilled(
                    ImVec2(phX - 6, tracksStart.y),
                    ImVec2(phX + 6, tracksStart.y),
                    ImVec2(phX, tracksStart.y + 7),
                    PlayheadColor
                );

                // --- Draw keys ---
                auto DrawKeysVec3Single = [&](const std::vector<KeyframeVec3>& keys, KeyTrackType tType, float yC, ImU32 col) {
                    for (size_t i = 0; i < keys.size(); ++i) {
                        float x = t0.x + keys[i].Time * pxPerSec;
                        bool selected = (s_SelectedKeyType == tType) && ((int)i == s_SelectedKeyIndex);
                        ImU32 kc = selected ? KeyColorSelected : col;
                        DrawDiamondKey(dl, x, yC, kc, 4.5f);
                    }
                };

                auto DrawKeysQuatTrack = [&](const std::vector<KeyframeQuat>& keys, KeyTrackType tType, float yC, ImU32 col) {
                    for (size_t i = 0; i < keys.size(); ++i) {
                        float x = t0.x + keys[i].Time * pxPerSec;
                        bool selected = (s_SelectedKeyType >= TRACK_ROT_ALL && s_SelectedKeyType <= TRACK_ROT_Z) &&
                            (s_SelectedKeyType == TRACK_ROT_ALL || s_SelectedKeyType == tType) &&
                            (int)i == s_SelectedKeyIndex;
                        ImU32 kc = selected ? KeyColorSelected : col;
                        DrawDiamondKey(dl, x, yC, kc, 4.5f);
                    }
                };

                curY = cy;
                for (int i = 0; i < numTracks; ++i) {
                    float yC = curY + lineH * 0.5f;
                    float yMd = curY + lineH * 0.5f;
                    switch (trackList[i].type) {
                        case TRACK_POS_ALL: {
                            for (size_t k = 0; k < clip->Position.Keys.size(); ++k) {
                                float x = t0.x + clip->Position.Keys[k].Time * pxPerSec;
                                bool selected = (s_SelectedKeyType == TRACK_POS_ALL) && ((int)k == s_SelectedKeyIndex);
                                ImU32 kc = selected ? KeyColorSelected : KeyColorPos;
                                DrawDiamondKey(dl, x, yMd, kc, 5.0f);
                            }
                            break;
                        }
                        case TRACK_POS_X: DrawKeysVec3Single(clip->Position.Keys, TRACK_POS_X, yC, IM_COL32(255, 90, 90, 255)); break;
                        case TRACK_POS_Y: DrawKeysVec3Single(clip->Position.Keys, TRACK_POS_Y, yC, IM_COL32(90, 255, 90, 255)); break;
                        case TRACK_POS_Z: DrawKeysVec3Single(clip->Position.Keys, TRACK_POS_Z, yC, IM_COL32(90, 140, 255, 255)); break;
                        case TRACK_ROT_ALL: DrawKeysQuatTrack(clip->Rotation.Keys, TRACK_ROT_ALL, yMd, KeyColorRot); break;
                        case TRACK_ROT_X: DrawKeysQuatTrack(clip->Rotation.Keys, TRACK_ROT_X, yC, IM_COL32(255, 90, 90, 255)); break;
                        case TRACK_ROT_Y: DrawKeysQuatTrack(clip->Rotation.Keys, TRACK_ROT_Y, yC, IM_COL32(90, 255, 90, 255)); break;
                        case TRACK_ROT_Z: DrawKeysQuatTrack(clip->Rotation.Keys, TRACK_ROT_Z, yC, IM_COL32(90, 140, 255, 255)); break;
                        case TRACK_SCL_ALL: {
                            for (size_t k = 0; k < clip->Scale.Keys.size(); ++k) {
                                float x = t0.x + clip->Scale.Keys[k].Time * pxPerSec;
                                bool selected = (s_SelectedKeyType == TRACK_SCL_ALL) && ((int)k == s_SelectedKeyIndex);
                                ImU32 kc = selected ? KeyColorSelected : KeyColorScl;
                                DrawDiamondKey(dl, x, yMd, kc, 5.0f);
                            }
                            break;
                        }
                        case TRACK_SCL_X: DrawKeysVec3Single(clip->Scale.Keys, TRACK_SCL_X, yC, IM_COL32(255, 90, 90, 255)); break;
                        case TRACK_SCL_Y: DrawKeysVec3Single(clip->Scale.Keys, TRACK_SCL_Y, yC, IM_COL32(90, 255, 90, 255)); break;
                        case TRACK_SCL_Z: DrawKeysVec3Single(clip->Scale.Keys, TRACK_SCL_Z, yC, IM_COL32(90, 140, 255, 255)); break;
                        default: break;
                    }
                    curY += lineH;
                }

                // --- Interactive surface ---
                float totalTracksH = numTracks * lineH;
                ImVec2 surfMin = ImVec2(t0.x, tracksStart.y);
                ImVec2 surfMax = ImVec2(t0.x + contentW, tracksStart.y + rulerH + totalTracksH);
                ImGui::SetCursorScreenPos(surfMin);
                ImGui::InvisibleButton("##trackssurf", ImVec2(surfMax.x - surfMin.x, surfMax.y - surfMin.y));

                bool hovered = ImGui::IsItemHovered();
                ImVec2 mouse = ImGui::GetMousePos();
                float localMx = mouse.x - t0.x;
                float localMy = mouse.y - tracksStart.y;

                // Compute which track line the mouse is over
                int hitTrackIdx = -1;
                if (localMy >= rulerH) {
                    float rel = localMy - rulerH;
                    int idx = (int)(rel / lineH);
                    if (idx >= 0 && idx < numTracks) {
                        hitTrackIdx = idx;
                    }
                }
                KeyTrackType hitTrackType = (hitTrackIdx >= 0) ? trackList[hitTrackIdx].type : TRACK_COUNT;

                float mouseTime = std::clamp(SnapTimelineTime(localMx / pxPerSec), 0.0f, duration);

                // Left click on ruler / empty = set playhead
                if (hovered && ImGui::IsMouseClicked(0) && !ImGui::IsMouseDoubleClicked(0)) {
                    // Test if clicking on a key first
                    bool clickedKey = false;
                    if (hitTrackIdx >= 0) {
                        float yC = rulerH + hitTrackIdx * lineH + lineH * 0.5f;
                        auto TestKeysVec3 = [&](const std::vector<KeyframeVec3>& ks, KeyTrackType tt) {
                            for (size_t i = 0; i < ks.size(); ++i) {
                                float kx = ks[i].Time * pxPerSec;
                                if (PointNearKey(localMx, localMy, kx, yC, 7.0f)) {
                                    s_SelectedKeyType = tt;
                                    s_SelectedKeyIndex = (int)i;
                                    s_PlaybackTime = ks[i].Time;
                                    clickedKey = true;
                                    return true;
                                }
                            }
                            return false;
                        };
                        auto TestKeysQuat = [&](const std::vector<KeyframeQuat>& ks, KeyTrackType tt) {
                            for (size_t i = 0; i < ks.size(); ++i) {
                                float kx = ks[i].Time * pxPerSec;
                                if (PointNearKey(localMx, localMy, kx, yC, 7.0f)) {
                                    s_SelectedKeyType = tt;
                                    s_SelectedKeyIndex = (int)i;
                                    s_PlaybackTime = ks[i].Time;
                                    clickedKey = true;
                                    return true;
                                }
                            }
                            return false;
                        };
                        switch (hitTrackType) {
                            case TRACK_POS_ALL: TestKeysVec3(clip->Position.Keys, TRACK_POS_ALL); break;
                            case TRACK_POS_X: TestKeysVec3(clip->Position.Keys, TRACK_POS_X); break;
                            case TRACK_POS_Y: TestKeysVec3(clip->Position.Keys, TRACK_POS_Y); break;
                            case TRACK_POS_Z: TestKeysVec3(clip->Position.Keys, TRACK_POS_Z); break;
                            case TRACK_ROT_ALL: TestKeysQuat(clip->Rotation.Keys, TRACK_ROT_ALL); break;
                            case TRACK_ROT_X: TestKeysQuat(clip->Rotation.Keys, TRACK_ROT_X); break;
                            case TRACK_ROT_Y: TestKeysQuat(clip->Rotation.Keys, TRACK_ROT_Y); break;
                            case TRACK_ROT_Z: TestKeysQuat(clip->Rotation.Keys, TRACK_ROT_Z); break;
                            case TRACK_SCL_ALL: TestKeysVec3(clip->Scale.Keys, TRACK_SCL_ALL); break;
                            case TRACK_SCL_X: TestKeysVec3(clip->Scale.Keys, TRACK_SCL_X); break;
                            case TRACK_SCL_Y: TestKeysVec3(clip->Scale.Keys, TRACK_SCL_Y); break;
                            case TRACK_SCL_Z: TestKeysVec3(clip->Scale.Keys, TRACK_SCL_Z); break;
                            default: break;
                        }
                    }

                    if (!clickedKey) {
                        s_PlaybackTime = mouseTime;
                        s_SelectedKeyType = -1;
                        s_SelectedKeyIndex = -1;
                    }
                }

                // Drag selected key in time
                if (s_SelectedKeyType >= 0 && s_SelectedKeyIndex >= 0 && hovered && ImGui::IsMouseDragging(0, 2.0f)) {
                    auto DragVec3 = [&](std::vector<KeyframeVec3>& ks) {
                        if (s_SelectedKeyIndex >= 0 && s_SelectedKeyIndex < static_cast<int>(ks.size())) {
                            const float newTime = ConstrainKeyTime(ks, s_SelectedKeyIndex, mouseTime, duration);
                            if (std::abs(ks[s_SelectedKeyIndex].Time - newTime) > 0.00001f) {
                                ks[s_SelectedKeyIndex].Time = newTime;
                                app.MarkDirty();
                            }
                        }
                    };
                    auto DragQuat = [&](std::vector<KeyframeQuat>& ks) {
                        if (s_SelectedKeyIndex >= 0 && s_SelectedKeyIndex < static_cast<int>(ks.size())) {
                            const float newTime = ConstrainKeyTime(ks, s_SelectedKeyIndex, mouseTime, duration);
                            if (std::abs(ks[s_SelectedKeyIndex].Time - newTime) > 0.00001f) {
                                ks[s_SelectedKeyIndex].Time = newTime;
                                app.MarkDirty();
                            }
                        }
                    };
                    switch (s_SelectedKeyType) {
                        case TRACK_POS_ALL: case TRACK_POS_X: case TRACK_POS_Y: case TRACK_POS_Z: DragVec3(clip->Position.Keys); break;
                        case TRACK_ROT_ALL: case TRACK_ROT_X: case TRACK_ROT_Y: case TRACK_ROT_Z: DragQuat(clip->Rotation.Keys); break;
                        case TRACK_SCL_ALL: case TRACK_SCL_X: case TRACK_SCL_Y: case TRACK_SCL_Z: DragVec3(clip->Scale.Keys); break;
                        default: break;
                    }
                }

                // Double-click = add key(s) on that track
                if (hovered && ImGui::IsMouseDoubleClicked(0)) {
                    Transform& tf = selectedEntity->GetTransform();
                    glm::vec3 pos(tf.Position.X, tf.Position.Y, tf.Position.Z);
                    glm::quat rot = tf.Rotation;
                    glm::vec3 scl(tf.Scale.X, tf.Scale.Y, tf.Scale.Z);
                    const float t = mouseTime;
                    s_PlaybackTime = t;
                    bool inserted = true;

                    if (hitTrackType == TRACK_POS_ALL) {
                        InsertOrReplaceKeyVec3(clip->Position.Keys, t, pos);
                    } else if (hitTrackType >= TRACK_POS_X && hitTrackType <= TRACK_POS_Z) {
                        int axis = hitTrackType - TRACK_POS_X;
                        glm::vec3 v = clip->Position.Keys.empty() ? pos : clip->Position.Evaluate(t);
                        v[axis] = pos[axis];
                        InsertOrReplaceKeyVec3(clip->Position.Keys, t, v);
                    } else if (hitTrackType == TRACK_ROT_ALL) {
                        InsertOrReplaceKeyQuat(clip->Rotation.Keys, t, rot);
                    } else if (hitTrackType >= TRACK_ROT_X && hitTrackType <= TRACK_ROT_Z) {
                        glm::quat q = clip->Rotation.Keys.empty() ? rot : clip->Rotation.Evaluate(t);
                        glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
                        glm::vec3 curE = glm::degrees(glm::eulerAngles(rot));
                        int axis = hitTrackType - TRACK_ROT_X;
                        euler[axis] = curE[axis];
                        glm::quat newQ = glm::quat(glm::radians(euler));
                        InsertOrReplaceKeyQuat(clip->Rotation.Keys, t, newQ);
                    } else if (hitTrackType == TRACK_SCL_ALL) {
                        InsertOrReplaceKeyVec3(clip->Scale.Keys, t, scl);
                    } else if (hitTrackType >= TRACK_SCL_X && hitTrackType <= TRACK_SCL_Z) {
                        int axis = hitTrackType - TRACK_SCL_X;
                        glm::vec3 v = clip->Scale.Keys.empty() ? scl : clip->Scale.Evaluate(t);
                        v[axis] = scl[axis];
                        InsertOrReplaceKeyVec3(clip->Scale.Keys, t, v);
                    }
                    else
                        inserted = false;
                    if (inserted)
                    {
                        s_LastPlaybackTime = -9999.0f;
                        app.MarkDirty();
                    }
                }

                // Right click only removes a key when the pointer is visually
                // close to it; tolerance stays constant at every zoom level.
                if (hovered && ImGui::IsMouseClicked(1)) {
                    bool removed = false;
                    if (hitTrackIdx >= 0) {
                        const float timeThreshold = 7.0f / pxPerSec;
                        auto RemoveNearVec3 = [&](std::vector<KeyframeVec3>& ks) -> bool {
                            int bestIdx = -1; float bestDist = timeThreshold;
                            for (size_t i = 0; i < ks.size(); ++i) {
                                float d = std::abs(ks[i].Time - mouseTime);
                                if (d < bestDist) { bestDist = d; bestIdx = (int)i; }
                            }
                            if (bestIdx >= 0) { ks.erase(ks.begin() + bestIdx); return true; }
                            return false;
                        };
                        auto RemoveNearQuat = [&](std::vector<KeyframeQuat>& ks) -> bool {
                            int bestIdx = -1; float bestDist = timeThreshold;
                            for (size_t i = 0; i < ks.size(); ++i) {
                                float d = std::abs(ks[i].Time - mouseTime);
                                if (d < bestDist) { bestDist = d; bestIdx = (int)i; }
                            }
                            if (bestIdx >= 0) { ks.erase(ks.begin() + bestIdx); return true; }
                            return false;
                        };
                        switch (hitTrackType) {
                            case TRACK_POS_ALL: case TRACK_POS_X: case TRACK_POS_Y: case TRACK_POS_Z: removed = RemoveNearVec3(clip->Position.Keys); break;
                            case TRACK_ROT_ALL: case TRACK_ROT_X: case TRACK_ROT_Y: case TRACK_ROT_Z: removed = RemoveNearQuat(clip->Rotation.Keys); break;
                            case TRACK_SCL_ALL: case TRACK_SCL_X: case TRACK_SCL_Y: case TRACK_SCL_Z: removed = RemoveNearVec3(clip->Scale.Keys); break;
                            default: break;
                        }
                    }
                    if (removed) app.MarkDirty();
                    s_SelectedKeyType = -1; s_SelectedKeyIndex = -1;
                }

                // Delete key with Delete key
                if (hovered && s_SelectedKeyType >= 0 && s_SelectedKeyIndex >= 0 &&
                    !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                    bool deleted = false;
                    auto DeleteVec3 = [&](std::vector<KeyframeVec3>& ks) {
                        if (s_SelectedKeyIndex >= 0 && s_SelectedKeyIndex < (int)ks.size()) {
                            ks.erase(ks.begin() + s_SelectedKeyIndex);
                            deleted = true;
                        }
                    };
                    auto DeleteQuat = [&](std::vector<KeyframeQuat>& ks) {
                        if (s_SelectedKeyIndex >= 0 && s_SelectedKeyIndex < (int)ks.size()) {
                            ks.erase(ks.begin() + s_SelectedKeyIndex);
                            deleted = true;
                        }
                    };
                    switch (s_SelectedKeyType) {
                        case TRACK_POS_ALL: case TRACK_POS_X: case TRACK_POS_Y: case TRACK_POS_Z: DeleteVec3(clip->Position.Keys); break;
                        case TRACK_ROT_ALL: case TRACK_ROT_X: case TRACK_ROT_Y: case TRACK_ROT_Z: DeleteQuat(clip->Rotation.Keys); break;
                        case TRACK_SCL_ALL: case TRACK_SCL_X: case TRACK_SCL_Y: case TRACK_SCL_Z: DeleteVec3(clip->Scale.Keys); break;
                        default: break;
                    }
                    s_SelectedKeyType = -1; s_SelectedKeyIndex = -1;
                    if (deleted) app.MarkDirty();
                }
            }
            ImGui::EndChild();

            // --- Selected key inspector at bottom of timeline ---
            ImGui::Separator();
            if (s_SelectedKeyType >= 0 && s_SelectedKeyIndex >= 0) {
                ImGui::BeginChild("##KeyInsp", ImVec2(0, 50), true);
                auto InspectVec3 = [&](const char* title, std::vector<KeyframeVec3>& ks, int idx, const char* compA, const char* compB, const char* compC) {
                    if (idx < 0 || idx >= (int)ks.size()) return;
                    ImGui::TextColored(ImVec4(0.9f,0.9f,0.4f,1), "%s  |  Key #%d", title, idx);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    float keyTime = ks[idx].Time;
                    if (ImGui::DragFloat(UI("##kt"), &keyTime, 0.01f, 0.0f, duration, "%.2f s")) {
                        const float constrained = ConstrainKeyTime(ks, idx, keyTime, duration);
                        if (std::abs(ks[idx].Time - constrained) > 0.00001f) {
                            ks[idx].Time = constrained;
                            app.MarkDirty();
                        }
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90);
                    bool valueChanged = ImGui::DragFloat(compA, &ks[idx].Value.x, 0.05f);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90);
                    valueChanged |= ImGui::DragFloat(compB, &ks[idx].Value.y, 0.05f);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90);
                    valueChanged |= ImGui::DragFloat(compC, &ks[idx].Value.z, 0.05f);
                    if (valueChanged) app.MarkDirty();
                };
                auto InspectQuat = [&](const char* title, std::vector<KeyframeQuat>& ks, int idx) {
                    if (idx < 0 || idx >= (int)ks.size()) return;
                    ImGui::TextColored(ImVec4(0.9f,0.9f,0.4f,1), "%s  |  Key #%d", title, idx);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    float keyTime = ks[idx].Time;
                    if (ImGui::DragFloat(UI("##qt"), &keyTime, 0.01f, 0.0f, duration, "%.2f s")) {
                        const float constrained = ConstrainKeyTime(ks, idx, keyTime, duration);
                        if (std::abs(ks[idx].Time - constrained) > 0.00001f) {
                            ks[idx].Time = constrained;
                            app.MarkDirty();
                        }
                    }
                    glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::normalize(ks[idx].Value)));
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    if (ImGui::DragFloat(UI("Rot X"), &euler.x, 1.0f)) {
                        ks[idx].Value = glm::normalize(glm::quat(glm::radians(euler)));
                        app.MarkDirty();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    if (ImGui::DragFloat(UI("Rot Y"), &euler.y, 1.0f)) {
                        ks[idx].Value = glm::normalize(glm::quat(glm::radians(euler)));
                        app.MarkDirty();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    if (ImGui::DragFloat(UI("Rot Z"), &euler.z, 1.0f)) {
                        ks[idx].Value = glm::normalize(glm::quat(glm::radians(euler)));
                        app.MarkDirty();
                    }
                };

                switch (s_SelectedKeyType) {
                    case TRACK_POS_ALL: case TRACK_POS_X: case TRACK_POS_Y: case TRACK_POS_Z:
                        InspectVec3("Position Key", clip->Position.Keys, s_SelectedKeyIndex, "Pos X", "Pos Y", "Pos Z"); break;
                    case TRACK_ROT_ALL: case TRACK_ROT_X: case TRACK_ROT_Y: case TRACK_ROT_Z:
                        InspectQuat("Rotation Key", clip->Rotation.Keys, s_SelectedKeyIndex); break;
                    case TRACK_SCL_ALL: case TRACK_SCL_X: case TRACK_SCL_Y: case TRACK_SCL_Z:
                        InspectVec3("Scale Key", clip->Scale.Keys, s_SelectedKeyIndex, "Scl X", "Scl Y", "Scl Z"); break;
                    default: ImGui::TextUnformatted(L("timeline.no_key")); break;
                }
                ImGui::EndChild();
            } else {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1),
                    "  LMB: Playhead / Select key    |    Drag: Move key    |    Double-click: Add key    |    RMB / Delete: Remove key");
            }

            // The Timeline has its own preview clock while editing. Runtime
            // playback remains owned by AnimationSystem::Update in Play mode.
            if (app.GetEditorState() == Application::EditorState::Edit) {
                const bool recording = ToolbarPanel::IsRecording();
                const bool playheadWasChanged =
                    std::abs(s_PlaybackTime - s_LastPlaybackTime) > 0.00001f;

                anim.Playing = false;
                if (s_IsPlaying || recording) {
                    const float frameDelta = std::max(0.0f, ImGui::GetIO().DeltaTime);
                    const float clipSpeed = s_IsPlaying ? std::max(0.0f, anim.Speed) : 1.0f;
                    const float nextTime = s_PlaybackTime + frameDelta * s_TimeScale * clipSpeed;
                    if (clip->Loop) {
                        s_PlaybackTime = WrapDuration(nextTime, duration, true);
                    } else if (nextTime >= duration) {
                        s_PlaybackTime = duration;
                        s_IsPlaying = false;
                    } else {
                        s_PlaybackTime = nextTime;
                    }
                }

                anim.Time = s_PlaybackTime;

                // Use the engine's animation evaluator for both scrubbing and
                // preview playback. While recording or manipulating a gizmo,
                // leave the edited transform alone so auto-key can capture it.
                const bool shouldPreview = s_IsPlaying || playheadWasChanged;
                if (anim.Enabled && shouldPreview && !recording && !ImGuizmo::IsUsing())
                    AnimationSystem::ApplyPoseAtTime(*selectedEntity, s_PlaybackTime, 0.0f);

                s_LastPlaybackTime = s_PlaybackTime;
            } else {
                // Do not let the editor's local preview clock fight the game.
                s_IsPlaying = false;
                s_PlaybackTime = WrapDuration(anim.Time, duration, clip->Loop);
                s_LastPlaybackTime = s_PlaybackTime;
            }
        }
    }

    ImGui::End();
}
