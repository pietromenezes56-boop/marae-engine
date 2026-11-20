#include "Shader.h"

#include <glad/glad.h>

#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <atomic>
#include <string>
#include <algorithm>

static std::atomic<int> s_ShaderCount{ 0 };

namespace
{
    bool CheckShaderStage(unsigned int shader, const char* label)
    {
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE)
            return true;

        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(std::max(1, logLength)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << "[Shader] Falha ao compilar " << label << ": " << log << std::endl;
        FILE* f = nullptr;
        fopen_s(&f, "shader_error.log", "a");
        if (f) { fprintf(f, "[Shader] Falha ao compilar %s: %s\n", label, log.c_str()); fclose(f); }
        return false;
    }

    bool CheckShaderProgram(unsigned int program, const char* label)
    {
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_TRUE)
            return true;

        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(std::max(1, logLength)), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::cerr << "[Shader] Falha ao vincular " << label << ": " << log << std::endl;
        FILE* f = nullptr;
        fopen_s(&f, "shader_error.log", "a");
        if (f) { fprintf(f, "[Shader] Falha ao vincular %s: %s\n", label, log.c_str()); fclose(f); }
        return false;
    }
}

Shader::Shader()
    : m_ProgramID(0)
{
}

// Note: move operations removed for this instrumentation run; Shader remains copyable.

