#pragma once

#include <memory>
#include <string>

#include "../../Renderer/Texture/Texture.h"
#include "../../Engine/Assets/GUID/GUID.h"

enum class MaterialType
{
    Plastic  = 0,
    Metal    = 1,
    Wood     = 2,
    Neon     = 3,
    Glass    = 4,
    Rubber   = 5,
    Ceramic  = 6,
    Fabric   = 7,
    Mirror   = 8    // reflexo planar — qualidade controlada por MirrorQuality
};

// Qualidade do reflexo para MaterialType::Mirror.
// Independente da GraphicsQuality global; pode ser sobrescrita por entidade.
enum class MirrorQuality
{
    InheritProject  = 0,   // segue o GraphicsQuality do projeto
    UltraPerformance = 1,  // sem reflexo real — cor sólida (sem render extra)
    Performance     = 2,   // 128×128, a cada 4 frames
    Balanced        = 3,   // 256×256, a cada 2 frames
    HighFidelity    = 4,   // 512×512, todo frame
    Ultra           = 5,   // 1024×1024, todo frame, sem bias de clip
};

// Controla como a textura se comporta quando a entidade e escalada.
// Stretch     : UV estica com a escala (comportamento padrao do OpenGL).
// FixedDrawing: UV mantem proporcao/tamanho original sem esticar e sem repetir.
// TileMesh    : Em vez de esticar a malha, duplica o mesh em grid pelo eixo escalado.
// WorldSpace  : UV calculado em espaco de mundo (triplanar) - continuo entre objetos.
// AutoTileFace: Repete a textura em cada face de acordo com o tamanho do objeto (tiling local por face).
// NineSlice   : Preserva as bordas/cantos do desenho e estica apenas o miolo (ideal para molduras, botoes e paineis).
enum class UVScaleMode
{
    Stretch      = 0, // Esticar normal com a escala
    FixedDrawing = 1, // Fixar desenho (mantem tamanho original, sem esticar e sem duplicar)
    TileMesh     = 2, // Duplicar mesh ao escalar (blocos modulares)
    WorldSpace   = 3, // Repetir textura no mundo (triplanar)
    AutoTileFace = 4, // Repetir na Face (Tile por Face sem esticar)
    NineSlice    = 5  // 9-Fatias / Nine-Slice (Preservar bordas e cantos)
};

class Material
{
public:

    Material();

    Material(const Material& other);
    Material& operator=(const Material& other);

    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;

    std::string Name;

    // O material define somente a resposta a luz. A cor vem do objeto/mesh.
    MaterialType Type;

    // Legado: mantido para compatibilidade com assets antigos, mas não é
    // usado pelo renderer como cor do material.
    float Color[4];

    // Parâmetros PBR opcionais. Quando habilitados, substituem o preset por
    // tipo sem quebrar materiais antigos que ainda usam Plastic/Metal/Wood.
    bool PBRParametersEnabled = false;
    float Roughness = 0.45f;
    float Metallic = 0.0f;
    float Specular = 0.5f;
    float EmissionStrength = 0.0f;
    float Alpha = 1.0f;
    float NormalStrength = 1.0f;
    std::string NormalMapPath;
    std::string EmissionTexturePath;

    std::shared_ptr<Texture> DiffuseTexture;
    std::shared_ptr<Texture> NormalTexture;
    std::shared_ptr<Texture> EmissionTexture;

    std::string TexturePath;
    std::string TextureName;
    std::string TextureGUID;

    // UV Scale Mode: como a textura se comporta ao escalar a entidade
    UVScaleMode ScaleMode  = UVScaleMode::Stretch;
    float UVWorldScale     = 1.0f;  // menor = textura repete mais (WorldSpace)
    float TileSize         = 1.0f;  // tamanho de cada tile em unidades de mundo (TileMesh)
    int FixedAlignment     = 0;     // 0=Centro, 1=Canto Inf-Esq, 2=Canto Inf-Dir, 3=Canto Sup-Esq, 4=Canto Sup-Dir
    float FaceTileRepeat   = 1.0f;  // Fator de repeticao para AutoTileFace
    float NineSliceBorder  = 0.25f; // Margem das bordas (0.0 a 0.45) para NineSlice

    // ── Mirror (MaterialType::Mirror) ──────────────────────────────────────
    MirrorQuality MirrorReflectionQuality = MirrorQuality::InheritProject;
    float         MirrorBlur              = 0.0f;  // 0 = espelho perfeito, 1 = muito difuso
    float         MirrorIntensity         = 1.0f;  // 0 = cor base pura, 1 = reflexo total
    float         MirrorClipOffset        = 0.05f; // afasta o clip plane para evitar artefatos

    std::string GUID;
};
