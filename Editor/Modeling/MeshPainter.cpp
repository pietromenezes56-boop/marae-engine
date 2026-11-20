#include "MeshPainter.h"
#include "../../Renderer/Texture/Texture.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Components/MeshComponent.h"
#include "../../Scene/Components/MaterialComponent.h"
#include "../../Scene/Components/ColorComponent.h"
#include "../../Renderer/Mesh/Mesh.h"
#include "../../Assets/MaterialDatabase/MaterialDatabase.h"
#include "MeshTopology.h"

#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <iostream>

// ---------------------------------------------------------------------------
// Variáveis estáticas de configuração do Pincel
// ---------------------------------------------------------------------------
std::unordered_map<uint64_t, std::shared_ptr<PaintCanvas>> MeshPainter::s_EntityCanvases;

bool      MeshPainter::s_BrushActive   = false;
glm::vec4 MeshPainter::s_BrushColor    = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f);
float     MeshPainter::s_BrushRadius   = 16.0f;
float     MeshPainter::s_BrushSoftness = 0.5f;
float     MeshPainter::s_BrushOpacity  = 1.0f;

bool      MeshPainter::s_RulerMode        = false;
bool      MeshPainter::s_RulerDrawing     = false;
glm::vec2 MeshPainter::s_RulerStartUV     = glm::vec2(0.0f);
ImVec2    MeshPainter::s_RulerStartScreen = ImVec2(0.0f, 0.0f);

bool      MeshPainter::s_HasLastHitUV     = false;
glm::vec2 MeshPainter::s_LastHitUV        = glm::vec2(0.0f);

// ---------------------------------------------------------------------------
// PaintCanvas
// ---------------------------------------------------------------------------

PaintCanvas::PaintCanvas(int width, int height)
    : m_Width(width), m_Height(height), m_Dirty(false)
{
    m_Pixels.resize(static_cast<size_t>(m_Width * m_Height * 4), 255);
    InitTexture();
}

PaintCanvas::~PaintCanvas()
{
    // A instância Texture (shared_ptr) já cuida de deletar seu OpenGL texture ID
    m_Texture = nullptr;
    m_TextureID = 0;
}

void PaintCanvas::InitTexture()
{
    glGenTextures(1, &m_TextureID);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_Pixels.data());

    glBindTexture(GL_TEXTURE_2D, 0);

    m_Texture = std::make_shared<Texture>();
    m_Texture->ID = m_TextureID;
    m_Texture->Width = m_Width;
    m_Texture->Height = m_Height;
    m_Texture->Channels = 4;
    m_Texture->Loaded = true;
}

void PaintCanvas::Clear(const glm::vec4& color)
{
    uint8_t r = static_cast<uint8_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    uint8_t a = static_cast<uint8_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f);

    for (size_t i = 0; i < m_Pixels.size(); i += 4)
    {
        m_Pixels[i + 0] = r;
        m_Pixels[i + 1] = g;
        m_Pixels[i + 2] = b;
        m_Pixels[i + 3] = a;
    }

    m_Dirty = true;
    m_DirtyMinX = 0; m_DirtyMinY = 0;
    m_DirtyMaxX = m_Width; m_DirtyMaxY = m_Height;
    UpdateGPU();
}

void PaintCanvas::SetPixel(int x, int y, const glm::vec4& color, float blendFactor)
{
    if (x < 0 || x >= m_Width || y < 0 || y >= m_Height) return;

    size_t index = static_cast<size_t>((y * m_Width + x) * 4);
    float srcR = color.r * 255.0f;
    float srcG = color.g * 255.0f;
    float srcB = color.b * 255.0f;
    float alpha = std::clamp(blendFactor * color.a, 0.0f, 1.0f);

    float dstR = static_cast<float>(m_Pixels[index + 0]);
    float dstG = static_cast<float>(m_Pixels[index + 1]);
    float dstB = static_cast<float>(m_Pixels[index + 2]);

    m_Pixels[index + 0] = static_cast<uint8_t>(std::clamp(dstR + (srcR - dstR) * alpha, 0.0f, 255.0f));
    m_Pixels[index + 1] = static_cast<uint8_t>(std::clamp(dstG + (srcG - dstG) * alpha, 0.0f, 255.0f));
    m_Pixels[index + 2] = static_cast<uint8_t>(std::clamp(dstB + (srcB - dstB) * alpha, 0.0f, 255.0f));
    m_Pixels[index + 3] = 255;

    if (!m_Dirty)
    {
        m_Dirty = true;
        m_DirtyMinX = x; m_DirtyMaxX = x + 1;
        m_DirtyMinY = y; m_DirtyMaxY = y + 1;
    }
    else
    {
        m_DirtyMinX = std::min(m_DirtyMinX, x);
        m_DirtyMaxX = std::max(m_DirtyMaxX, x + 1);
        m_DirtyMinY = std::min(m_DirtyMinY, y);
        m_DirtyMaxY = std::max(m_DirtyMaxY, y + 1);
    }
}