bool Shader::Create()
{
    const char* vertexShaderSource =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec3 aNormal;\n"
        "layout(location = 2) in vec2 aTexCoord;\n"
        "layout(location = 3) in uvec4 aJointIndices;\n"
        "layout(location = 4) in vec4 aJointWeights;\n"
        "\n"
        "out vec2 TexCoord;\n"
        "out vec3 Normal;\n"
        "out vec3 WorldPos;\n"
        "out vec4 FragPosLightSpace;\n"
        "out vec4 FragPosShadowCascade[4];\n"
        "out float ViewDepth;\n"
        "\n"
        "uniform mat4 u_Model;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "uniform mat4 u_LightSpaceMatrix;\n"
        "uniform mat4 u_ShadowCascadeMatrices[4];\n"
        "uniform mat4 u_SkinMatrices[48];\n"
        "uniform int u_SkinningEnabled;\n"
        "\n"
        "void main()\n"
        "{\n"
        "   mat4 skinMatrix = mat4(1.0);\n"
        "   float totalWeight = dot(aJointWeights, vec4(1.0));\n"
        "   if (u_SkinningEnabled != 0 && totalWeight > 0.0001) {\n"
        "       skinMatrix = u_SkinMatrices[aJointIndices.x] * aJointWeights.x +\n"
        "                    u_SkinMatrices[aJointIndices.y] * aJointWeights.y +\n"
        "                    u_SkinMatrices[aJointIndices.z] * aJointWeights.z +\n"
        "                    u_SkinMatrices[aJointIndices.w] * aJointWeights.w;\n"
        "       skinMatrix = skinMatrix * (1.0 / totalWeight);\n"
        "   }\n"
        "   vec4 skinnedPosition = skinMatrix * vec4(aPos, 1.0);\n"
        "   vec4 worldPosition = u_Model * skinnedPosition;\n"
        "   TexCoord = aTexCoord;\n"
        "   Normal = mat3(transpose(inverse(u_Model * skinMatrix))) * aNormal;\n"
        "   WorldPos = worldPosition.xyz;\n"
        "   FragPosLightSpace = u_LightSpaceMatrix * worldPosition;\n"
        "   for (int cascade = 0; cascade < 4; ++cascade)\n"
        "       FragPosShadowCascade[cascade] = u_ShadowCascadeMatrices[cascade] * worldPosition;\n"
        "   ViewDepth = -(u_View * worldPosition).z;\n"
        "   gl_Position = u_Projection * u_View * worldPosition;\n"
        "}\n";

    // ---------------------------------------------------------------------------
    // Fragment shader de ILUMINACAO HDR PURA (sem post-processamento inline).
    // A pipeline de Post-Processing aplica: ColorGrading, LUT, ToneMapping,
    // Vignette, FilmGrain, Chromatic, Sharpen, Bloom, DoF, MotionBlur etc.
    //
    // Mantemos os uniforms legados declarados para nao quebrar a API C++,
    // mas somente os de iluminacao + fog + global lighting new sao usados.
    // ---------------------------------------------------------------------------
    const char* fragmentShaderSource =
        "#version 330 core\n"
        "\n"
        "uniform vec3 u_Color;\n"
        "uniform float u_Opacity;\n"
        "uniform float u_Metallic;\n"
        "uniform float u_Roughness;\n"
        "uniform float u_Specular;\n"
        "uniform float u_EmissionStrength;\n"
        "uniform sampler2D u_Diffuse;\n"
        "uniform sampler2D u_NormalMap;\n"
        "uniform sampler2D u_EmissionMap;\n"
        "uniform int u_HasNormalMap;\n"
        "uniform int u_HasEmissionMap;\n"
        "uniform float u_NormalStrength;\n"
        "uniform int u_UVMode;\n"
        "uniform float u_UVWorldScale;\n"
        "uniform vec3 u_ObjectScale;\n"
        "uniform vec2 u_UVAtlasGrid;\n"
        "uniform vec4 u_CanvasBgColor;\n"
        "uniform vec4 u_UVExtraParams;\n"
        "uniform sampler2D u_ShadowMap;\n"
        "uniform sampler2D u_ShadowOpacityMap;\n"
        "uniform sampler2D u_ShadowCascadeMaps[4];\n"
        "uniform sampler2DArray u_ShadowCascadeOpacityMap;\n"
        "uniform mat4 u_ShadowCascadeMatrices[4];\n"
        "uniform float u_ShadowCascadeFarPlanes[4];\n"
        "uniform float u_ShadowCascadeDepthRanges[4];\n"
        "uniform vec2 u_ShadowCascadeWorldTexelSizes[4];\n"
        "uniform float u_DirectionalSourceAngularRadius;\n"
        "uniform int u_ShadowCascadeCount;\n"
        "uniform samplerCube u_PointShadowMap0;\n"
        "uniform samplerCube u_PointShadowMap1;\n"
        "uniform samplerCube u_PointShadowMap2;\n"
        "uniform samplerCube u_PointShadowMap3;\n"
        "uniform samplerCube u_PointShadowOpacityMap0;\n"
        "uniform samplerCube u_PointShadowOpacityMap1;\n"
        "uniform samplerCube u_PointShadowOpacityMap2;\n"
        "uniform samplerCube u_PointShadowOpacityMap3;\n"
        "uniform float u_PointShadowFarPlanes[4];\n"
        "uniform float u_PointShadowAngularTexelSize;\n"
        "uniform int u_PointShadowEnabled[4];\n"
        "uniform int u_UseShadows;\n"
        "uniform int u_DirectionalShadowsEnabled;\n"
        "uniform int u_DirectionalShadowDistanceSoftening;\n"
        "uniform int u_DirectionalShadowSubTexelFilter;\n"
        "\n"
        "// Luz Direcional\n"
        "uniform vec3 u_LightDirection;\n"
        "uniform vec3 u_LightColor;\n"
        "uniform vec3 u_CameraPosition;\n"
        "uniform float u_LightIntensity;\n"
        "uniform float u_ShadowIntensity;\n"
        "uniform float u_AmbientLight;\n"
        "uniform int u_AdvancedLightingEnabled;\n"
        "uniform int u_EditorSelectionUnlit;\n"
        "uniform int u_ParticleTextured;\n"
        "uniform float u_IndirectIntensity;\n"
        "uniform float u_ContactShadowStrength;\n"
        "uniform float u_ContactShadowDistance;\n"
        "\n"
        "// Point Lights (ate 4)\n"
        "struct PointLight {\n"
        "    vec3  Position;\n"
        "    vec3  Color;\n"
        "    float Intensity;\n"
        "    float Radius;\n"
        "    float SourceRadius;\n"
        "    float ShadowSoftness;\n"
        "    float ShadowStrength;\n"
        "    int   DistanceSoftening;\n"
        "    int   IndirectContribution;\n"
        "    int   ShadowIndex;\n"
        "};\n"
        "uniform int        u_PointLightCount;\n"
        "uniform PointLight u_PointLights[8];\n"
        "\n"
        "// Spot Lights (ate 4)\n"
        "struct SpotLight {\n"
        "    vec3  Position;\n"
        "    vec3  Direction;    // normalizada, aponta na direcao da luz\n"
        "    vec3  Color;\n"
        "    float Intensity;\n"
        "    float Range;\n"
        "    float InnerCos;    // cos(innerAngle) — area de luz plena\n"
        "    float OuterCos;    // cos(outerAngle) — borda da penumbra\n"
        "    float SourceRadius;\n"
        "    float ShadowSoftness;\n"
        "    float ShadowStrength;\n"
        "    int   DistanceSoftening;\n"
        "    int   IndirectContribution;\n"
        "    int   ShadowIndex;\n"
        "};\n"
        "uniform int       u_SpotLightCount;\n"
        "uniform SpotLight u_SpotLights[8];\n"
        "\n"
        "in vec2 TexCoord;\n"
        "in vec3 Normal;\n"
        "in vec3 WorldPos;\n"
        "in vec4 FragPosLightSpace;\n"
        "in vec4 FragPosShadowCascade[4];\n"
        "in float ViewDepth;\n"
        "\n"
        "out vec4 FragColor;\n"
        "\n"
        "// ---- uniforms USADOS ativamente (iluminacao e fog) ----\n"
        "uniform int   u_GlobalFogEnabled;\n"
        "uniform vec3  u_GlobalFogColor;\n"
        "uniform float u_GlobalFogDensity;\n"
        "uniform float u_GlobalFogStart;\n"
        "uniform float u_GlobalFogEnd;\n"
        "uniform float u_GlobalGamma;\n"
        "\n"
        "uniform int   u_GlobalLightEnabledNew;\n"
        "uniform float u_GlobalIntensity;\n"
        "uniform float u_GlobalShadowStrength;\n"
        "uniform float u_AmbientIntensity;\n"
        "uniform float u_SkyLightIntensity;\n"
        "uniform float u_SceneExposure;\n"
        "uniform vec3  u_AmbientColorNew;\n"
        "\n"
        "// ---- Local Light uniforms (como o objeto recebe luz) ----\n"
        "uniform int   u_LocalLightEnabled;\n"
        "uniform float u_SunLightInfluence;\n"
        "uniform float u_ArtificialLightInfluence;\n"
        "uniform float u_AmbientLightInfluence;\n"
        "uniform int   u_ReceiveShadows;\n"
        "uniform float u_ShadowStrength;\n"
        "uniform float u_ShadowBias;\n"
        "uniform int   u_EnableReflections;\n"
        "uniform float u_ReflectionStrength;\n"
        "uniform int   u_EnableAO;\n"
        "uniform float u_AOStrength;\n"
        "\n"
        "// ---- uniforms LEGADOS (mantidos para compilar; nao usados no calculo). ----\n"
        "// Eles sao setados pela API mas a pipeline de PP usa os BlendedSettings.\n"
        "uniform int   u_GlobalLightEnabled;\n"
        "uniform float u_GlobalBrightness;\n"
        "uniform float u_GlobalContrast;\n"
        "uniform float u_GlobalSaturation;\n"
        "uniform float u_GlobalExposure;\n"
        "uniform vec3 u_GlobalTint;\n"
        "uniform float u_GlobalVignetteStrength;\n"
        "uniform float u_GlobalVignetteRadius;\n"
        "uniform vec2 u_ViewportSize;\n"
        "uniform int       u_MirrorEnabled;\n"
        "uniform sampler2D u_MirrorTexture;\n"
        "uniform float     u_MirrorIntensity;\n"
        "uniform float     u_MirrorBlur;\n"
        "uniform int       u_MirrorMode;\n"
        "uniform int u_ToneMapping;\n"
        "uniform float u_Temperature;\n"
        "uniform float u_Tint;\n"
        "uniform int u_FilmGrainEnabled;\n"
        "uniform float u_FilmGrainStrength;\n"
        "uniform float u_FilmGrainSize;\n"
        "uniform float u_FilmGrainSpeed;\n"
        "uniform int u_ChromaticAberrationEnabled;\n"
        "uniform float u_ChromaticAberrationStrength;\n"
        "uniform float u_BloomStrength;\n"
        "uniform float u_SharpenStrength;\n"
        "\n"
        "// Poisson disk: 16 pontos distribuidos irregularmente no disco unitario.\n"
        "// Elimina o padrao de grade do PCF uniforme, produzindo bordas mais naturais.\n"
        "const vec2 poissonDisk[16] = vec2[](\n"
        "    vec2(-0.94201624, -0.39906216),\n"
        "    vec2( 0.94558609, -0.76890725),\n"
        "    vec2(-0.09418410, -0.92938870),\n"
        "    vec2( 0.34495938,  0.29387760),\n"
        "    vec2(-0.91588581,  0.45771432),\n"
        "    vec2(-0.81544232, -0.87912464),\n"
        "    vec2(-0.38277543,  0.27676845),\n"
        "    vec2( 0.97484398,  0.75648379),\n"
        "    vec2( 0.44323325, -0.97511554),\n"
        "    vec2( 0.53742981, -0.47373420),\n"
        "    vec2(-0.26496911, -0.41893023),\n"
        "    vec2( 0.79197514,  0.19090188),\n"
        "    vec2(-0.24188840,  0.99706507),\n"
        "    vec2(-0.81409955,  0.91437590),\n"
        "    vec2( 0.19984126,  0.78641367),\n"
        "    vec2( 0.14383161, -0.14100790)\n"
        ");\n"
        "\n"
        "float SampleCascadeDepth(int cascade, vec2 uv)\n"
        "{\n"
        "    if (cascade == 0) return texture(u_ShadowCascadeMaps[0], uv).r;\n"
        "    if (cascade == 1) return texture(u_ShadowCascadeMaps[1], uv).r;\n"
        "    if (cascade == 2) return texture(u_ShadowCascadeMaps[2], uv).r;\n"
        "    return texture(u_ShadowCascadeMaps[3], uv).r;\n"
        "}\n"
        "\n"
        "float SampleCascadeVisibility(int cascade, vec2 uv, float referenceDepth)\n"
        "{\n"
        "    if (u_DirectionalShadowSubTexelFilter == 0)\n"
        "        return referenceDepth > SampleCascadeDepth(cascade, uv) ? 1.0 : 0.0;\n"
        "    // Interpola quatro comparações de profundidade, não os valores de\n"
        "    // profundidade. Isso suaviza a borda sem criar profundidades falsas\n"
        "    // entre o caster e o fundo do mapa.\n"
        "    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowCascadeMaps[0], 0));\n"
        "    vec2 texelPosition = uv / texelSize - 0.5;\n"
        "    vec2 baseTexel = floor(texelPosition);\n"
        "    vec2 blend = fract(texelPosition);\n"
        "    vec2 uv00 = (baseTexel + vec2(0.5, 0.5)) * texelSize;\n"
        "    vec2 uv10 = uv00 + vec2(texelSize.x, 0.0);\n"
        "    vec2 uv01 = uv00 + vec2(0.0, texelSize.y);\n"
        "    vec2 uv11 = uv00 + texelSize;\n"
        "    float v00 = referenceDepth > SampleCascadeDepth(cascade, uv00) ? 1.0 : 0.0;\n"
        "    float v10 = referenceDepth > SampleCascadeDepth(cascade, uv10) ? 1.0 : 0.0;\n"
        "    float v01 = referenceDepth > SampleCascadeDepth(cascade, uv01) ? 1.0 : 0.0;\n"
        "    float v11 = referenceDepth > SampleCascadeDepth(cascade, uv11) ? 1.0 : 0.0;\n"
        "    return mix(mix(v00, v10, blend.x), mix(v01, v11, blend.x), blend.y);\n"
        "}\n"
        "\n"
        "float SampleCascadeOpacity(int cascade, vec2 uv)\n"
        "{\n"
        "    return texture(u_ShadowCascadeOpacityMap, vec3(uv, float(cascade))).r;\n"
        "}\n"
        "\n"
        "vec3 SafeNormalize(vec3 value, vec3 fallback)\n"
        "{\n"
        "    float lengthSquared = dot(value, value);\n"
        "    return lengthSquared > 0.00000001 ? value * inversesqrt(lengthSquared) : fallback;\n"
        "}\n"
        "\n"
        "const float PI = 3.14159265359;\n"
        "vec3 FresnelSchlick(float cosTheta, vec3 F0)\n"
        "{\n"
        "    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
        "}\n"
        "float DistributionGGX(vec3 normal, vec3 halfVector, float roughness)\n"
        "{\n"
        "    float alpha = roughness * roughness;\n"
        "    float alpha2 = alpha * alpha;\n"
        "    float NdotH = max(dot(normal, halfVector), 0.0);\n"
        "    float denominator = NdotH * NdotH * (alpha2 - 1.0) + 1.0;\n"
        "    return alpha2 / max(PI * denominator * denominator, 0.000001);\n"
        "}\n"
        "float GeometrySchlickGGX(float NdotV, float roughness)\n"
        "{\n"
        "    float k = (roughness + 1.0);\n"
        "    k = (k * k) * 0.125;\n"
        "    return NdotV / max(NdotV * (1.0 - k) + k, 0.000001);\n"
        "}\n"
        "vec3 EvaluateDirectBRDF(vec3 baseColor, float metallic, float roughness, float specularLevel,\n"
        "                        vec3 normal, vec3 lightDir, vec3 viewDir)\n"
        "{\n"
        "    float NdotL = max(dot(normal, lightDir), 0.0);\n"
        "    float NdotV = max(dot(normal, viewDir), 0.0);\n"
        "    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);\n"
        "    vec3 halfVector = SafeNormalize(lightDir + viewDir, normal);\n"
        "    vec3 F0 = mix(vec3(0.04 * clamp(specularLevel, 0.0, 1.0)), baseColor, metallic);\n"
        "    vec3 F = FresnelSchlick(max(dot(halfVector, viewDir), 0.0), F0);\n"
        "    float D = DistributionGGX(normal, halfVector, roughness);\n"
        "    float G = GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);\n"
        "    vec3 specularBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);\n"
        "    vec3 diffuseBRDF = (1.0 - F) * (1.0 - metallic) * baseColor / PI;\n"
        "    return (diffuseBRDF + specularBRDF) * NdotL;\n"
        "}\n"
        "\n"
        "// Variante para luzes locais: alarga o lobo especular conforme o tamanho\n"
        "// fisico da fonte (SourceRadius) e a distancia, aproximando uma luz de area.\n"
        "// Remove o brilho pontual duro que faz as luzes artificiais parecerem chapadas.\n"
        "vec3 EvaluateDirectBRDFSoft(vec3 baseColor, float metallic, float roughness, float specularLevel,\n"
        "                            vec3 normal, vec3 lightDir, vec3 viewDir,\n"
        "                            float sourceRadius, float distanceToLight)\n"
        "{\n"
        "    float angularRadius = atan(max(sourceRadius, 0.0) / max(distanceToLight, 1e-3));\n"
        "    float softRoughness = clamp(roughness + angularRadius * 0.5, 0.04, 1.0);\n"
        "    return EvaluateDirectBRDF(baseColor, metallic, softRoughness, specularLevel, normal, lightDir, viewDir);\n"
        "}\n"
        "\n"
         "float CalculateShadowCascade(int cascade, vec3 normal, vec3 lightDir)\n"
        "{\n"
        "    float NdotL = dot(normal, lightDir);\n"
        "    if (NdotL <= 0.0)\n"
        "        return 1.0;\n"
        "\n"
        "    // Normal Offset Bias adaptativo por cascata:\n"
        "    // Desloca a coordenada ao longo da normal geometrica para evitar auto-sombra\n"
        "    // e eliminar vazamento de bordas (contact edge bleeding) em quinas de 90 graus.\n"
        "    float normalBiasScale = clamp(1.0 - NdotL, 0.0, 1.0);\n"
        "    float cascadeTexelScale = (cascade == 0) ? 0.04 : ((cascade == 1) ? 0.10 : ((cascade == 2) ? 0.25 : 0.60));\n"
        "    vec3 biasedWorldPos = WorldPos + normal * (cascadeTexelScale * (1.2 + 2.0 * normalBiasScale));\n"
        "    vec4 fragPosLightSpace = u_ShadowCascadeMatrices[cascade] * vec4(biasedWorldPos, 1.0);\n"
        "    if (abs(fragPosLightSpace.w) <= 0.00001)\n"
        "        return 0.0;\n"
        "    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;\n"
        "    projCoords = projCoords * 0.5 + 0.5;\n"
        "\n"
        "    if (projCoords.x <= 0.0 || projCoords.x >= 1.0 ||\n"
        "        projCoords.y <= 0.0 || projCoords.y >= 1.0 ||\n"
        "        projCoords.z <= 0.0 || projCoords.z >= 1.0)\n"
        "        return 0.0;\n"
        "\n"
        "    float currentDepth = projCoords.z;\n"
        "\n"
        "    // Receiver Plane Depth Bias (Gradiente de plano receptor):\n"
        "    // Calcula a inclinacao analitica do plano nos eixos U e V do mapa de sombras.\n"
        "    // Isso elimina 100% os degraus em ziguezague (shadow acne) causados pela inclinacao\n"
        "    // da superficie sem criar descolamento de sombra (peter-panning).\n"
        "    vec3 dx = dFdx(projCoords);\n"
        "    vec3 dy = dFdy(projCoords);\n"
        "    float det = dx.x * dy.y - dx.y * dy.x;\n"
        "    vec2 dz_duv = vec2(0.0);\n"
        "    if (abs(det) > 1e-7)\n"
        "    {\n"
        "        dz_duv = vec2(\n"
        "            dy.y * dx.z - dx.y * dy.z,\n"
        "            dx.x * dy.z - dy.x * dx.z\n"
        "        ) / det;\n"
        "    }\n"
        "    dz_duv = clamp(dz_duv, vec2(-5.0), vec2(5.0));\n"
        "\n"
        "    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowCascadeMaps[0], 0));\n"
        "    float slopeTexelBias = (abs(dz_duv.x) * texelSize.x + abs(dz_duv.y) * texelSize.y) * 0.75;\n"
        "    float baseBias = max(0.0005 * normalBiasScale, 0.0002);\n"
        "    float totalBias = slopeTexelBias + baseBias;\n"
        "\n"
        "    // Profundidade do oclusor no centro do texel. O vão entre oclusor\n"
        "    // e receptor, em unidades de mundo, define a penumbra física: a\n"
        "    // sombra de contato fica concentrada e a projetada ao longe se\n"
        "    // difunde — como numa fonte de luz estendida real.\n"
        "    float blockerDepth = SampleCascadeDepth(cascade, projCoords.xy);\n"
        "    float worldDepthRange = max(u_ShadowCascadeDepthRanges[cascade], 0.001);\n"
        "    float preliminaryGap = max(currentDepth - totalBias - blockerDepth, 0.0) * worldDepthRange;\n"
        "    float contactWeight = clamp(u_ContactShadowStrength, 0.0, 1.0) *\n"
        "        (1.0 - smoothstep(0.0, max(u_ContactShadowDistance, 0.01), preliminaryGap));\n"
        "    totalBias *= mix(1.0, 0.5, contactWeight);\n"
        "    float occluderGap = max(currentDepth - totalBias - blockerDepth, 0.0) * worldDepthRange;\n"
        "    if (u_DirectionalShadowDistanceSoftening == 0)\n"
        "        occluderGap = 0.0;\n"
        "\n"
        "    // Penumbra geométrica de uma fonte distante extensa:\n"
        "    // raio = vão oclusor-receptor * tan(raio angular aparente do Sol/Lua).\n"
        "    float penumbraWorld = occluderGap * tan(max(u_DirectionalSourceAngularRadius, 0.0));\n"
        "    vec2 worldTexelSize = max(u_ShadowCascadeWorldTexelSizes[cascade], vec2(0.00001));\n"
        "    vec2 radiusUV = vec2(penumbraWorld) / worldTexelSize * texelSize;\n"
        "    radiusUV = max(radiusUV, texelSize * 0.5);\n"
        "    radiusUV = min(radiusUV, texelSize * float(textureSize(u_ShadowCascadeMaps[0], 0).x) * 0.125);\n"
        "    float penumbraTexels = max(radiusUV.x / texelSize.x, radiusUV.y / texelSize.y);\n"
        "    int sampleCount = penumbraTexels < 1.5 ? 4 : (penumbraTexels < 6.0 ? 8 : 16);\n"
        "\n"
        "    // Kernel fixo em coordenadas do shadow map: sem ruído por pixel,\n"
        "    // reduz cintilação temporal enquanto a câmera se move.\n"
        "    float shadow = 0.0;\n"
        "    for (int i = 0; i < 16; ++i)\n"
        "    {\n"
        "        if (i >= sampleCount) break;\n"
        "        vec2 offset = poissonDisk[i] * radiusUV;\n"
        "        vec2 sampleUV = projCoords.xy + offset;\n"
        "        float sampleOpacity = clamp(SampleCascadeOpacity(cascade, sampleUV), 0.0, 1.0);\n"
        "        float expectedDepth = currentDepth + dot(dz_duv, offset);\n"
        "        shadow += SampleCascadeVisibility(\n"
        "            cascade, sampleUV, expectedDepth - totalBias) * sampleOpacity;\n"
        "    }\n"
        "    shadow /= float(sampleCount);\n"
        "    return clamp(shadow, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "float CalculateShadow(vec3 normal, vec3 lightDir)\n"
         "{\n"
         "    if (u_UseShadows == 0 || u_DirectionalShadowsEnabled == 0 || u_ShadowCascadeCount <= 0)\n"
         "        return 0.0;\n"
         "\n"
         "    int cascade = 0;\n"
         "    if (u_ShadowCascadeCount > 1 && ViewDepth > u_ShadowCascadeFarPlanes[0]) cascade = 1;\n"
         "    if (u_ShadowCascadeCount > 2 && ViewDepth > u_ShadowCascadeFarPlanes[1]) cascade = 2;\n"
         "    if (u_ShadowCascadeCount > 3 && ViewDepth > u_ShadowCascadeFarPlanes[2]) cascade = 3;\n"
         "\n"
         "    float shadow = CalculateShadowCascade(cascade, normal, lightDir);\n"
         "    if (cascade == u_ShadowCascadeCount - 1)\n"
         "    {\n"
         "        float shadowEnd = u_ShadowCascadeFarPlanes[cascade];\n"
         "        float fadeWidth = max(1.0, shadowEnd * 0.05);\n"
         "        shadow *= 1.0 - smoothstep(shadowEnd - fadeWidth, shadowEnd, ViewDepth);\n"
         "    }\n"
         "    if (cascade + 1 < u_ShadowCascadeCount)\n"
         "    {\n"
         "        float split = u_ShadowCascadeFarPlanes[cascade];\n"
         "        float fadeWidth = max(1.5, split * 0.10);\n"
         "        float blend = smoothstep(split - fadeWidth, split, ViewDepth);\n"
         "        if (blend > 0.0)\n"
         "        {\n"
         "            float nextShadow = CalculateShadowCascade(cascade + 1, normal, lightDir);\n"
         "            shadow = mix(shadow, nextShadow, blend);\n"
         "        }\n"
         "    }\n"
         "    return shadow;\n"
         "}\n"
         "\n"
         "float SamplePointShadowDepth(int index, vec3 direction)\n"
        "{\n"
        "    if (index == 0) return texture(u_PointShadowMap0, direction).r;\n"
        "    if (index == 1) return texture(u_PointShadowMap1, direction).r;\n"
        "    if (index == 2) return texture(u_PointShadowMap2, direction).r;\n"
        "    return texture(u_PointShadowMap3, direction).r;\n"
        "}\n"
        "\n"
        "float SamplePointShadowOpacity(int index, vec3 direction)\n"
        "{\n"
        "    if (index == 0) return texture(u_PointShadowOpacityMap0, direction).r;\n"
        "    if (index == 1) return texture(u_PointShadowOpacityMap1, direction).r;\n"
        "    if (index == 2) return texture(u_PointShadowOpacityMap2, direction).r;\n"
        "    return texture(u_PointShadowOpacityMap3, direction).r;\n"
        "}\n"
        "\n"
        "float CalculatePointShadow(int index, vec3 fragToLight, vec3 normal, vec3 lightDir, float sourceRadius, float softness, int distanceSoftening)\n"
        "{\n"
        // Sombras locais têm seu próprio enable por luz. Não podem depender
        // de u_UseShadows, que controla apenas o caminho direcional/global.
        "    if (index < 0 || index >= 4 || u_PointShadowEnabled[index] == 0)\n"
        "        return 0.0;\n"
        "    if (dot(fragToLight, fragToLight) <= 0.000001)\n"
        "        return 0.0;\n"
        "\n"
        "    float farPlane = max(u_PointShadowFarPlanes[index], 0.001);\n"
        "    float currentDepth = length(fragToLight);\n"
        "    if (currentDepth >= farPlane)\n"
        "        return 0.0;\n"
        "\n"
         "    // Bias em unidades do mundo: mais forte em superfícies inclinadas,\n"
         "    // mas limitado para não empurrar a sombra para longe do caster.\n"
        "    float NdotL = clamp(dot(normal, lightDir), 0.0, 1.0);\n"
        "    float biasScale = clamp(currentDepth / 6.0, 0.7, 1.8);\n"
        "    float bias = mix(0.05, 0.012, NdotL) * biasScale;\n"
        "    // Vão oclusor→receptor em unidades de mundo: mesma física do sol —\n"
        "    // contato nítido, projeção distante difusa.\n"
        "    vec3 centerDir = SafeNormalize(fragToLight, vec3(0.0, 1.0, 0.0));\n"
        "    float blockerDepth = SamplePointShadowDepth(index, centerDir) * farPlane;\n"
        "    float preliminaryGap = max(currentDepth - bias - blockerDepth, 0.0);\n"
        "    float contactWeight = clamp(u_ContactShadowStrength, 0.0, 1.0) *\n"
        "        (1.0 - smoothstep(0.0, max(u_ContactShadowDistance, 0.01), preliminaryGap));\n"
        "    bias *= mix(1.0, 0.5, contactWeight);\n"
        "    float occluderGap = max(currentDepth - bias - blockerDepth, 0.0);\n"
        "    if (distanceSoftening == 0) occluderGap = 0.0;\n"
        "    float physicalSourceRadius = max(sourceRadius, 0.0);\n"
        "    float penumbraWorld = occluderGap * physicalSourceRadius /\n"
        "        max(blockerDepth, physicalSourceRadius);\n"
        "    penumbraWorld *= mix(0.25, 1.0, clamp(softness, 0.0, 1.0));\n"
        "    penumbraWorld = min(penumbraWorld, currentDepth * 0.25);\n"
        "    float minFilterWorld = currentDepth * max(u_PointShadowAngularTexelSize * 0.5, 0.00001);\n"
        "    float filterRadius = max(penumbraWorld, minFilterWorld) / max(currentDepth, 0.001);\n"
        "    const vec3 sampleOffsets[16] = vec3[](\n"
        "        vec3( 1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0),\n"
        "        vec3( 0.0,  1.0,  0.0), vec3( 0.0, -1.0,  0.0),\n"
        "        vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0, -1.0),\n"
        "        vec3( 0.577, 0.577, 0.577), vec3(-0.577, -0.577, -0.577),\n"
        "        vec3( 0.577, 0.577,-0.577), vec3(-0.577, -0.577, 0.577),\n"
        "        vec3( 0.577,-0.577, 0.577), vec3(-0.577, 0.577, -0.577),\n"
        "        vec3( 0.707, 0.707, 0.0), vec3(-0.707, -0.707, 0.0),\n"
        "        vec3( 0.0, 0.707, 0.707), vec3( 0.0, -0.707, -0.707)\n"
        "    );\n"
        "\n"
        "    float shadow = 0.0;\n"
        // Menos leituras de cubemap para bordas nítidas; sombras muito suaves
        // mantêm o kernel completo para preservar a penumbra.
        "    float penumbraTexels = filterRadius / max(u_PointShadowAngularTexelSize, 0.00001);\n"
        "    int sampleCount = penumbraTexels < 1.5 ? 4 : (penumbraTexels < 6.0 ? 8 : 16);\n"
        "    for (int i = 0; i < 16; ++i)\n"
        "    {\n"
        "        if (i >= sampleCount) break;\n"
        "        vec3 sampleDirection = SafeNormalize(\n"
         "            fragToLight + sampleOffsets[i] * filterRadius * max(currentDepth, 1.0),\n"
         "            fragToLight);\n"
        "        float closestDepth = SamplePointShadowDepth(index, sampleDirection) * farPlane;\n"
        "        float shadowOpacity = SamplePointShadowOpacity(index, sampleDirection);\n"
         "        shadowOpacity = clamp(shadowOpacity, 0.0, 1.0);\n"
         "        shadow += (closestDepth < farPlane && currentDepth - bias > closestDepth)\n"
         "            ? shadowOpacity : 0.0;\n"
        "    }\n"
        "    return shadow / float(sampleCount);\n"
        "}\n"
        "\n"
        "void main()\n"
        "{\n"
        "    vec2 uv = TexCoord;\n"
        "    bool isOutsideFixed = false;\n"
        "    if (u_UVMode == 1)\n"
        "    {\n"
        "        vec3 absNorm = abs(SafeNormalize(Normal, vec3(0.0, 1.0, 0.0)));\n"
        "        vec2 faceScale = vec2(1.0);\n"
        "        if (absNorm.z >= absNorm.x && absNorm.z >= absNorm.y)\n"
        "            faceScale = vec2(abs(u_ObjectScale.x), abs(u_ObjectScale.y));\n"
        "        else if (absNorm.x >= absNorm.y && absNorm.x >= absNorm.z)\n"
        "            faceScale = vec2(abs(u_ObjectScale.z), abs(u_ObjectScale.y));\n"
        "        else\n"
        "            faceScale = vec2(abs(u_ObjectScale.x), abs(u_ObjectScale.z));\n"
        "        faceScale = max(faceScale, vec2(0.001));\n"
        "\n"
        "        vec2 grid = max(u_UVAtlasGrid, vec2(1.0));\n"
        "        vec2 cellSize = vec2(1.0) / grid;\n"
        "        vec2 cellIdx = clamp(floor(TexCoord * grid), vec2(0.0), grid - vec2(1.0));\n"
        "        vec2 cellMin = cellIdx * cellSize;\n"
        "        vec2 cellMax = (cellIdx + vec2(1.0)) * cellSize;\n"
        "        vec2 cellCenter = (cellMin + cellMax) * 0.5;\n"
        "        int align = int(u_UVExtraParams.x + 0.5);\n"
        "        vec2 anchor = cellCenter;\n"
        "        if (align == 1) anchor = cellMin;\n"
        "        else if (align == 2) anchor = vec2(cellMax.x, cellMin.y);\n"
        "        else if (align == 3) anchor = vec2(cellMin.x, cellMax.y);\n"
        "        else if (align == 4) anchor = cellMax;\n"
        "        vec2 scaledOffset = (TexCoord - anchor) * faceScale;\n"
        "        uv = anchor + scaledOffset;\n"
        "        if (uv.x < cellMin.x || uv.x > cellMax.x || uv.y < cellMin.y || uv.y > cellMax.y)\n"
        "        {\n"
        "            isOutsideFixed = true;\n"
        "            uv = cellMin + vec2(0.002);\n"
        "        }\n"
        "    }\n"
        "    else if (u_UVMode == 4)\n"
        "    {\n"
        "        // AutoTileFace: Repete a textura em cada face de acordo com o tamanho do objeto (sem esticar)\n"
        "        vec3 absNorm = abs(SafeNormalize(Normal, vec3(0.0, 1.0, 0.0)));\n"
        "        vec2 faceScale = vec2(1.0);\n"
        "        if (absNorm.z >= absNorm.x && absNorm.z >= absNorm.y)\n"
        "            faceScale = vec2(abs(u_ObjectScale.x), abs(u_ObjectScale.y));\n"
        "        else if (absNorm.x >= absNorm.y && absNorm.x >= absNorm.z)\n"
        "            faceScale = vec2(abs(u_ObjectScale.z), abs(u_ObjectScale.y));\n"
        "        else\n"
        "            faceScale = vec2(abs(u_ObjectScale.x), abs(u_ObjectScale.z));\n"
        "        faceScale = max(faceScale, vec2(0.001));\n"
        "        float repeatFactor = max(u_UVExtraParams.y, 0.01);\n"
        "        vec2 grid = max(u_UVAtlasGrid, vec2(1.0));\n"
        "        vec2 cellSize = vec2(1.0) / grid;\n"
        "        vec2 cellIdx = clamp(floor(TexCoord * grid), vec2(0.0), grid - vec2(1.0));\n"
        "        vec2 cellMin = cellIdx * cellSize;\n"
        "        vec2 localUV = fract((TexCoord - cellMin) / cellSize * faceScale * repeatFactor);\n"
        "        uv = cellMin + localUV * cellSize;\n"
        "    }\n"
        "    else if (u_UVMode == 5)\n"
        "    {\n"
        "        // NineSlice: Preserva cantos e bordas com proporcao original, esticando apenas o miolo\n"
        "        vec3 absNorm = abs(SafeNormalize(Normal, vec3(0.0, 1.0, 0.0)));\n"
        "        vec2 faceScale = vec2(1.0);\n"
        "        if (absNorm.z >= absNorm.x && absNorm.z >= absNorm.y)\n"
        "            faceScale = vec2(abs(u_ObjectScale.x), abs(u_ObjectScale.y));\n"
        "        else if (absNorm.x >= absNorm.y && absNorm.x >= absNorm.z)\n"
        "            faceScale = vec2(abs(u_ObjectScale.z), abs(u_ObjectScale.y));\n"
        "        else\n"
        "            faceScale = vec2(abs(u_ObjectScale.x), abs(u_ObjectScale.z));\n"
        "        faceScale = max(faceScale, vec2(0.001));\n"
        "        float b = clamp(u_UVExtraParams.z, 0.01, 0.45);\n"
        "        vec2 grid = max(u_UVAtlasGrid, vec2(1.0));\n"
        "        vec2 cellSize = vec2(1.0) / grid;\n"
        "        vec2 cellIdx = clamp(floor(TexCoord * grid), vec2(0.0), grid - vec2(1.0));\n"
        "        vec2 cellMin = cellIdx * cellSize;\n"
        "        vec2 t = clamp((TexCoord - cellMin) / cellSize, vec2(0.0), vec2(1.0));\n"
        "        vec2 borderCoord = b / faceScale;\n"
        "        vec2 mappedUV;\n"
        "        // Eixo X\n"
        "        if (t.x < borderCoord.x)\n"
        "            mappedUV.x = (t.x / max(borderCoord.x, 0.0001)) * b;\n"
        "        else if (t.x > 1.0 - borderCoord.x)\n"
        "            mappedUV.x = (1.0 - b) + ((t.x - (1.0 - borderCoord.x)) / max(borderCoord.x, 0.0001)) * b;\n"
        "        else\n"
        "            mappedUV.x = b + ((t.x - borderCoord.x) / max(1.0 - 2.0 * borderCoord.x, 0.0001)) * (1.0 - 2.0 * b);\n"
        "        // Eixo Y\n"
        "        if (t.y < borderCoord.y)\n"
        "            mappedUV.y = (t.y / max(borderCoord.y, 0.0001)) * b;\n"
        "        else if (t.y > 1.0 - borderCoord.y)\n"
        "            mappedUV.y = (1.0 - b) + ((t.y - (1.0 - borderCoord.y)) / max(borderCoord.y, 0.0001)) * b;\n"
        "        else\n"
        "            mappedUV.y = b + ((t.y - borderCoord.y) / max(1.0 - 2.0 * borderCoord.y, 0.0001)) * (1.0 - 2.0 * b);\n"
        "        uv = cellMin + clamp(mappedUV, vec2(0.0), vec2(1.0)) * cellSize;\n"
        "    }\n"
        "    else if (u_UVMode == 3)\n"
        "    {\n"
        "        vec3 absNorm = abs(SafeNormalize(Normal, vec3(0.0, 1.0, 0.0)));\n"
        "        float uvScale = max(u_UVWorldScale, 0.0001);\n"
        "        if (absNorm.y >= absNorm.x && absNorm.y >= absNorm.z)\n"
        "            uv = WorldPos.xz * uvScale;\n"
        "        else if (absNorm.x >= absNorm.y && absNorm.x >= absNorm.z)\n"
        "            uv = WorldPos.zy * uvScale;\n"
        "        else\n"
        "            uv = WorldPos.xy * uvScale;\n"
        "    }\n"
        "\n"
        "    vec4 texColor = texture(u_Diffuse, uv);\n"
        "    if (isOutsideFixed)\n"
        "        if (texColor.a < 0.05) texColor = u_CanvasBgColor;\n"
        "\n"
        "    float alpha = texColor.a * clamp(u_Opacity, 0.0, 1.0);\n"
        "    if (alpha <= 0.001) discard;\n"
        "    if (u_EditorSelectionUnlit != 0)\n"
        "    {\n"
        "        vec3 unlitColor = u_ParticleTextured != 0 ? texColor.rgb * u_Color : u_Color;\n"
        "        FragColor = vec4(unlitColor, alpha);\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    vec3 baseColor = texColor.rgb * u_Color;\n"
        "    vec3 emissionColor = vec3(1.0);\n"
        "    if (u_HasEmissionMap != 0)\n"
        "        emissionColor = texture(u_EmissionMap, uv).rgb;\n"
        "    vec3 emission = baseColor * emissionColor * max(u_EmissionStrength, 0.0);\n"
        "    vec3 normal = SafeNormalize(Normal, vec3(0.0, 1.0, 0.0));\n"
        "    if (u_HasNormalMap != 0)\n"
        "    {\n"
        "        vec3 dp1 = dFdx(WorldPos);\n"
        "        vec3 dp2 = dFdy(WorldPos);\n"
        "        vec2 duv1 = dFdx(uv);\n"
        "        vec2 duv2 = dFdy(uv);\n"
        "        float determinant = duv1.x * duv2.y - duv1.y * duv2.x;\n"
        "        if (abs(determinant) > 0.000001)\n"
        "        {\n"
        "            float invDet = 1.0 / determinant;\n"
        "            vec3 tangentRaw = (dp1 * duv2.y - dp2 * duv1.y) * invDet;\n"
        "            vec3 bitangentRaw = (dp2 * duv1.x - dp1 * duv2.x) * invDet;\n"
        "            vec3 tangent = SafeNormalize(tangentRaw - normal * dot(normal, tangentRaw),\n"
        "                abs(normal.y) < 0.9 ? SafeNormalize(cross(vec3(0.0, 1.0, 0.0), normal), vec3(1.0, 0.0, 0.0))\n"
        "                                     : SafeNormalize(cross(vec3(1.0, 0.0, 0.0), normal), vec3(0.0, 0.0, 1.0)));\n"
        "            float tangentHandedness = dot(cross(normal, tangent), bitangentRaw) < 0.0 ? -1.0 : 1.0;\n"
        "            vec3 bitangent = SafeNormalize(cross(normal, tangent), vec3(0.0, 0.0, 1.0)) * tangentHandedness;\n"
        "            vec3 mappedNormal = texture(u_NormalMap, uv).xyz * 2.0 - 1.0;\n"
        "            mappedNormal.xy *= clamp(u_NormalStrength, 0.0, 2.0);\n"
        "            normal = SafeNormalize(mat3(tangent, bitangent, normal) * mappedNormal, normal);\n"
        "        }\n"
        "    }\n"
        "    vec3 lightDir = SafeNormalize(-u_LightDirection, vec3(0.0, 1.0, 0.0));\n"
        "    vec3 viewDir  = SafeNormalize(u_CameraPosition - WorldPos, vec3(0.0, 0.0, 1.0));\n"
        "    float metallic = clamp(u_Metallic, 0.0, 1.0);\n"
        "    float roughness = clamp(u_Roughness, 0.04, 1.0);\n"
        "    float shadow = CalculateShadow(normal, lightDir);\n"
        "    \n"
        "    // Aplicar Local Light settings se habilitado, senão usa valores globais\n"
        "    float receiverShadowStrength = u_ReceiveShadows != 0 ? 1.0 : 0.0;\n"
        "    if (u_LocalLightEnabled != 0)\n"
        "        receiverShadowStrength *= clamp(u_ShadowStrength, 0.0, 1.0);\n"
        "    float shadowStrength = u_GlobalShadowStrength * receiverShadowStrength;\n"
        "    float shadowBias = 0.005;\n"
        "    if (u_LocalLightEnabled != 0)\n"
        "    {\n"
        "        shadowBias = u_ShadowBias;\n"
        "    }\n"
        "    \n"
        "    float shadowMask = shadow * clamp(shadowStrength, 0.0, 1.0);\n"
        "    float globalLightEnabled = u_GlobalLightEnabledNew != 0 ? 1.0 : 0.0;\n"
        "    // A luz do ceu e ambiente escala com a presenca de luz celeste (sol/lua).\n"
        "    // Na ausencia de sol e luzes locais, a cena fica em escuridao total realista (sem brilho radioativo no escuro).\n"
        "    float celestialFactor = clamp(u_LightIntensity, 0.0, 1.0);\n"
        "    vec3 skyColor = u_AmbientColorNew * u_AmbientIntensity * celestialFactor;\n"
        "    vec3 ambientScale = vec3(u_GlobalIntensity) * skyColor;\n"
        "    \n"
        "    // Aplicar influência de luz local\n"
        "    float sunInfluence = u_SunLightInfluence;\n"
        "    float artificialInfluence = u_ArtificialLightInfluence;\n"
        "    float ambientInfluence = u_AmbientLightInfluence;\n"
        "    if (u_LocalLightEnabled == 0)\n"
        "    {\n"
        "        sunInfluence = 1.0;\n"
        "        artificialInfluence = 1.0;\n"
        "        ambientInfluence = 1.0;\n"
        "    }\n"
        "    \n"
        "    vec3 F0 = mix(vec3(0.04 * clamp(u_Specular, 0.0, 1.0)), baseColor, metallic);\n"
        "    vec3 direct = EvaluateDirectBRDF(baseColor, metallic, roughness, u_Specular,\n"
        "        normal, lightDir, viewDir) * u_LightColor * u_LightIntensity * globalLightEnabled * sunInfluence;\n"
        "    direct *= (1.0 - shadowMask);\n"
        "    // Atenua a luz do ceu/ambiente em areas sob telhado ou oclusao (evita o quarto cinza lavado)\n"
        "    // Atenua suavemente a luz ambiente sob sombra direta (preserva luz difusa do ceu)\n"
        "    float shadowAmbientOcclusion = mix(1.0, 0.85, shadowMask);\n"
        "    vec3 ambient = baseColor * (1.0 - metallic) * ambientScale * u_AmbientLight * globalLightEnabled * ambientInfluence * shadowAmbientOcclusion / PI;\n"
        "    vec3 localDirectContrib = vec3(0.0);\n"
        "    vec3 localIndirectRadiance = vec3(0.0);\n"
        "    for (int i = 0; i < u_PointLightCount; ++i)\n"
        "    {\n"
        "        vec3  toLight  = u_PointLights[i].Position - WorldPos;\n"
        "        float dist     = length(toLight);\n"
        "        float r        = max(u_PointLights[i].Radius, 0.001);\n"
        "        float normDist = clamp(dist / r, 0.0, 1.0);\n"
        "        float window   = clamp(1.0 - normDist * normDist * normDist * normDist, 0.0, 1.0);\n"
        "        // A queda 1/d² original apagava a luz muito antes da borda do\n"
        "        // range em alcances longos. O penalty é atenuado pelo quadrado\n"
        "        // do range: Range <= 10 fica idêntico ao comportamento anterior.\n"
        "        float distPenalty = 0.18 * min(1.0, 100.0 / (r * r));\n"
        "        float atten    = (window * window) / (1.0 + (dist * dist) * distPenalty) * 2.8;\n"
        "        vec3  toLightDir = SafeNormalize(toLight, vec3(0.0, 1.0, 0.0));\n"
        "        float pointShadow = CalculatePointShadow(\n"
        "            u_PointLights[i].ShadowIndex, -toLight, normal, toLightDir,\n"
        "            u_PointLights[i].SourceRadius, u_PointLights[i].ShadowSoftness\n"
        "            , u_PointLights[i].DistanceSoftening\n"
        "        );\n"
         "        float localVisibility = 1.0 - pointShadow * clamp(u_PointLights[i].ShadowStrength, 0.0, 1.0) * receiverShadowStrength;\n"
        "        localDirectContrib += EvaluateDirectBRDFSoft(baseColor, metallic, roughness, u_Specular,\n"
        "            normal, toLightDir, viewDir, u_PointLights[i].SourceRadius, dist) * u_PointLights[i].Color *\n"
        "            u_PointLights[i].Intensity * atten * localVisibility * artificialInfluence;\n"
        "        if (u_PointLights[i].IndirectContribution != 0)\n"
        "            localIndirectRadiance += u_PointLights[i].Color * u_PointLights[i].Intensity * atten * 0.015 * artificialInfluence;\n"
        "    }\n"
        "\n"
        "    vec3 spotContrib = vec3(0.0);\n"
        "    for (int i = 0; i < u_SpotLightCount; ++i)\n"
        "    {\n"
        "        vec3  toLight    = u_SpotLights[i].Position - WorldPos;\n"
        "        float dist       = length(toLight);\n"
        "        vec3  toLightDir = SafeNormalize(toLight, vec3(0.0, 1.0, 0.0));\n"
        "        float r          = max(u_SpotLights[i].Range, 0.001);\n"
        "        float normDist   = clamp(dist / r, 0.0, 1.0);\n"
        "        float window     = clamp(1.0 - normDist * normDist * normDist * normDist, 0.0, 1.0);\n"
        "        // Mesma compensação de alcance das point lights (ver acima).\n"
        "        float distPenalty = 0.18 * min(1.0, 100.0 / (r * r));\n"
        "        float atten      = (window * window) / (1.0 + (dist * dist) * distPenalty) * 2.8;\n"
        "        float cosTheta   = dot(SafeNormalize(-u_SpotLights[i].Direction, vec3(0.0, -1.0, 0.0)), toLightDir);\n"
        "        float epsilon    = u_SpotLights[i].InnerCos - u_SpotLights[i].OuterCos;\n"
        "        // Penumbra suave: a transicao inner->outer do cone deixa de ter vinco.\n"
        "        float coneLinear = clamp((cosTheta - u_SpotLights[i].OuterCos) / max(epsilon, 0.0001), 0.0, 1.0);\n"
        "        float coneFactor = coneLinear * coneLinear * (3.0 - 2.0 * coneLinear);\n"
        "        float spotShadow = CalculatePointShadow(\n"
        "            u_SpotLights[i].ShadowIndex, -toLight, normal, toLightDir,\n"
        "            u_SpotLights[i].SourceRadius, u_SpotLights[i].ShadowSoftness\n"
        "            , u_SpotLights[i].DistanceSoftening\n"
        "        );\n"
         "        float localVisibility = clamp(1.0 - spotShadow * clamp(u_SpotLights[i].ShadowStrength, 0.0, 1.0) * receiverShadowStrength, 0.0, 1.0);\n"
        "        localDirectContrib += EvaluateDirectBRDFSoft(baseColor, metallic, roughness, u_Specular,\n"
        "            normal, toLightDir, viewDir, u_SpotLights[i].SourceRadius, dist) * u_SpotLights[i].Color *\n"
        "            u_SpotLights[i].Intensity * atten * coneFactor * localVisibility * artificialInfluence;\n"
        "        if (u_SpotLights[i].IndirectContribution != 0)\n"
        "            localIndirectRadiance += u_SpotLights[i].Color * u_SpotLights[i].Intensity * atten * coneFactor * 0.015 * artificialInfluence;\n"
        "    }\n"
        "\n"
        "    float hemisphere = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);\n"
        "    // Irradiancia hemisferica proporcional a cor do ceu e ao sol (sem piso constante indevido)\n"
        "    vec3 skyIrradiance = skyColor * mix(0.12, 1.0, hemisphere);\n"
        "    // OpenGL 3.3 sem probe/buffer de cena: irradiância hemisférica é uma\n"
        "    // aproximação ambiental barata, não GI dinâmica multi-bounce.\n"
        "    vec3 diffuseIndirect = baseColor * (1.0 - metallic) / PI;\n"
        "    vec3 indirect = diffuseIndirect * (skyIrradiance * u_SkyLightIntensity * globalLightEnabled * shadowAmbientOcclusion +\n"
        "        localIndirectRadiance) * u_IndirectIntensity;\n"
        "    vec3 environmentSpecular = FresnelSchlick(max(dot(normal, viewDir), 0.0), F0) *\n"
        "        skyIrradiance * roughness * u_SkyLightIntensity * u_IndirectIntensity * globalLightEnabled * shadowAmbientOcclusion;\n"
        "    vec3 finalColor = direct + ambient + localDirectContrib + indirect +\n"
        "        environmentSpecular + emission;\n"
        "\n"
        "    // ---- Mirror reflection (suporta UltraPerformance ate Ultra) ----\n"
        "    if (u_MirrorMode == 1 && u_MirrorEnabled != 0)\n"
        "    {\n"
        "        vec2 screenUV = gl_FragCoord.xy / max(u_ViewportSize, vec2(1.0, 1.0));\n"
        "        vec2 mirrorUV = vec2(1.0 - screenUV.x, screenUV.y);\n"
        "        vec3 mirrorSample = vec3(0.0);\n"
        "        if (u_MirrorBlur > 0.002)\n"
        "        {\n"
        "            float blurAmt = u_MirrorBlur * 0.02;\n"
        "            for (int p = 0; p < 16; ++p)\n"
        "            {\n"
        "                vec2 uvOffset = poissonDisk[p] * blurAmt;\n"
        "                mirrorSample += texture(u_MirrorTexture, clamp(mirrorUV + uvOffset, 0.001, 0.999)).rgb;\n"
        "            }\n"
        "            mirrorSample *= (1.0 / 16.0);\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "            mirrorSample = texture(u_MirrorTexture, clamp(mirrorUV, 0.001, 0.999)).rgb;\n"
        "        }\n"
        "        vec3 silverTint = mix(vec3(0.92, 0.95, 0.98), baseColor, 0.35);\n"
        "        vec3 mirrorTint = mirrorSample * silverTint;\n"
        "        float mirrorFresnel = clamp(0.35 + 0.65 * pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0), 0.0, 1.0);\n"
        "        float mirrorAlpha = clamp(u_MirrorIntensity, 0.0, 1.0) * mirrorFresnel;\n"
        "        finalColor = mix(finalColor, mirrorTint, mirrorAlpha);\n"
        "    }\n"
        "    else if (u_MirrorMode == 2)\n"
        "    {\n"
        "        // Modo Ultra Performance / sem FBO: reflexo cromado PBR levissimo apenas para espelho\n"
        "        vec3 reflDir = reflect(-viewDir, normal);\n"
        "        float skyHemi = clamp(reflDir.y * 0.5 + 0.5, 0.0, 1.0);\n"
        "        vec3 chrome = mix(vec3(0.20, 0.25, 0.30), u_AmbientColorNew * 2.5 + vec3(0.85, 0.90, 0.95), skyHemi);\n"
        "        finalColor = mix(finalColor, chrome * baseColor, 0.85);\n"
        "    }\n"
        "\n"
        "    // Scene exposure pre-PP (pipeline aplica exposure completa no Color Grading).\n"
        "    finalColor *= exp2(u_SceneExposure);\n"
        "\n"
        "    // Fog: aplicado no shader principal porque depende de WorldPos.\n"
        "    if (u_GlobalFogEnabled != 0)\n"
        "    {\n"
        "        float distanceToCamera = length(u_CameraPosition - WorldPos);\n"
        "        float rangeFactor = smoothstep(u_GlobalFogStart, max(u_GlobalFogEnd, u_GlobalFogStart + 0.01), distanceToCamera);\n"
        "        float densityFactor = 1.0 - exp(-max(distanceToCamera - u_GlobalFogStart, 0.0) * u_GlobalFogDensity);\n"
        "        float fogFactor = clamp(rangeFactor * densityFactor, 0.0, 1.0);\n"
        "        finalColor = mix(finalColor, u_GlobalFogColor, fogFactor);\n"
        "    }\n"
        "\n"
        "    // Gamma linear -> monitor? NAO. A pipeline de PP aplica gamma correto\n"
        "    // no tone map final. Saida e LINEAR HDR para o FBO RGBA16F.\n"
        "\n"
        "    FragColor = vec4(max(finalColor, vec3(0.0)), alpha);\n"
        "}\n";

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);
    const bool vertexCompiled = CheckShaderStage(vertexShader, "vertex principal");

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);
    const bool fragmentCompiled = CheckShaderStage(fragmentShader, "fragment principal");

    m_ProgramID = glCreateProgram();

    glAttachShader(m_ProgramID, vertexShader);
    glAttachShader(m_ProgramID, fragmentShader);
    glLinkProgram(m_ProgramID);
    const bool programLinked = CheckShaderProgram(m_ProgramID, "programa principal");

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Nunca deixe o viewport inteiro vazio por causa de uma extensão GLSL
    // incompatível. Alguns drivers recusam construções mais novas usadas
    // somente nas sombras locais (por exemplo, indexação dinâmica de arrays
    // de samplers). Nesse caso, cria um pipeline mínimo de geometria para o
    // editor: o mesh volta a aparecer e o shader completo continua sendo
    // tentado normalmente em drivers compatíveis.
    bool shaderReady = vertexCompiled && fragmentCompiled && programLinked;
    if (!shaderReady)
    {
        const char* fallbackVertexSource =
            "#version 330 core\n"
            "layout(location = 0) in vec3 aPos;\n"
            "uniform mat4 u_Model;\n"
            "uniform mat4 u_View;\n"
            "uniform mat4 u_Projection;\n"
            "void main()\n"
            "{\n"
            "    gl_Position = u_Projection * u_View * u_Model * vec4(aPos, 1.0);\n"
            "}\n";
        const char* fallbackFragmentSource =
            "#version 330 core\n"
            "uniform vec3 u_Color;\n"
            "uniform float u_Opacity;\n"
            "out vec4 FragColor;\n"
            "void main()\n"
            "{\n"
            "    FragColor = vec4(max(u_Color, vec3(0.08)), clamp(u_Opacity, 0.0, 1.0));\n"
            "}\n";

        unsigned int fallbackVertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(fallbackVertex, 1, &fallbackVertexSource, nullptr);
        glCompileShader(fallbackVertex);
        const bool fallbackVertexCompiled =
            CheckShaderStage(fallbackVertex, "vertex fallback");

        unsigned int fallbackFragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fallbackFragment, 1, &fallbackFragmentSource, nullptr);
        glCompileShader(fallbackFragment);
        const bool fallbackFragmentCompiled =
            CheckShaderStage(fallbackFragment, "fragment fallback");

        unsigned int fallbackProgram = glCreateProgram();
        glAttachShader(fallbackProgram, fallbackVertex);
        glAttachShader(fallbackProgram, fallbackFragment);
        glLinkProgram(fallbackProgram);
        const bool fallbackLinked =
            CheckShaderProgram(fallbackProgram, "programa fallback");

        glDeleteShader(fallbackVertex);
        glDeleteShader(fallbackFragment);

        if (fallbackVertexCompiled && fallbackFragmentCompiled && fallbackLinked)
        {
            glDeleteProgram(m_ProgramID);
            m_ProgramID = fallbackProgram;
            shaderReady = true;
            std::cerr << "[Shader] Usando pipeline fallback para manter os meshes visiveis." << std::endl;
        }
        else
        {
            glDeleteProgram(fallbackProgram);
        }
    }

    // Cache uniform locations and set sampler units
    glUseProgram(m_ProgramID);
    m_LocModel            = glGetUniformLocation(m_ProgramID, "u_Model");
    m_LocSkinningEnabled  = glGetUniformLocation(m_ProgramID, "u_SkinningEnabled");
    m_LocSkinMatrices     = glGetUniformLocation(m_ProgramID, "u_SkinMatrices[0]");
    m_LocView             = glGetUniformLocation(m_ProgramID, "u_View");
    m_LocProjection       = glGetUniformLocation(m_ProgramID, "u_Projection");
    m_LocCameraPosition   = glGetUniformLocation(m_ProgramID, "u_CameraPosition");
    m_LocLightSpaceMatrix = glGetUniformLocation(m_ProgramID, "u_LightSpaceMatrix");
    m_LocColor            = glGetUniformLocation(m_ProgramID, "u_Color");
    m_LocOpacity          = glGetUniformLocation(m_ProgramID, "u_Opacity");
    m_LocDiffuse          = glGetUniformLocation(m_ProgramID, "u_Diffuse");
    m_LocShadowMap        = glGetUniformLocation(m_ProgramID, "u_ShadowMap");
    m_LocShadowOpacityMap = glGetUniformLocation(m_ProgramID, "u_ShadowOpacityMap");
    m_LocShadowOpacityArray = glGetUniformLocation(m_ProgramID, "u_ShadowCascadeOpacityMap");
    m_LocShadowCascadeFarPlanes = glGetUniformLocation(
        m_ProgramID, "u_ShadowCascadeFarPlanes[0]");
    m_LocShadowCascadeDepthRanges = glGetUniformLocation(
        m_ProgramID, "u_ShadowCascadeDepthRanges[0]");
    m_LocShadowCascadeWorldTexelSizes = glGetUniformLocation(
        m_ProgramID, "u_ShadowCascadeWorldTexelSizes[0]");
    m_LocDirectionalSourceAngularRadius = glGetUniformLocation(
        m_ProgramID, "u_DirectionalSourceAngularRadius");
    m_LocPointShadowAngularTexelSize = glGetUniformLocation(
        m_ProgramID, "u_PointShadowAngularTexelSize");
    m_LocShadowCascadeCount = glGetUniformLocation(
        m_ProgramID, "u_ShadowCascadeCount");
    for (int i = 0; i < MAX_SHADOW_CASCADES; ++i)
    {
        m_LocShadowCascadeMatrices[i] = glGetUniformLocation(
            m_ProgramID,
            (std::string("u_ShadowCascadeMatrices[") + std::to_string(i) + "]").c_str());
    }
    m_LocUseShadows       = glGetUniformLocation(m_ProgramID, "u_UseShadows");
    m_LocDirectionalShadowsEnabled = glGetUniformLocation(
        m_ProgramID, "u_DirectionalShadowsEnabled");
    m_LocDirectionalShadowDistanceSoftening = glGetUniformLocation(
        m_ProgramID, "u_DirectionalShadowDistanceSoftening");
    m_LocDirectionalShadowSubTexelFilter = glGetUniformLocation(
        m_ProgramID, "u_DirectionalShadowSubTexelFilter");
    m_LocMetallic         = glGetUniformLocation(m_ProgramID, "u_Metallic");
    m_LocRoughness        = glGetUniformLocation(m_ProgramID, "u_Roughness");
    m_LocSpecular         = glGetUniformLocation(m_ProgramID, "u_Specular");
    m_LocEmissionStrength = glGetUniformLocation(m_ProgramID, "u_EmissionStrength");

    // Local Light uniforms
    m_LocLocalLightEnabled = glGetUniformLocation(m_ProgramID, "u_LocalLightEnabled");
    m_LocSunLightInfluence = glGetUniformLocation(m_ProgramID, "u_SunLightInfluence");
    m_LocArtificialLightInfluence = glGetUniformLocation(m_ProgramID, "u_ArtificialLightInfluence");
    m_LocAmbientLightInfluence = glGetUniformLocation(m_ProgramID, "u_AmbientLightInfluence");
    m_LocReceiveShadows = glGetUniformLocation(m_ProgramID, "u_ReceiveShadows");
    m_LocShadowStrength = glGetUniformLocation(m_ProgramID, "u_ShadowStrength");
    m_LocShadowBias = glGetUniformLocation(m_ProgramID, "u_ShadowBias");
    m_LocEnableReflections = glGetUniformLocation(m_ProgramID, "u_EnableReflections");
    m_LocReflectionStrength = glGetUniformLocation(m_ProgramID, "u_ReflectionStrength");
    m_LocEnableAO = glGetUniformLocation(m_ProgramID, "u_EnableAO");
    m_LocAOStrength = glGetUniformLocation(m_ProgramID, "u_AOStrength");

    m_LocGlobalEnabled    = glGetUniformLocation(m_ProgramID, "u_GlobalLightEnabled");
    m_LocGlobalBrightness = glGetUniformLocation(m_ProgramID, "u_GlobalBrightness");
    m_LocGlobalContrast   = glGetUniformLocation(m_ProgramID, "u_GlobalContrast");
    m_LocGlobalSaturation = glGetUniformLocation(m_ProgramID, "u_GlobalSaturation");
    m_LocGlobalExposure   = glGetUniformLocation(m_ProgramID, "u_GlobalExposure");
    m_LocGlobalTint       = glGetUniformLocation(m_ProgramID, "u_GlobalTint");
    m_LocGlobalGamma      = glGetUniformLocation(m_ProgramID, "u_GlobalGamma");
    m_LocGlobalFogEnabled = glGetUniformLocation(m_ProgramID, "u_GlobalFogEnabled");
    m_LocGlobalFogColor   = glGetUniformLocation(m_ProgramID, "u_GlobalFogColor");
    m_LocGlobalFogDensity = glGetUniformLocation(m_ProgramID, "u_GlobalFogDensity");
    m_LocGlobalFogStart   = glGetUniformLocation(m_ProgramID, "u_GlobalFogStart");
    m_LocGlobalFogEnd     = glGetUniformLocation(m_ProgramID, "u_GlobalFogEnd");
    m_LocGlobalVignetteStrength = glGetUniformLocation(m_ProgramID, "u_GlobalVignetteStrength");
    m_LocGlobalVignetteRadius = glGetUniformLocation(m_ProgramID, "u_GlobalVignetteRadius");
    m_LocViewportSize     = glGetUniformLocation(m_ProgramID, "u_ViewportSize");

    for (int i = 0; i < MAX_LOCAL_SHADOWS; ++i)
    {
        const std::string index = std::to_string(i);
        m_LocPointShadowMaps[i] = glGetUniformLocation(
            m_ProgramID, ("u_PointShadowMap" + index).c_str());
        m_LocPointShadowOpacityMaps[i] = glGetUniformLocation(
            m_ProgramID, ("u_PointShadowOpacityMap" + index).c_str());
        m_LocPointShadowEnabled[i] = glGetUniformLocation(
            m_ProgramID, ("u_PointShadowEnabled[" + index + "]").c_str());
        m_LocPointShadowFarPlanes[i] = glGetUniformLocation(
            m_ProgramID, ("u_PointShadowFarPlanes[" + index + "]").c_str());
    }

    if (m_LocDiffuse    >= 0) glUniform1i(m_LocDiffuse,    0);
    if (m_LocShadowMap  >= 0) glUniform1i(m_LocShadowMap,  1);
    if (m_LocOpacity >= 0) glUniform1f(m_LocOpacity, 1.0f);
    if (m_LocShadowOpacityMap >= 0) glUniform1i(m_LocShadowOpacityMap, 14);
    if (m_LocShadowOpacityArray >= 0) glUniform1i(m_LocShadowOpacityArray, 14);
    for (int i = 0; i < MAX_SHADOW_CASCADES; ++i)
    {
        const int location = glGetUniformLocation(
            m_ProgramID,
            (std::string("u_ShadowCascadeMaps[") + std::to_string(i) + "]").c_str());
        if (location >= 0)
            glUniform1i(location, 1 + i);
    }
    if (m_LocShadowCascadeFarPlanes >= 0)
    {
        const float defaultFarPlanes[MAX_SHADOW_CASCADES] = { 12.0f, 35.0f, 90.0f, 220.0f };
        glUniform1fv(m_LocShadowCascadeFarPlanes, MAX_SHADOW_CASCADES, defaultFarPlanes);
    }
    if (m_LocShadowCascadeDepthRanges >= 0)
    {
        const float defaultDepthRanges[MAX_SHADOW_CASCADES] = { 40.0f, 80.0f, 160.0f, 320.0f };
        glUniform1fv(m_LocShadowCascadeDepthRanges, MAX_SHADOW_CASCADES, defaultDepthRanges);
    }
    if (m_LocShadowCascadeWorldTexelSizes >= 0)
    {
        const float defaultWorldTexelSizes[MAX_SHADOW_CASCADES * 2] = {
            0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
        glUniform2fv(m_LocShadowCascadeWorldTexelSizes, MAX_SHADOW_CASCADES,
            defaultWorldTexelSizes);
    }
    if (m_LocDirectionalSourceAngularRadius >= 0)
        glUniform1f(m_LocDirectionalSourceAngularRadius, glm::radians(0.53f * 0.5f));
    if (m_LocPointShadowAngularTexelSize >= 0)
        glUniform1f(m_LocPointShadowAngularTexelSize, 2.0f / 512.0f);
    if (m_LocShadowCascadeCount >= 0)
        glUniform1i(m_LocShadowCascadeCount, MAX_SHADOW_CASCADES);
    if (m_LocUseShadows >= 0) glUniform1i(m_LocUseShadows, 1);
    if (m_LocDirectionalShadowsEnabled >= 0)
        glUniform1i(m_LocDirectionalShadowsEnabled, 1);
    if (m_LocMetallic >= 0) glUniform1f(m_LocMetallic, 0.0f);
    if (m_LocRoughness >= 0) glUniform1f(m_LocRoughness, 0.45f);
    if (m_LocSpecular >= 0) glUniform1f(m_LocSpecular, 0.5f);
    if (m_LocEmissionStrength >= 0) glUniform1f(m_LocEmissionStrength, 0.0f);
    if (m_LocGlobalEnabled >= 0) glUniform1i(m_LocGlobalEnabled, 1);
    if (m_LocGlobalBrightness >= 0) glUniform1f(m_LocGlobalBrightness, 1.0f);
    if (m_LocGlobalContrast >= 0) glUniform1f(m_LocGlobalContrast, 1.0f);
    if (m_LocGlobalSaturation >= 0) glUniform1f(m_LocGlobalSaturation, 1.0f);
    if (m_LocGlobalExposure >= 0) glUniform1f(m_LocGlobalExposure, 0.0f);
    if (m_LocGlobalTint >= 0) glUniform3f(m_LocGlobalTint, 1.0f, 1.0f, 1.0f);
    if (m_LocGlobalGamma >= 0) glUniform1f(m_LocGlobalGamma, 1.0f);
    if (m_LocGlobalFogEnabled >= 0) glUniform1i(m_LocGlobalFogEnabled, 0);
    if (m_LocGlobalFogColor >= 0) glUniform3f(m_LocGlobalFogColor, 0.55f, 0.62f, 0.70f);
    if (m_LocGlobalFogDensity >= 0) glUniform1f(m_LocGlobalFogDensity, 0.015f);
    if (m_LocGlobalFogStart >= 0) glUniform1f(m_LocGlobalFogStart, 10.0f);
    if (m_LocGlobalFogEnd >= 0) glUniform1f(m_LocGlobalFogEnd, 250.0f);
    if (m_LocGlobalVignetteStrength >= 0) glUniform1f(m_LocGlobalVignetteStrength, 0.0f);
    if (m_LocGlobalVignetteRadius >= 0) glUniform1f(m_LocGlobalVignetteRadius, 0.72f);
    if (m_LocViewportSize >= 0) glUniform2f(m_LocViewportSize, 1280.0f, 720.0f);
    m_LocToneMapping = glGetUniformLocation(m_ProgramID, "u_ToneMapping");
    m_LocTemperature = glGetUniformLocation(m_ProgramID, "u_Temperature");
    m_LocTint = glGetUniformLocation(m_ProgramID, "u_Tint");
    m_LocFilmGrainEnabled = glGetUniformLocation(m_ProgramID, "u_FilmGrainEnabled");
    m_LocFilmGrainStrength = glGetUniformLocation(m_ProgramID, "u_FilmGrainStrength");
    m_LocFilmGrainSize = glGetUniformLocation(m_ProgramID, "u_FilmGrainSize");
    m_LocFilmGrainSpeed = glGetUniformLocation(m_ProgramID, "u_FilmGrainSpeed");
    m_LocChromaticAberrationEnabled = glGetUniformLocation(m_ProgramID, "u_ChromaticAberrationEnabled");
    m_LocChromaticAberrationStrength = glGetUniformLocation(m_ProgramID, "u_ChromaticAberrationStrength");
    m_LocBloomStrength = glGetUniformLocation(m_ProgramID, "u_BloomStrength");
    m_LocSharpenStrength = glGetUniformLocation(m_ProgramID, "u_SharpenStrength");
    m_LocGlobalLightEnabledNew = glGetUniformLocation(m_ProgramID, "u_GlobalLightEnabledNew");
    m_LocGlobalIntensity = glGetUniformLocation(m_ProgramID, "u_GlobalIntensity");
    m_LocGlobalShadowStrength = glGetUniformLocation(m_ProgramID, "u_GlobalShadowStrength");
    m_LocAmbientIntensity = glGetUniformLocation(m_ProgramID, "u_AmbientIntensity");
    m_LocSkyLightIntensity = glGetUniformLocation(m_ProgramID, "u_SkyLightIntensity");
    m_LocSceneExposure = glGetUniformLocation(m_ProgramID, "u_SceneExposure");
    m_LocAmbientColorNew = glGetUniformLocation(m_ProgramID, "u_AmbientColorNew");
    m_LocAdvancedLightingEnabled = glGetUniformLocation(m_ProgramID, "u_AdvancedLightingEnabled");
    m_LocIndirectIntensity = glGetUniformLocation(m_ProgramID, "u_IndirectIntensity");
    m_LocContactShadowStrength = glGetUniformLocation(m_ProgramID, "u_ContactShadowStrength");
    m_LocContactShadowDistance = glGetUniformLocation(m_ProgramID, "u_ContactShadowDistance");
    m_LocNormalMap = glGetUniformLocation(m_ProgramID, "u_NormalMap");
    m_LocHasNormalMap = glGetUniformLocation(m_ProgramID, "u_HasNormalMap");
    m_LocNormalStrength = glGetUniformLocation(m_ProgramID, "u_NormalStrength");

    // Local Light defaults
    if (m_LocLocalLightEnabled >= 0) glUniform1i(m_LocLocalLightEnabled, 0);
    if (m_LocSunLightInfluence >= 0) glUniform1f(m_LocSunLightInfluence, 1.0f);
    if (m_LocArtificialLightInfluence >= 0) glUniform1f(m_LocArtificialLightInfluence, 1.0f);
    if (m_LocAmbientLightInfluence >= 0) glUniform1f(m_LocAmbientLightInfluence, 1.0f);
    if (m_LocReceiveShadows >= 0) glUniform1i(m_LocReceiveShadows, 1);
    if (m_LocShadowStrength >= 0) glUniform1f(m_LocShadowStrength, 1.0f);
    if (m_LocShadowBias >= 0) glUniform1f(m_LocShadowBias, 0.005f);
    if (m_LocEnableReflections >= 0) glUniform1i(m_LocEnableReflections, 0);
    if (m_LocReflectionStrength >= 0) glUniform1f(m_LocReflectionStrength, 0.5f);
    if (m_LocEnableAO >= 0) glUniform1i(m_LocEnableAO, 1);
    if (m_LocAOStrength >= 0) glUniform1f(m_LocAOStrength, 1.0f);

    m_LocEmissionMap = glGetUniformLocation(m_ProgramID, "u_EmissionMap");
    m_LocHasEmissionMap = glGetUniformLocation(m_ProgramID, "u_HasEmissionMap");
    m_LocUVMode = glGetUniformLocation(m_ProgramID, "u_UVMode");
    m_LocUVWorldScale = glGetUniformLocation(m_ProgramID, "u_UVWorldScale");
    m_LocObjectScale = glGetUniformLocation(m_ProgramID, "u_ObjectScale");
    m_LocUVAtlasGrid = glGetUniformLocation(m_ProgramID, "u_UVAtlasGrid");
    m_LocCanvasBgColor = glGetUniformLocation(m_ProgramID, "u_CanvasBgColor");
    m_LocUVExtraParams = glGetUniformLocation(m_ProgramID, "u_UVExtraParams");

    // Mirror reflection
    m_LocMirrorEnabled   = glGetUniformLocation(m_ProgramID, "u_MirrorEnabled");
    m_LocMirrorTexture   = glGetUniformLocation(m_ProgramID, "u_MirrorTexture");
    m_LocMirrorIntensity = glGetUniformLocation(m_ProgramID, "u_MirrorIntensity");
    m_LocMirrorBlur      = glGetUniformLocation(m_ProgramID, "u_MirrorBlur");
    m_LocMirrorMode      = glGetUniformLocation(m_ProgramID, "u_MirrorMode");
    if (m_LocMirrorMode >= 0) glUniform1i(m_LocMirrorMode, 0);
    if (m_LocToneMapping >= 0) glUniform1i(m_LocToneMapping, 2);
    if (m_LocTemperature >= 0) glUniform1f(m_LocTemperature, 0.0f);
    if (m_LocTint >= 0) glUniform1f(m_LocTint, 0.0f);
    if (m_LocFilmGrainEnabled >= 0) glUniform1i(m_LocFilmGrainEnabled, 0);
    if (m_LocFilmGrainStrength >= 0) glUniform1f(m_LocFilmGrainStrength, 0.0f);
    if (m_LocFilmGrainSize >= 0) glUniform1f(m_LocFilmGrainSize, 1.0f);
    if (m_LocFilmGrainSpeed >= 0) glUniform1f(m_LocFilmGrainSpeed, 1.0f);
    if (m_LocChromaticAberrationEnabled >= 0) glUniform1i(m_LocChromaticAberrationEnabled, 0);
    if (m_LocChromaticAberrationStrength >= 0) glUniform1f(m_LocChromaticAberrationStrength, 0.0f);
    if (m_LocBloomStrength >= 0) glUniform1f(m_LocBloomStrength, 0.0f);
    if (m_LocSharpenStrength >= 0) glUniform1f(m_LocSharpenStrength, 0.0f);
    if (m_LocGlobalLightEnabledNew >= 0) glUniform1i(m_LocGlobalLightEnabledNew, 1);
    if (m_LocGlobalIntensity >= 0) glUniform1f(m_LocGlobalIntensity, 1.0f);
    if (m_LocAmbientIntensity >= 0) glUniform1f(m_LocAmbientIntensity, 1.0f);
    if (m_LocSkyLightIntensity >= 0) glUniform1f(m_LocSkyLightIntensity, 1.0f);
    if (m_LocSceneExposure >= 0) glUniform1f(m_LocSceneExposure, 0.0f);
    if (m_LocAmbientColorNew >= 0) glUniform3f(m_LocAmbientColorNew, 1.0f, 1.0f, 1.0f);
    if (m_LocAdvancedLightingEnabled >= 0) glUniform1i(m_LocAdvancedLightingEnabled, 0);
    if (m_LocIndirectIntensity >= 0) glUniform1f(m_LocIndirectIntensity, 0.35f);
    if (m_LocContactShadowStrength >= 0) glUniform1f(m_LocContactShadowStrength, 0.35f);
    if (m_LocContactShadowDistance >= 0) glUniform1f(m_LocContactShadowDistance, 0.75f);
    if (m_LocNormalMap >= 0) glUniform1i(m_LocNormalMap, 15);
    if (m_LocEmissionMap >= 0) glUniform1i(m_LocEmissionMap, 16);
    if (m_LocHasNormalMap >= 0) glUniform1i(m_LocHasNormalMap, 0);
    if (m_LocHasEmissionMap >= 0) glUniform1i(m_LocHasEmissionMap, 0);
    if (m_LocNormalStrength >= 0) glUniform1f(m_LocNormalStrength, 1.0f);
    if (m_LocUVMode >= 0) glUniform1i(m_LocUVMode, 0);
    if (m_LocUVWorldScale >= 0) glUniform1f(m_LocUVWorldScale, 1.0f);
    if (m_LocObjectScale >= 0) glUniform3f(m_LocObjectScale, 1.0f, 1.0f, 1.0f);
    if (m_LocUVAtlasGrid >= 0) glUniform2f(m_LocUVAtlasGrid, 1.0f, 1.0f);
    if (m_LocCanvasBgColor >= 0) glUniform4f(m_LocCanvasBgColor, 1.0f, 1.0f, 1.0f, 1.0f);
    for (int i = 0; i < MAX_LOCAL_SHADOWS; ++i)
    {
        // Units 1..4 are reserved for the four cascades do sol. As sombras
        // locais ficam depois delas para que os samplers nunca se sobreponham.
        if (m_LocPointShadowMaps[i] >= 0) glUniform1i(m_LocPointShadowMaps[i], 5 + i);
        if (m_LocPointShadowOpacityMaps[i] >= 0) glUniform1i(m_LocPointShadowOpacityMaps[i], 10 + i);
        if (m_LocPointShadowEnabled[i] >= 0) glUniform1i(m_LocPointShadowEnabled[i], 0);
        if (m_LocPointShadowFarPlanes[i] >= 0) glUniform1f(m_LocPointShadowFarPlanes[i], 10.0f);
    }

    m_LocLightDirection  = glGetUniformLocation(m_ProgramID, "u_LightDirection");
    m_LocLightColor      = glGetUniformLocation(m_ProgramID, "u_LightColor");
    m_LocLightIntensity  = glGetUniformLocation(m_ProgramID, "u_LightIntensity");
    m_LocShadowIntensity = glGetUniformLocation(m_ProgramID, "u_ShadowIntensity");
    m_LocAmbientLight    = glGetUniformLocation(m_ProgramID, "u_AmbientLight");

    if (m_LocLightIntensity  >= 0) glUniform1f(m_LocLightIntensity,  1.0f);
    if (m_LocShadowIntensity >= 0) glUniform1f(m_LocShadowIntensity, 0.5f);
    if (m_LocAmbientLight    >= 0) glUniform1f(m_LocAmbientLight,    0.4f);

    // Point light uniforms
    m_LocPointLightCount = glGetUniformLocation(m_ProgramID, "u_PointLightCount");
    if (m_LocPointLightCount >= 0) glUniform1i(m_LocPointLightCount, 0);
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
    {
        auto fmt = [&](const char* field) {
            return std::string("u_PointLights[") + std::to_string(i) + "]." + field;
        };
        m_LocPointLights[i].pos       = glGetUniformLocation(m_ProgramID, fmt("Position").c_str());
        m_LocPointLights[i].color     = glGetUniformLocation(m_ProgramID, fmt("Color").c_str());
        m_LocPointLights[i].intensity = glGetUniformLocation(m_ProgramID, fmt("Intensity").c_str());
        m_LocPointLights[i].radius    = glGetUniformLocation(m_ProgramID, fmt("Radius").c_str());
        m_LocPointLights[i].sourceRadius = glGetUniformLocation(m_ProgramID, fmt("SourceRadius").c_str());
        m_LocPointLights[i].shadowSoftness = glGetUniformLocation(m_ProgramID, fmt("ShadowSoftness").c_str());
        m_LocPointLights[i].shadowStrength = glGetUniformLocation(m_ProgramID, fmt("ShadowStrength").c_str());
        m_LocPointLights[i].distanceSoftening = glGetUniformLocation(m_ProgramID, fmt("DistanceSoftening").c_str());
        m_LocPointLights[i].indirectContribution = glGetUniformLocation(m_ProgramID, fmt("IndirectContribution").c_str());
        m_LocPointLights[i].shadowIndex = glGetUniformLocation(m_ProgramID, fmt("ShadowIndex").c_str());
    }

    // Spot light uniforms
    m_LocSpotLightCount = glGetUniformLocation(m_ProgramID, "u_SpotLightCount");
    if (m_LocSpotLightCount >= 0) glUniform1i(m_LocSpotLightCount, 0);
    for (int i = 0; i < MAX_SPOT_LIGHTS; ++i)
    {
        auto fmt = [&](const char* field) {
            return std::string("u_SpotLights[") + std::to_string(i) + "]." + field;
        };
        m_LocSpotLights[i].pos       = glGetUniformLocation(m_ProgramID, fmt("Position").c_str());
        m_LocSpotLights[i].dir       = glGetUniformLocation(m_ProgramID, fmt("Direction").c_str());
        m_LocSpotLights[i].color     = glGetUniformLocation(m_ProgramID, fmt("Color").c_str());
        m_LocSpotLights[i].intensity = glGetUniformLocation(m_ProgramID, fmt("Intensity").c_str());
        m_LocSpotLights[i].range     = glGetUniformLocation(m_ProgramID, fmt("Range").c_str());
        m_LocSpotLights[i].innerCos  = glGetUniformLocation(m_ProgramID, fmt("InnerCos").c_str());
        m_LocSpotLights[i].outerCos  = glGetUniformLocation(m_ProgramID, fmt("OuterCos").c_str());
        m_LocSpotLights[i].sourceRadius = glGetUniformLocation(m_ProgramID, fmt("SourceRadius").c_str());
        m_LocSpotLights[i].shadowSoftness = glGetUniformLocation(m_ProgramID, fmt("ShadowSoftness").c_str());
        m_LocSpotLights[i].shadowStrength = glGetUniformLocation(m_ProgramID, fmt("ShadowStrength").c_str());
        m_LocSpotLights[i].distanceSoftening = glGetUniformLocation(m_ProgramID, fmt("DistanceSoftening").c_str());
        m_LocSpotLights[i].indirectContribution = glGetUniformLocation(m_ProgramID, fmt("IndirectContribution").c_str());
        m_LocSpotLights[i].shadowIndex = glGetUniformLocation(m_ProgramID, fmt("ShadowIndex").c_str());
    }

    CreateShadowShader();
    CreatePointShadowShader();

    ++s_ShaderCount;

    return shaderReady;
}

