#pragma once

#include <glad/glad.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

class Scene;
class Entity;
class Texture;

// Renderizador de UI 2D nativo do engine.
//
// Diferente do editor (que desenha a UI como overlay do ImGui no viewport),
// este renderizador desenha os elementos ScreenGui/GuiObject diretamente no
// framebuffer da cena, permitindo que a interface apareça no jogo exportado.
//
// O pipeline é próprio: shader GLSL dedicado, quads em lote (batching),
// clipping via scissor e SDF para cantos arredondados/bordas. O atlas de
// glifos é reaproveitado da fonte do ImGui (fonte TTF já rasterizada), que é
// usada apenas como fonte de dados de textura/glifos.
class UIRenderer
{
public:
    void Init();
    void Shutdown();

    bool IsReady() const { return m_Program != 0; }

    // Desenha a UI da cena no framebuffer atualmente vinculado.
    // canvasWidth/Height: resolução em pixels do alvo atual.
    void Render(Scene& scene, int canvasWidth, int canvasHeight);

private:
    struct Vertex
    {
        float x, y;             // posição (pixels, origem no canto superior esquerdo)
        float u, v;             // coordenadas de textura
        float r, g, b, a;       // cor
        float mode;             // 0 = retângulo (SDF), 1 = imagem, 2 = glifo
        float cornerRadius;     // raio dos cantos
        float borderWidth;      // espessura da borda
        float softness;         // largura do antialias
        float pad;
        float br, bg, bb, ba;   // cor da borda
        float cx, cy;           // centro do retângulo (para o SDF)
        float hx, hy;           // meia extensão do retângulo
    };

    void EnsureBuffers();
    void Flush();
    void PushQuad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d);

    void AddRect(float x, float y, float w, float h, const float color[4],
                 float cornerRadius, float borderWidth, const float borderColor[4]);
    void AddImage(GLuint texture, float x, float y, float w, float h, const float color[4]);
    // Nine-slice: mantém a espessura das bordas e estica só o centro.
    void AddImage9Slice(GLuint texture, int texWidth, int texHeight,
                        float x, float y, float w, float h,
                        float left, float top, float right, float bottom,
                        const float color[4]);
    void AddText(GLuint atlasTexture, void* imFont, const char* text,
                 float x, float y, float boxWidth, float boxHeight,
                 float fontSize, const float color[4], int align,
                 bool richText = false, float markupScale = 1.0f);
    // Largura em pixels de uma linha de texto (fonte já rasterizada).
    float MeasureTextWidth(void* imFont, const char* text, float fontSize) const;

    void ApplyScissor(int x, int y, int w, int h);

    GLuint m_Program = 0;
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    GLsizei m_VertexCount = 0;
    GLint m_LocProj = -1;
    GLint m_LocTexture = -1;

    GLuint m_WhiteTexture = 0;
    GLuint m_BoundTexture = 0;

    int m_CanvasWidth = 0;
    int m_CanvasHeight = 0;
    float m_ScaleX = 1.0f;
    float m_ScaleY = 1.0f;

    std::vector<Vertex> m_Vertices;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_ImageCache;
};
