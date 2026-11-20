#pragma once
// ---------------------------------------------------------------------------
// SurvivalGame — modo sobrevivência embutido (Marae Wildlands).
//
// Ativado automaticamente quando a cena contém uma entidade "SurvivalPlayer"
// (a entidade "Toolbox" é opcional e habilita o crafting). Durante o Play o
// jogo assume o controle do jogador em primeira pessoa sobre um mundo voxel
// procedural, com coleta, crafting, inventário e autosave em Save/.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <vector>

#include "../Voxel/VoxelWorld.h"

class Scene;
class Mesh;
struct DemoGameContext;

class SurvivalGame
{
public:
    bool IsActive() const { return m_Active; }

    void Reset();

    // Varre a cena atrás de SurvivalPlayer/Toolbox e ativa o modo se houver player.
    void ActivateFromScene(Scene& scene);

    // Gera (ou carrega do Save/) o mundo voxel e os objetos de gameplay.
    void GenerateWorld(const DemoGameContext& ctx);

    void Update(const DemoGameContext& ctx, float deltaTime);
    void DrawHUD(const DemoGameContext& ctx);

    // Persiste mundo voxel + estado do jogador em <projeto>/Save/.
    void Save(const DemoGameContext& ctx);

    // Chamado na saída do Play: salva o estado e restaura a malha original
    // do jogador no editor.
    void OnExitPlay(const DemoGameContext& ctx);

private:
    bool LoadGame(const DemoGameContext& ctx);
    void RebuildVoxelEntities(const DemoGameContext& ctx);

    bool m_Active = false;
    bool m_WorldGenerated = false;
    bool m_ToolboxOpen = false;
    int m_SelectedTool = 4;   // 0: Machado, 1: Picareta, 2: Enxada, 3: Tocha, 4: Mao, 5: Maca
    int m_SelectedBlock = static_cast<int>(VoxelWorld::BlockType::Dirt);
    int m_Wood = 0;
    int m_Planks = 0;
    int m_Stone = 0;
    int m_Dirt = 0;
    int m_Axes = 0;
    int m_Pickaxes = 0;
    int m_Apples = 0;
    float m_Hunger = 100.0f;
    float m_Health = 100.0f;
    float m_BobTime = 0.0f;
    float m_ToolSwing = 0.0f;
    float m_AutosaveTimer = 0.0f;
    uint64_t m_PlayerID = 0;
    uint64_t m_ToolboxID = 0;
    uint64_t m_HeldToolID = 0;
    uint64_t m_ArmID = 0;
    bool m_MouseWasDown = false;
    bool m_InteractWasDown = false;
    bool m_PlaceWasDown = false;
    bool m_FirstMouse = true;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    std::vector<uint64_t> m_ResourceIDs;
    std::vector<uint64_t> m_VoxelEntityIDs;
    VoxelWorld m_VoxelWorld;
    std::shared_ptr<Mesh> m_PlayerEditorMesh;
};