bool Shader::CreateShadowShader()
{
    const char* depthVertexSource =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "uniform mat4 u_LightSpaceMatrix;\n"
        "uniform mat4 u_Model;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = u_LightSpaceMatrix * u_Model * vec4(aPos, 1.0);\n"
        "}\n";

    const char* depthFragmentSource =
        "#version 330 core\n"
        "uniform float u_Opacity;\n"
        "layout(location = 0) out float FragOpacity;\n"
        "void main()\n"
        "{\n"
        "    FragOpacity = clamp(u_Opacity, 0.0, 1.0);\n"
        "}\n";

    unsigned int vShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vShader, 1, &depthVertexSource, nullptr);
    glCompileShader(vShader);
    const bool vertexCompiled = CheckShaderStage(vShader, "vertex de sombra direcional");

    unsigned int fShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fShader, 1, &depthFragmentSource, nullptr);
    glCompileShader(fShader);
    const bool fragmentCompiled = CheckShaderStage(fShader, "fragment de sombra direcional");

    m_ShadowProgramID = glCreateProgram();
    glAttachShader(m_ShadowProgramID, vShader);
    glAttachShader(m_ShadowProgramID, fShader);
    glLinkProgram(m_ShadowProgramID);
    const bool programLinked = CheckShaderProgram(m_ShadowProgramID, "programa de sombra direcional");

    glDeleteShader(vShader);
    glDeleteShader(fShader);

    if (!vertexCompiled || !fragmentCompiled || !programLinked)
    {
        glDeleteProgram(m_ShadowProgramID);
        m_ShadowProgramID = 0;
        return false;
    }

    glUseProgram(m_ShadowProgramID);
    m_LocShadowModel      = glGetUniformLocation(m_ShadowProgramID, "u_Model");
    m_LocShadowLightSpace = glGetUniformLocation(m_ShadowProgramID, "u_LightSpaceMatrix");
    m_LocShadowOpacity    = glGetUniformLocation(m_ShadowProgramID, "u_Opacity");

    return true;
}