void PaintCanvas::PaintStamp(const glm::vec2& uv, const glm::vec4& color, float radius, float softness, float opacity)
{
    // Converte UV para coordenadas de pixel
    float u = std::clamp(uv.x, 0.0f, 1.0f);
    float v = std::clamp(uv.y, 0.0f, 1.0f);

    float cx = u * static_cast<float>(m_Width);
    float cy = v * static_cast<float>(m_Height);

    int minX = std::max(0, static_cast<int>(std::floor(cx - radius)));
    int maxX = std::min(m_Width, static_cast<int>(std::ceil(cx + radius)));
    int minY = std::max(0, static_cast<int>(std::floor(cy - radius)));
    int maxY = std::min(m_Height, static_cast<int>(std::ceil(cy + radius)));

    float radiusSq = radius * radius;
    float softVal = std::clamp(softness, 0.0f, 0.999f);
    float innerRadius = radius * (1.0f - softVal);

    for (int y = minY; y < maxY; ++y)
    {
        for (int x = minX; x < maxX; ++x)
        {
            float dx = static_cast<float>(x) - cx;
            float dy = static_cast<float>(y) - cy;
            float distSq = dx * dx + dy * dy;

            if (distSq > radiusSq) continue;

            float dist = std::sqrt(distSq);
            float factor = 1.0f;

            if (dist > innerRadius && radius > innerRadius)
            {
                // Falloff suave baseado em cosseno cúbico
                float t = (dist - innerRadius) / (radius - innerRadius);
                factor = 0.5f * (1.0f + std::cos(t * 3.14159265f));
            }

            SetPixel(x, y, color, factor * opacity);
        }
    }

    UpdateGPU();
}

void PaintCanvas::PaintLine(const glm::vec2& startUV, const glm::vec2& endUV, const glm::vec4& color,
                            float radius, float softness, float opacity)
{
    float startX = startUV.x * static_cast<float>(m_Width);
    float startY = startUV.y * static_cast<float>(m_Height);
    float endX = endUV.x * static_cast<float>(m_Width);
    float endY = endUV.y * static_cast<float>(m_Height);

    float dist = std::hypot(endX - startX, endY - startY);
    float stepSize = std::max(1.0f, radius * 0.25f);
    int steps = std::max(1, static_cast<int>(std::ceil(dist / stepSize)));

    for (int i = 0; i <= steps; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        glm::vec2 currentUV = glm::mix(startUV, endUV, t);
        PaintStamp(currentUV, color, radius, softness, opacity);
    }
}

void PaintCanvas::PaintTriangleUV(const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec4& color)
{
    glm::vec2 p0(uv0.x * m_Width, uv0.y * m_Height);
    glm::vec2 p1(uv1.x * m_Width, uv1.y * m_Height);
    glm::vec2 p2(uv2.x * m_Width, uv2.y * m_Height);

    int minX = std::max(0, static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x }))));
    int maxX = std::min(m_Width, static_cast<int>(std::ceil(std::max({ p0.x, p1.x, p2.x }))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y }))));
    int maxY = std::min(m_Height, static_cast<int>(std::ceil(std::max({ p0.y, p1.y, p2.y }))));

    auto edgeFunction = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) -> float {
        return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
    };

    float area = edgeFunction(p0, p1, p2);
    if (std::abs(area) < 0.0001f) return;

    for (int y = minY; y < maxY; ++y)
    {
        for (int x = minX; x < maxX; ++x)
        {
            glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            float w0 = edgeFunction(p1, p2, p);
            float w1 = edgeFunction(p2, p0, p);
            float w2 = edgeFunction(p0, p1, p);

            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0))
            {
                SetPixel(x, y, color, 1.0f);
            }
        }
    }

    UpdateGPU();
}

