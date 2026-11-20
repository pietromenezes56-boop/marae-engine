#pragma once

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <imgui.h>

class Entity;
class Mesh;
class Texture;

// ---------------------------------------------------------------------------
// PaintCanvas — Buffer de textura dinâmica editável em tempo real
// ---------------------------------------------------------------------------
class PaintCanvas
{
public:
    PaintCanvas(int width = 512, int height = 512);
    ~PaintCanvas();

    // Impede cópia rasa de recursos de textura OpenGL
    PaintCanvas(const PaintCanvas&) = delete;
    PaintCanvas& operator=(const PaintCanvas&) = delete;

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    unsigned int GetTextureID() const { return m_TextureID; }
    const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }
    std::shared_ptr<Texture> GetTexture() const { return m_Texture; }

    void Clear(const glm::vec4& color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    // Carimbo circular do pincel com raio, suavidade (falloff) e opacidade
    void PaintStamp(const glm::vec2& uv, const glm::vec4& color, float radius, float softness, float opacity);

    // Linha reta contínua usando interpolação de carimbos (Modo Régua)
    void PaintLine(const glm::vec2& startUV, const glm::vec2& endUV, const glm::vec4& color,
                   float radius, float softness, float opacity);

    // Preenche um triângulo no espaço UV com cor sólida (usado para pintar faces inteiras)
    void PaintTriangleUV(const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec4& color);

    // Envia os pixels modificados para a GPU de forma rápida via glTexSubImage2D
    void UpdateGPU();

private:
    void InitTexture();
    void SetPixel(int x, int y, const glm::vec4& color, float blendFactor);

    int m_Width = 512;
    int m_Height = 512;
    unsigned int m_TextureID = 0;
    std::vector<uint8_t> m_Pixels;
    std::shared_ptr<Texture> m_Texture;
    bool m_Dirty = false;

    // Região suja para uploads parciais rápidos
    int m_DirtyMinX = 0, m_DirtyMinY = 0;
    int m_DirtyMaxX = 0, m_DirtyMaxY = 0;
};

// ---------------------------------------------------------------------------
// MeshPainter — Gerenciador de pintura de malhas, faces e pincel 3D
// ---------------------------------------------------------------------------
class MeshPainter
{
public:
    // Garante que o objeto tenha UVs desdobradas em atlas para que faces distintas
    // não compartilhem a mesma coordenada UV
    static bool EnsureAtlasUV(Entity& entity);

    // Obtém ou cria o canvas de pintura da entidade
    static std::shared_ptr<PaintCanvas> GetOrCreateCanvas(Entity& entity);

    // Pinta todas as faces selecionadas com a cor especificada
    static void PaintSelectedFaces(Entity& entity, const std::vector<int>& faceIndices, const glm::vec4& color);

    // Preenche a textura inteira com uma cor
    static void FillEntity(Entity& entity, const glm::vec4& color);

    // Aplica traço de pincel 3D contínuo (com interpolação fluida ao arrastar o mouse)
    static void ApplyBrushStroke(Entity& entity, const glm::vec2& uv);
    static void EndBrushStroke();

    // Aplica traço de pincel 3D dado o ponto UV de impacto
    static void ApplyBrushStamp(Entity& entity, const glm::vec2& uv);

    // Aplica linha reta (Régua) entre dois pontos UV
    static void ApplyRulerLine(Entity& entity, const glm::vec2& startUV, const glm::vec2& endUV);

    // ---- Configurações Globais do Pincel ----
    static bool IsBrushActive() { return s_BrushActive; }
    // Desativar o pincel também encerra qualquer traço ou régua pendente.
    static void SetBrushActive(bool active);

    static glm::vec4& GetBrushColor() { return s_BrushColor; }
    static float& GetBrushRadius() { return s_BrushRadius; }
    static float& GetBrushSoftness() { return s_BrushSoftness; }
    static float& GetBrushOpacity() { return s_BrushOpacity; }

    static bool IsRulerMode() { return s_RulerMode; }
    static void SetRulerMode(bool ruler) { s_RulerMode = ruler; }

    static bool IsRulerDrawing() { return s_RulerDrawing; }
    static void StartRuler(const glm::vec2& uv, const ImVec2& screenPos);
    static void CancelRuler();
    static glm::vec2 GetRulerStartUV() { return s_RulerStartUV; }
    static ImVec2 GetRulerStartScreen() { return s_RulerStartScreen; }

private:
    static std::unordered_map<uint64_t, std::shared_ptr<PaintCanvas>> s_EntityCanvases;

    static bool s_BrushActive;
    static glm::vec4 s_BrushColor;
    static float s_BrushRadius;
    static float s_BrushSoftness;
    static float s_BrushOpacity;

    static bool s_RulerMode;
    static bool s_RulerDrawing;
    static glm::vec2 s_RulerStartUV;
    static ImVec2 s_RulerStartScreen;

    static bool s_HasLastHitUV;
    static glm::vec2 s_LastHitUV;
};