bool Shader::CreatePointShadowShader()
{
    const char* vertexShaderSource =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "uniform mat4 u_LightSpaceMatrix;\n"
        "uniform mat4 u_Model;\n"
        "out vec3 WorldPos;\n"
        "void main()\n"
        "{\n"
        "    vec4 worldPosition = u_Model * vec4(aPos, 1.0);\n"
        "    WorldPos = worldPosition.xyz;\n"
        "    gl_Position = u_LightSpaceMatrix * worldPosition;\n"
        "}\n";

    const char* fragmentShaderSource =
        "#version 330 core\n"
        "in vec3 WorldPos;\n"
        "uniform vec3 u_LightPosition;\n"
        "uniform float u_FarPlane;\n"
        "uniform float u_Opacity;\n"
        "layout(location = 0) out float FragOpacity;\n"
        "void main()\n"
        "{\n"
        "    gl_FragDepth = length(WorldPos - u_LightPosition) / max(u_FarPlane, 0.001);\n"
        "    FragOpacity = clamp(u_Opacity, 0.0, 1.0);\n"
        "}\n";

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);
    const bool vertexCompiled = CheckShaderStage(vertexShader, "vertex de sombra local");

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);
    const bool fragmentCompiled = CheckShaderStage(fragmentShader, "fragment de sombra local");

    m_PointShadowProgramID = glCreateProgram();
    glAttachShader(m_PointShadowProgramID, vertexShader);
    glAttachShader(m_PointShadowProgramID, fragmentShader);
    glLinkProgram(m_PointShadowProgramID);
    const bool programLinked = CheckShaderProgram(m_PointShadowProgramID, "programa de sombra local");

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!vertexCompiled || !fragmentCompiled || !programLinked)
    {
        glDeleteProgram(m_PointShadowProgramID);
        m_PointShadowProgramID = 0;
        return false;
    }

    glUseProgram(m_PointShadowProgramID);
    m_LocPointShadowModel = glGetUniformLocation(m_PointShadowProgramID, "u_Model");
    m_LocPointShadowLightSpace = glGetUniformLocation(m_PointShadowProgramID, "u_LightSpaceMatrix");
    m_LocPointShadowLightPosition = glGetUniformLocation(m_PointShadowProgramID, "u_LightPosition");
    m_LocPointShadowFarPlane = glGetUniformLocation(m_PointShadowProgramID, "u_FarPlane");
    m_LocPointShadowOpacity = glGetUniformLocation(m_PointShadowProgramID, "u_Opacity");

    return true;
}