void PaintCanvas::UpdateGPU()
{
    if (!m_Dirty || m_TextureID == 0) return;

    m_DirtyMinX = std::clamp(m_DirtyMinX, 0, m_Width);
    m_DirtyMaxX = std::clamp(m_DirtyMaxX, 0, m_Width);
    m_DirtyMinY = std::clamp(m_DirtyMinY, 0, m_Height);
    m_DirtyMaxY = std::clamp(m_DirtyMaxY, 0, m_Height);

    int updateW = m_DirtyMaxX - m_DirtyMinX;
    int updateH = m_DirtyMaxY - m_DirtyMinY;

    if (updateW <= 0 || updateH <= 0)
    {
        m_Dirty = false;
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_TextureID);

    // Upload rápido da região afetada
    if (updateW == m_Width && updateH == m_Height)
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height,
                        GL_RGBA, GL_UNSIGNED_BYTE, m_Pixels.data());
    }
    else
    {
        std::vector<uint8_t> subBuffer(static_cast<size_t>(updateW * updateH * 4));
        for (int row = 0; row < updateH; ++row)
        {
            int srcY = m_DirtyMinY + row;
            size_t srcOffset = static_cast<size_t>((srcY * m_Width + m_DirtyMinX) * 4);
            size_t dstOffset = static_cast<size_t>(row * updateW * 4);
            std::copy_n(&m_Pixels[srcOffset], updateW * 4, &subBuffer[dstOffset]);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, m_DirtyMinX, m_DirtyMinY, updateW, updateH,
                        GL_RGBA, GL_UNSIGNED_BYTE, subBuffer.data());
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    m_Dirty = false;
}

// ---------------------------------------------------------------------------
// MeshPainter — Gerenciador de pintura de malhas, faces e pincel 3D
// ---------------------------------------------------------------------------

void MeshPainter::SetBrushActive(bool active)
{
    s_BrushActive = active;
    if (!active)
    {
        EndBrushStroke();
        CancelRuler();
    }
}

bool MeshPainter::EnsureAtlasUV(Entity& entity)
{
    auto& meshComp = entity.GetMeshComponent();
    if (!meshComp.MeshData || !meshComp.MeshData->IsValid()) return false;

    auto vertices = meshComp.MeshData->GetVertices();
    if (vertices.size() < 3) return false;

    // Verifica se as faces compartilham UV idêntico (ex: cubo com 36 vértices e UVs repetidos)
    bool needsAtlas = false;
    if (vertices.size() == 36) // Cubo padrão da engine
    {
        needsAtlas = true;
    }
    else
    {
        // Se todas as coordenadas UV forem exatamente 0 ou 1 repetidamente, precisa de desdobramento
        int uvZeroOneCount = 0;
        for (const auto& v : vertices)
        {
            if ((v.U == 0.0f || v.U == 1.0f) && (v.V == 0.0f || v.V == 1.0f))
                ++uvZeroOneCount;
        }
        if (uvZeroOneCount == static_cast<int>(vertices.size()))
            needsAtlas = true;
    }

    if (!needsAtlas) return false;

    // Desdobra em atlas (grid de faces)
    // Para um cubo de 6 faces (6 quads = 12 triângulos = 36 vértices, 6 por face):
    if (vertices.size() == 36)
    {
        // Layout 3 colunas x 2 linhas
        const float colW = 1.0f / 3.0f;
        const float rowH = 1.0f / 2.0f;

        for (int face = 0; face < 6; ++face)
        {
            int col = face % 3;
            int row = face / 3;

            float uMin = col * colW + 0.005f;
            float uMax = (col + 1) * colW - 0.005f;
            float vMin = row * rowH + 0.005f;
            float vMax = (row + 1) * rowH - 0.005f;

            // Cada face do cubo tem 6 vértices (2 triângulos)
            int baseIdx = face * 6;
            // Triângulo 1: (0,0), (1,0), (1,1)
            vertices[baseIdx + 0].U = uMin; vertices[baseIdx + 0].V = vMin;
            vertices[baseIdx + 1].U = uMax; vertices[baseIdx + 1].V = vMin;
            vertices[baseIdx + 2].U = uMax; vertices[baseIdx + 2].V = vMax;

            // Triângulo 2: (0,0), (1,1), (0,1)
            vertices[baseIdx + 3].U = uMin; vertices[baseIdx + 3].V = vMin;
            vertices[baseIdx + 4].U = uMax; vertices[baseIdx + 4].V = vMax;
            vertices[baseIdx + 5].U = uMin; vertices[baseIdx + 5].V = vMax;
        }

        meshComp.MeshData->UpdateVertices(vertices, "CubeAtlasUV");
        return true;
    }

    return false;
}