void Shader::UseShadowShader()
{
    if (m_ShadowProgramID != 0)
        glUseProgram(m_ShadowProgramID);
}

void Shader::SetShadowModelMatrix(const glm::mat4& model)
{
    if (m_LocShadowModel >= 0)
        glUniformMatrix4fv(m_LocShadowModel, 1, GL_FALSE, &model[0][0]);
}

void Shader::SetShadowOpacity(float opacity)
{
    if (m_LocShadowOpacity >= 0)
        glUniform1f(m_LocShadowOpacity, std::clamp(opacity, 0.0f, 1.0f));
}

void Shader::SetShadowLightSpaceMatrix(const glm::mat4& matrix)
{
    if (m_LocShadowLightSpace >= 0)
        glUniformMatrix4fv(m_LocShadowLightSpace, 1, GL_FALSE, &matrix[0][0]);
}

unsigned int Shader::GetShadowProgramID() const
{
    return m_ShadowProgramID;
}

Shader::~Shader()
{
    if (m_ProgramID != 0)
    {
        glDeleteProgram(m_ProgramID);
        --s_ShaderCount;
        m_ProgramID = 0;
    }

    if (m_ShadowProgramID != 0)
    {
        glDeleteProgram(m_ShadowProgramID);
        m_ShadowProgramID = 0;
    }

    if (m_PointShadowProgramID != 0)
    {
        glDeleteProgram(m_PointShadowProgramID);
        m_PointShadowProgramID = 0;
    }
}

void Shader::Use()
{
    glUseProgram(m_ProgramID);
}

void Shader::SetOffset(float x, float y, float z)
{
    int location = glGetUniformLocation(m_ProgramID, "u_Offset");
    glUniform3f(location, x, y, z);
}

void Shader::SetModelMatrix(const glm::mat4& model)
{
    if (m_LocModel >= 0)
        glUniformMatrix4fv(m_LocModel, 1, GL_FALSE, &model[0][0]);
}

void Shader::SetSkinMatrices(const std::vector<glm::mat4>& matrices)
{
    if (m_ProgramID == 0)
        return;

    const int count = std::clamp(static_cast<int>(matrices.size()), 0, MAX_SKIN_JOINTS);
    if (m_LocSkinningEnabled >= 0)
        glUniform1i(m_LocSkinningEnabled, count > 0 ? 1 : 0);
    if (count > 0 && m_LocSkinMatrices >= 0)
        glUniformMatrix4fv(m_LocSkinMatrices, count, GL_FALSE, glm::value_ptr(matrices.front()));
}

void Shader::SetViewMatrix(const glm::mat4& view)
{
    if (m_LocView >= 0)
        glUniformMatrix4fv(m_LocView, 1, GL_FALSE, &view[0][0]);
}

void Shader::SetProjectionMatrix(const glm::mat4& projection)
{
    if (m_LocProjection >= 0)
        glUniformMatrix4fv(m_LocProjection, 1, GL_FALSE, &projection[0][0]);
}