std::shared_ptr<PaintCanvas> MeshPainter::GetOrCreateCanvas(Entity& entity)
{
    uint64_t id = entity.GetID();
    auto it = s_EntityCanvases.find(id);
    if (it != s_EntityCanvases.end())
    {
        return it->second;
    }

    EnsureAtlasUV(entity);

    auto canvas = std::make_shared<PaintCanvas>(512, 512);

    // Inicializa o canvas com a cor base da entidade
    const auto& color = entity.GetColorComponent().Color;
    canvas->Clear(glm::vec4(color.X, color.Y, color.Z, 1.0f));

    // Conecta a textura no material da entidade (MaterialData e banco de materiais)
    auto& matComp = entity.GetMaterialComponent();
    matComp.MaterialData.DiffuseTexture = canvas->GetTexture();
    matComp.MaterialData.TexturePath = "[PaintCanvas]";

    if (!matComp.MaterialGUID.empty())
    {
        Material* mat = MaterialDatabase::GetMaterialByGUID(matComp.MaterialGUID);
        if (mat)
        {
            mat->DiffuseTexture = canvas->GetTexture();
            mat->TexturePath = "[PaintCanvas]";
        }
    }

    // A cor base da entidade e do material é normalizada para branco para que
    // o shader projete 100% da cor da textura sem escurecimento indevido
    entity.GetColorComponent().Color = Vector3(1.0f, 1.0f, 1.0f);
    matComp.MaterialData.Color[0] = 1.0f;
    matComp.MaterialData.Color[1] = 1.0f;
    matComp.MaterialData.Color[2] = 1.0f;

    s_EntityCanvases[id] = canvas;
    return canvas;
}

void MeshPainter::PaintSelectedFaces(Entity& entity, const std::vector<int>& faceIndices, const glm::vec4& color)
{
    if (faceIndices.empty()) return;

    auto canvas = GetOrCreateCanvas(entity);
    if (!canvas) return;

    auto& meshComp = entity.GetMeshComponent();
    if (!meshComp.MeshData) return;

    const auto& vertices = meshComp.MeshData->GetVertices();
    const MeshTopology topology = MeshTopology::FromVertices(vertices);

    for (int faceIdx : faceIndices)
    {
        if (faceIdx < 0 || faceIdx >= static_cast<int>(topology.Faces.size())) continue;

        const auto& face = topology.Faces[static_cast<size_t>(faceIdx)];
        const auto& v0 = topology.Vertices[face.Vertices[0]].Value;
        const auto& v1 = topology.Vertices[face.Vertices[1]].Value;
        const auto& v2 = topology.Vertices[face.Vertices[2]].Value;

        canvas->PaintTriangleUV(glm::vec2(v0.U, v0.V),
                                glm::vec2(v1.U, v1.V),
                                glm::vec2(v2.U, v2.V),
                                color);
    }
}

void MeshPainter::FillEntity(Entity& entity, const glm::vec4& color)
{
    auto canvas = GetOrCreateCanvas(entity);
    if (canvas)
    {
        canvas->Clear(color);
    }
}

void MeshPainter::ApplyBrushStroke(Entity& entity, const glm::vec2& uv)
{
    auto canvas = GetOrCreateCanvas(entity);
    if (!canvas) return;

    if (!s_HasLastHitUV)
    {
        canvas->PaintStamp(uv, s_BrushColor, s_BrushRadius, s_BrushSoftness, s_BrushOpacity);
        s_LastHitUV = uv;
        s_HasLastHitUV = true;
    }
    else
    {
        if (glm::distance(s_LastHitUV, uv) > 0.0005f)
        {
            canvas->PaintLine(s_LastHitUV, uv, s_BrushColor, s_BrushRadius, s_BrushSoftness, s_BrushOpacity);
            s_LastHitUV = uv;
        }
    }
}

void MeshPainter::EndBrushStroke()
{
    s_HasLastHitUV = false;
}

void MeshPainter::ApplyBrushStamp(Entity& entity, const glm::vec2& uv)
{
    auto canvas = GetOrCreateCanvas(entity);
    if (canvas)
    {
        canvas->PaintStamp(uv, s_BrushColor, s_BrushRadius, s_BrushSoftness, s_BrushOpacity);
    }
}

void MeshPainter::ApplyRulerLine(Entity& entity, const glm::vec2& startUV, const glm::vec2& endUV)
{
    auto canvas = GetOrCreateCanvas(entity);
    if (canvas)
    {
        canvas->PaintLine(startUV, endUV, s_BrushColor, s_BrushRadius, s_BrushSoftness, s_BrushOpacity);
    }
}

void MeshPainter::StartRuler(const glm::vec2& uv, const ImVec2& screenPos)
{
    s_RulerDrawing = true;
    s_RulerStartUV = uv;
    s_RulerStartScreen = screenPos;
}

void MeshPainter::CancelRuler()
{
    s_RulerDrawing = false;
}