void Shader::SetLightSpaceMatrix(const glm::mat4& matrix)
{
    if (m_LocLightSpaceMatrix >= 0)
        glUniformMatrix4fv(m_LocLightSpaceMatrix, 1, GL_FALSE, &matrix[0][0]);
}

void Shader::SetShadowCascadeMatrix(int cascade, const glm::mat4& matrix)
{
    if (cascade < 0 || cascade >= MAX_SHADOW_CASCADES)
        return;
    if (m_LocShadowCascadeMatrices[cascade] >= 0)
    {
        glUniformMatrix4fv(
            m_LocShadowCascadeMatrices[cascade], 1, GL_FALSE, &matrix[0][0]);
    }
}

void Shader::SetShadowOpacityArray(int textureUnit)
{
    if (m_LocShadowOpacityArray >= 0)
        glUniform1i(m_LocShadowOpacityArray, textureUnit);
}

void Shader::SetShadowCascadeFarPlanes(const float* farPlanes, int count)
{
    if (!farPlanes || m_LocShadowCascadeFarPlanes < 0)
        return;
    const int clampedCount = std::clamp(count, 1, MAX_SHADOW_CASCADES);
    glUniform1fv(m_LocShadowCascadeFarPlanes, clampedCount, farPlanes);
    if (m_LocShadowCascadeCount >= 0)
        glUniform1i(m_LocShadowCascadeCount, clampedCount);
}

void Shader::SetShadowCascadeDepthRanges(const float* depthRanges, int count)
{
    if (!depthRanges || m_LocShadowCascadeDepthRanges < 0)
        return;
    const int clampedCount = std::clamp(count, 1, MAX_SHADOW_CASCADES);
    glUniform1fv(m_LocShadowCascadeDepthRanges, clampedCount, depthRanges);
}

void Shader::SetShadowCascadeWorldTexelSizes(const glm::vec2* sizes, int count)
{
    if (!sizes || m_LocShadowCascadeWorldTexelSizes < 0)
        return;
    const int clampedCount = std::clamp(count, 1, MAX_SHADOW_CASCADES);
    glUniform2fv(m_LocShadowCascadeWorldTexelSizes, clampedCount, &sizes[0].x);
}

void Shader::SetDirectionalSourceAngularRadius(float radians)
{
    if (m_LocDirectionalSourceAngularRadius >= 0)
        glUniform1f(m_LocDirectionalSourceAngularRadius,
            std::clamp(radians, glm::radians(0.005f), glm::radians(2.5f)));
}

void Shader::SetPointShadowAngularTexelSize(float angularTexelSize)
{
    if (m_LocPointShadowAngularTexelSize >= 0)
        glUniform1f(m_LocPointShadowAngularTexelSize,
            std::clamp(angularTexelSize, 0.0001f, 0.1f));
}

void Shader::SetUseShadows(bool use)
{
    if (m_LocUseShadows >= 0)
        glUniform1i(m_LocUseShadows, use ? 1 : 0);
}

void Shader::SetDirectionalShadowsEnabled(bool enabled)
{
    if (m_LocDirectionalShadowsEnabled >= 0)
        glUniform1i(m_LocDirectionalShadowsEnabled, enabled ? 1 : 0);
}

void Shader::SetDirectionalShadowDistanceSoftening(bool enabled)
{
    if (m_LocDirectionalShadowDistanceSoftening >= 0)
        glUniform1i(m_LocDirectionalShadowDistanceSoftening, enabled ? 1 : 0);
}

void Shader::SetDirectionalShadowSubTexelFilter(bool enabled)
{
    if (m_LocDirectionalShadowSubTexelFilter >= 0)
        glUniform1i(m_LocDirectionalShadowSubTexelFilter, enabled ? 1 : 0);
}

void Shader::SetGlobalShadowStrength(float strength)
{
    if (m_LocGlobalShadowStrength >= 0)
        glUniform1f(m_LocGlobalShadowStrength, strength);
}

void Shader::SetCameraPosition(float x, float y, float z)
{
    if (m_LocCameraPosition >= 0)
        glUniform3f(m_LocCameraPosition, x, y, z);
}

void Shader::SetColor(float r, float g, float b)
{
    if (m_LocColor >= 0)
        glUniform3f(m_LocColor, r, g, b);
}

void Shader::SetOpacity(float opacity)
{
    if (m_LocOpacity >= 0)
        glUniform1f(m_LocOpacity, std::clamp(opacity, 0.0f, 1.0f));
}

unsigned int Shader::GetProgramID() const
{
    return m_ProgramID;
}

void Shader::SetAmbientLight(float r, float g, float b)
{
    int location = glGetUniformLocation(m_ProgramID, "u_AmbientLight");
    if (location >= 0)
        glUniform3f(location, r, g, b);
}

void Shader::SetLightDirection(float x, float y, float z)
{
    if (m_LocLightDirection >= 0)
        glUniform3f(m_LocLightDirection, x, y, z);
}

void Shader::SetLightColor(float r, float g, float b)
{
    if (m_LocLightColor >= 0)
        glUniform3f(m_LocLightColor, r, g, b);
}

void Shader::SetLightIntensity(float intensity)
{
    if (m_LocLightIntensity >= 0)
        glUniform1f(m_LocLightIntensity, intensity);
}

void Shader::SetShadowIntensity(float intensity)
{
    if (m_LocShadowIntensity >= 0)
        glUniform1f(m_LocShadowIntensity, intensity);
}

void Shader::SetAmbientLight(float intensity)
{
    if (m_LocAmbientLight >= 0)
        glUniform1f(m_LocAmbientLight, intensity);
}

void Shader::SetGlobalLightFilter(
    bool enabled,
    float brightness,
    float contrast,
    float saturation,
    float exposure,
    float tintR,
    float tintG,
    float tintB)
{
    if (m_LocGlobalEnabled >= 0) glUniform1i(m_LocGlobalEnabled, enabled ? 1 : 0);
    if (m_LocGlobalBrightness >= 0) glUniform1f(m_LocGlobalBrightness, brightness);
    if (m_LocGlobalContrast >= 0) glUniform1f(m_LocGlobalContrast, contrast);
    if (m_LocGlobalSaturation >= 0) glUniform1f(m_LocGlobalSaturation, saturation);
    if (m_LocGlobalExposure >= 0) glUniform1f(m_LocGlobalExposure, exposure);
    if (m_LocGlobalTint >= 0) glUniform3f(m_LocGlobalTint, tintR, tintG, tintB);
}

void Shader::SetGlobalLightAtmosphere(
    bool fogEnabled,
    float fogR,
    float fogG,
    float fogB,
    float fogDensity,
    float fogStart,
    float fogEnd,
    float vignetteStrength,
    float vignetteRadius,
    float gamma,
    float viewportWidth,
    float viewportHeight)
{
    if (m_LocGlobalFogEnabled >= 0) glUniform1i(m_LocGlobalFogEnabled, fogEnabled ? 1 : 0);
    if (m_LocGlobalFogColor >= 0) glUniform3f(m_LocGlobalFogColor, fogR, fogG, fogB);
    if (m_LocGlobalFogDensity >= 0) glUniform1f(m_LocGlobalFogDensity, fogDensity);
    if (m_LocGlobalFogStart >= 0) glUniform1f(m_LocGlobalFogStart, fogStart);
    if (m_LocGlobalFogEnd >= 0) glUniform1f(m_LocGlobalFogEnd, fogEnd);
    if (m_LocGlobalVignetteStrength >= 0) glUniform1f(m_LocGlobalVignetteStrength, vignetteStrength);
    if (m_LocGlobalVignetteRadius >= 0) glUniform1f(m_LocGlobalVignetteRadius, vignetteRadius);
    if (m_LocGlobalGamma >= 0) glUniform1f(m_LocGlobalGamma, gamma);
    if (m_LocViewportSize >= 0) glUniform2f(m_LocViewportSize, viewportWidth, viewportHeight);
}

void Shader::SetPostProcessing(
    int toneMapping, float temperature, float tint,
    bool filmGrainEnabled, float filmGrainStrength, float filmGrainSize, float filmGrainSpeed,
    bool chromaticAberrationEnabled, float chromaticAberrationStrength,
    float bloomStrength, float sharpenStrength)
{
    if (m_LocToneMapping >= 0) glUniform1i(m_LocToneMapping, toneMapping);
    if (m_LocTemperature >= 0) glUniform1f(m_LocTemperature, temperature);
    if (m_LocTint >= 0) glUniform1f(m_LocTint, tint);
    if (m_LocFilmGrainEnabled >= 0) glUniform1i(m_LocFilmGrainEnabled, filmGrainEnabled ? 1 : 0);
    if (m_LocFilmGrainStrength >= 0) glUniform1f(m_LocFilmGrainStrength, filmGrainStrength);
    if (m_LocFilmGrainSize >= 0) glUniform1f(m_LocFilmGrainSize, filmGrainSize);
    if (m_LocFilmGrainSpeed >= 0) glUniform1f(m_LocFilmGrainSpeed, filmGrainSpeed);
    if (m_LocChromaticAberrationEnabled >= 0) glUniform1i(m_LocChromaticAberrationEnabled, chromaticAberrationEnabled ? 1 : 0);
    if (m_LocChromaticAberrationStrength >= 0) glUniform1f(m_LocChromaticAberrationStrength, chromaticAberrationStrength);
    if (m_LocBloomStrength >= 0) glUniform1f(m_LocBloomStrength, bloomStrength);
    if (m_LocSharpenStrength >= 0) glUniform1f(m_LocSharpenStrength, sharpenStrength);
}

void Shader::SetGlobalLightingParams(
    bool enabled,
    float globalIntensity,
    float ambientIntensity,
    float skyLightIntensity,
    float sceneExposure,
    const glm::vec3& ambientColor,
    bool advancedLightingEnabled,
    float indirectIntensity,
    float contactShadowStrength,
    float contactShadowDistance)
{
    Use();
    if (m_LocGlobalLightEnabledNew >= 0) glUniform1i(m_LocGlobalLightEnabledNew, enabled ? 1 : 0);
    if (m_LocGlobalIntensity >= 0)       glUniform1f(m_LocGlobalIntensity, globalIntensity);
    if (m_LocAmbientIntensity >= 0)      glUniform1f(m_LocAmbientIntensity, ambientIntensity);
    if (m_LocSkyLightIntensity >= 0)     glUniform1f(m_LocSkyLightIntensity, skyLightIntensity);
    if (m_LocSceneExposure >= 0)         glUniform1f(m_LocSceneExposure, sceneExposure);
    if (m_LocAmbientColorNew >= 0)       glUniform3fv(m_LocAmbientColorNew, 1, glm::value_ptr(ambientColor));
    if (m_LocAdvancedLightingEnabled >= 0) glUniform1i(m_LocAdvancedLightingEnabled, advancedLightingEnabled ? 1 : 0);
    if (m_LocIndirectIntensity >= 0) glUniform1f(m_LocIndirectIntensity, std::clamp(indirectIntensity, 0.0f, 2.0f));
    if (m_LocContactShadowStrength >= 0) glUniform1f(m_LocContactShadowStrength, std::clamp(contactShadowStrength, 0.0f, 1.0f));
    if (m_LocContactShadowDistance >= 0) glUniform1f(m_LocContactShadowDistance, std::max(0.01f, contactShadowDistance));
}

void Shader::SetAdvancedLightingEnabled(bool enabled)
{
    if (m_LocAdvancedLightingEnabled >= 0)
        glUniform1i(m_LocAdvancedLightingEnabled, enabled ? 1 : 0);
}

void Shader::SetMaterialTextures(
    int normalTextureUnit,
    bool hasNormalMap,
    float normalStrength,
    int emissionTextureUnit,
    bool hasEmissionMap)
{
    if (m_LocNormalMap >= 0) glUniform1i(m_LocNormalMap, normalTextureUnit);
    if (m_LocHasNormalMap >= 0) glUniform1i(m_LocHasNormalMap, hasNormalMap ? 1 : 0);
    if (m_LocNormalStrength >= 0) glUniform1f(m_LocNormalStrength, std::clamp(normalStrength, 0.0f, 2.0f));
    if (m_LocEmissionMap >= 0) glUniform1i(m_LocEmissionMap, emissionTextureUnit);
    if (m_LocHasEmissionMap >= 0) glUniform1i(m_LocHasEmissionMap, hasEmissionMap ? 1 : 0);
}

void Shader::SetEmission(float r, float g, float b)
{
    if (m_LocEmission >= 0)
        glUniform3f(m_LocEmission, r, g, b);
}

void Shader::UsePointShadowShader()
{
    if (m_PointShadowProgramID != 0)
        glUseProgram(m_PointShadowProgramID);
}

void Shader::SetPointShadowModelMatrix(const glm::mat4& model)
{
    if (m_LocPointShadowModel >= 0)
        glUniformMatrix4fv(m_LocPointShadowModel, 1, GL_FALSE, &model[0][0]);
}

void Shader::SetPointShadowLightSpaceMatrix(const glm::mat4& matrix)
{
    if (m_LocPointShadowLightSpace >= 0)
        glUniformMatrix4fv(m_LocPointShadowLightSpace, 1, GL_FALSE, &matrix[0][0]);
}

void Shader::SetPointShadowLightPosition(float x, float y, float z)
{
    if (m_LocPointShadowLightPosition >= 0)
        glUniform3f(m_LocPointShadowLightPosition, x, y, z);
}

void Shader::SetPointShadowFarPlane(float farPlane)
{
    if (m_LocPointShadowFarPlane >= 0)
        glUniform1f(m_LocPointShadowFarPlane, farPlane);
}

void Shader::SetPointShadowOpacity(float opacity)
{
    if (m_LocPointShadowOpacity >= 0)
        glUniform1f(m_LocPointShadowOpacity, std::clamp(opacity, 0.0f, 1.0f));
}

void Shader::SetMaterialProperties(
    float metallic,
    float roughness,
    float specular,
    float emissionStrength
)
{
    if (m_LocMetallic >= 0)
        glUniform1f(m_LocMetallic, metallic);
    if (m_LocRoughness >= 0)
        glUniform1f(m_LocRoughness, roughness);
    if (m_LocSpecular >= 0)
        glUniform1f(m_LocSpecular, specular);
    if (m_LocEmissionStrength >= 0)
        glUniform1f(m_LocEmissionStrength, emissionStrength);
}

void Shader::SetLocalLightEnabled(bool enabled)
{
    if (m_LocLocalLightEnabled >= 0)
        glUniform1i(m_LocLocalLightEnabled, enabled ? 1 : 0);
}

void Shader::SetLocalLightInfluence(
    float sunInfluence,
    float artificialInfluence,
    float ambientInfluence
)
{
    if (m_LocSunLightInfluence >= 0)
        glUniform1f(m_LocSunLightInfluence, sunInfluence);
    if (m_LocArtificialLightInfluence >= 0)
        glUniform1f(m_LocArtificialLightInfluence, artificialInfluence);
    if (m_LocAmbientLightInfluence >= 0)
        glUniform1f(m_LocAmbientLightInfluence, ambientInfluence);
}

void Shader::SetLocalLightShadow(
    bool receiveShadows,
    float shadowStrength,
    float shadowBias
)
{
    if (m_LocReceiveShadows >= 0)
        glUniform1i(m_LocReceiveShadows, receiveShadows ? 1 : 0);
    if (m_LocShadowStrength >= 0)
        glUniform1f(m_LocShadowStrength, shadowStrength);
    if (m_LocShadowBias >= 0)
        glUniform1f(m_LocShadowBias, shadowBias);
}

void Shader::SetLocalLightReflection(
    bool enableReflections,
    float reflectionStrength
)
{
    if (m_LocEnableReflections >= 0)
        glUniform1i(m_LocEnableReflections, enableReflections ? 1 : 0);
    if (m_LocReflectionStrength >= 0)
        glUniform1f(m_LocReflectionStrength, reflectionStrength);
}

void Shader::SetLocalLightAO(
    bool enableAO,
    float aoStrength
)
{
    if (m_LocEnableAO >= 0)
        glUniform1i(m_LocEnableAO, enableAO ? 1 : 0);
    if (m_LocAOStrength >= 0)
        glUniform1f(m_LocAOStrength, aoStrength);
}

void Shader::SetUVMode(
    int mode,
    float worldScale,
    const glm::vec3& objectScale,
    const glm::vec2& atlasGrid,
    const glm::vec4& bgColor,
    const glm::vec4& extraParams
)
{
    if (m_LocUVMode >= 0)
        glUniform1i(m_LocUVMode, mode);
    if (m_LocUVWorldScale >= 0)
        glUniform1f(m_LocUVWorldScale, worldScale);
    if (m_LocObjectScale >= 0)
        glUniform3f(m_LocObjectScale, objectScale.x, objectScale.y, objectScale.z);
    if (m_LocUVAtlasGrid >= 0)
        glUniform2f(m_LocUVAtlasGrid, atlasGrid.x, atlasGrid.y);
    if (m_LocCanvasBgColor >= 0)
        glUniform4f(m_LocCanvasBgColor, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
    if (m_LocUVExtraParams >= 0)
        glUniform4f(m_LocUVExtraParams, extraParams.x, extraParams.y, extraParams.z, extraParams.w);
}

void Shader::SetMirrorReflection(bool enabled, int reflectionTextureUnit,
                                   float intensity, float blur)
{
    if (m_LocMirrorEnabled >= 0)
        glUniform1i(m_LocMirrorEnabled, enabled ? 1 : 0);
    if (enabled && m_LocMirrorTexture >= 0)
        glUniform1i(m_LocMirrorTexture, reflectionTextureUnit);
    if (m_LocMirrorIntensity >= 0)
        glUniform1f(m_LocMirrorIntensity, intensity);
    if (m_LocMirrorBlur >= 0)
        glUniform1f(m_LocMirrorBlur, blur);
}

void Shader::SetMirrorMode(int mode)
{
    if (m_LocMirrorMode >= 0)
        glUniform1i(m_LocMirrorMode, mode);
}

void Shader::SetPointLightCount(int count)
{
    if (m_LocPointLightCount >= 0)
        glUniform1i(m_LocPointLightCount, std::clamp(count, 0, MAX_POINT_LIGHTS));
}

void Shader::SetPointLight(int index, float px, float py, float pz,
                            float cr, float cg, float cb,
                            float intensity, float radius,
                            int shadowIndex,
                            float sourceRadius,
                            float shadowSoftness,
                            float shadowStrength,
                            bool indirectContribution,
                            bool distanceSoftening)
{
    if (index < 0 || index >= MAX_POINT_LIGHTS) return;
    const auto& locs = m_LocPointLights[index];
    if (locs.pos       >= 0) glUniform3f(locs.pos,       px, py, pz);
    if (locs.color     >= 0) glUniform3f(locs.color,     cr, cg, cb);
    if (locs.intensity >= 0) glUniform1f(locs.intensity,  intensity);
    if (locs.radius    >= 0) glUniform1f(locs.radius,     radius);
    if (locs.sourceRadius >= 0) glUniform1f(locs.sourceRadius, std::max(0.025f, sourceRadius));
    if (locs.shadowSoftness >= 0) glUniform1f(locs.shadowSoftness, std::clamp(shadowSoftness, 0.0f, 1.0f));
    if (locs.shadowStrength >= 0) glUniform1f(locs.shadowStrength, std::clamp(shadowStrength, 0.0f, 1.0f));
    if (locs.distanceSoftening >= 0) glUniform1i(locs.distanceSoftening, distanceSoftening ? 1 : 0);
    if (locs.indirectContribution >= 0)
        glUniform1i(locs.indirectContribution, indirectContribution ? 1 : 0);
    if (locs.shadowIndex >= 0) glUniform1i(locs.shadowIndex, shadowIndex);
}

void Shader::SetSpotLightCount(int count)
{
    if (m_LocSpotLightCount >= 0)
        glUniform1i(m_LocSpotLightCount, std::clamp(count, 0, MAX_SPOT_LIGHTS));
}

void Shader::SetSpotLight(int index,
                           float px, float py, float pz,
                           float dx, float dy, float dz,
                           float cr, float cg, float cb,
                           float intensity, float range,
                           float innerCos, float outerCos,
                           int shadowIndex,
                           float sourceRadius,
                           float shadowSoftness,
                           float shadowStrength,
                           bool indirectContribution,
                           bool distanceSoftening)
{
    if (index < 0 || index >= MAX_SPOT_LIGHTS) return;

    // Check if shader is properly initialized
    if (m_ProgramID == 0) return;

    const auto& locs = m_LocSpotLights[index];
    if (locs.pos       >= 0) glUniform3f(locs.pos,       px, py, pz);
    if (locs.dir       >= 0) glUniform3f(locs.dir,       dx, dy, dz);
    if (locs.color     >= 0) glUniform3f(locs.color,     cr, cg, cb);
    if (locs.intensity >= 0) glUniform1f(locs.intensity,  intensity);
    if (locs.range     >= 0) glUniform1f(locs.range,      range);
    if (locs.innerCos  >= 0) glUniform1f(locs.innerCos,   innerCos);
    if (locs.outerCos  >= 0) glUniform1f(locs.outerCos,   outerCos);
    if (locs.sourceRadius >= 0) glUniform1f(locs.sourceRadius, std::max(0.025f, sourceRadius));
    if (locs.shadowSoftness >= 0) glUniform1f(locs.shadowSoftness, std::clamp(shadowSoftness, 0.0f, 1.0f));
    if (locs.shadowStrength >= 0) glUniform1f(locs.shadowStrength, std::clamp(shadowStrength, 0.0f, 1.0f));
    if (locs.distanceSoftening >= 0) glUniform1i(locs.distanceSoftening, distanceSoftening ? 1 : 0);
    if (locs.indirectContribution >= 0)
        glUniform1i(locs.indirectContribution, indirectContribution ? 1 : 0);
    if (locs.shadowIndex >= 0) glUniform1i(locs.shadowIndex, shadowIndex);
}

void Shader::SetPointShadowMap(int index, int textureUnit)
{
    if (index < 0 || index >= MAX_LOCAL_SHADOWS)
        return;
    if (m_LocPointShadowMaps[index] >= 0)
        glUniform1i(m_LocPointShadowMaps[index], textureUnit);
}

void Shader::SetPointShadowEnabled(int index, bool enabled)
{
    if (index < 0 || index >= MAX_LOCAL_SHADOWS)
        return;
    if (m_LocPointShadowEnabled[index] >= 0)
        glUniform1i(m_LocPointShadowEnabled[index], enabled ? 1 : 0);
}

void Shader::SetPointShadowFarPlane(int index, float farPlane)
{
    if (index < 0 || index >= MAX_LOCAL_SHADOWS)
        return;
    if (m_LocPointShadowFarPlanes[index] >= 0)
        glUniform1f(m_LocPointShadowFarPlanes[index], farPlane);
}

