#pragma once

#include <glad/glad.h>
#include "CommonStructs.h"
#include "../../../Math/Vector3.h"
#include <string>
#include <glm/glm.hpp>

class PostProcessingShader
{
public:
    PostProcessingShader();
    ~PostProcessingShader();

    bool Create();
    void Destroy();

    enum class Pass : unsigned int
    {
        Composite = 0,
        BloomExtract,
        BloomBlur,
        BloomComposite,
        ToneMapOnly,
        Count
    };

    void UseCompositePass();
    void UseBloomExtractPass();
    void UseBloomBlurPass(bool horizontal);
    void UseBloomCompositePass();
    void UseToneMapOnlyPass();

    void SetSourceTexture(int unit);
    void SetBloomTexture(int unit);
    void SetLUTTexture(int unit);
    void SetDepthTexture(int unit);
    void SetViewportSize(float w, float h);
    void SetElapsedTime(float t);
    void SetFogCamera(const glm::mat4& inverseProjection,
                      const glm::mat4& inverseView,
                      const glm::mat4& projection,
                      const glm::vec3& cameraPosition);

    void SetGlobalLighting(const GlobalLightingSettings& g);
    void SetColorGrading(const ColorGradingSettings& c);
    void SetToneMapping(const ToneMappingSettings& t);
    void SetLUT(const LUTSettings& l);
    void SetBloom(const BloomSettings& b);
    void SetVignette(const VignetteSettings& v);
    void SetChromatic(const ChromaticAberrationSettings& c);
    void SetSharpen(const SharpenSettings& s);
    void SetLensDistort(const LensDistortionSettings& l);
    void SetFilmGrain(const FilmGrainSettings& f);
    void SetDoF(const DoFSettings& d);
    void SetMotionBlur(const MotionBlurSettings& m);
    void SetShadowIntensity(float v);
    void SetFogOnlyPass(bool enabled);
    void SetTemporalHistoryValid(bool valid);
    void SetAdvancedEffectQuality(int qualityLevel);
    void SetTemporalHistoryUVOffset(const glm::vec2& offset);
    void SetGodRaysSunScreen(const glm::vec2& sunScreenPos, bool sunInFront);

private:
    bool CompileProgram(GLuint& outProgram, const char* label, const char* vs, const char* fs);
    void InvalidateLocations();
    void CacheCompositeLocations();
    void CacheBloomExtractLocations();
    void CacheBloomBlurLocations();
    void CacheBloomCompositeLocations();
    void CacheToneMapOnlyLocations();

    GLuint m_Programs[(unsigned int)Pass::Count] = { 0 };

    struct CompositeLocs {
        int srcTex = -1;
        int bloomTex = -1;
        int lutTex = -1;
        int depthTex = -1;
        int viewport = -1;
        int time = -1;
        int shadowIntensity = -1;

        int gEnabled = -1;
        int gGlobalIntensity = -1;
        int gAmbientColor = -1;
        int gAmbientIntensity = -1;
        int gSkyLightIntensity = -1;
        int gSceneExposure = -1;
        int gBrightness = -1;
        int gContrast = -1;
        int gSaturation = -1;
        int gExposureEV = -1;
        int gColorTint = -1;

        int gFogEnabled = -1;
        int gFogColor = -1;
        int gFogDensity = -1;
        int gFogStart = -1;
        int gFogEnd = -1;
        int gFogMode = -1;
        int fogInverseProjection = -1;
        int fogInverseView = -1;
        int fogProjection = -1;
        int fogCameraPosition = -1;
        int fogOnlyPass = -1;

        int ssaoEnabled = -1;
        int ssaoIntensity = -1;
        int ssaoRadius = -1;
        int ssrEnabled = -1;
        int ssrStrength = -1;
        int ssrDistance = -1;
        int taaEnabled = -1;
        int taaHistoryWeight = -1;
        int taaHistoryValid = -1;
        int taaHistoryUVOffset = -1;
        int advancedEffectQuality = -1;

        int cExposure = -1;
        int cContrast = -1;
        int cSaturation = -1;
        int cTemperature = -1;
        int cTint = -1;
        int cBrightness = -1;
        int cGamma = -1;
        int cGain = -1;
        int cLift = -1;
        int cPresetGamma = -1;
        int cShadows = -1; int cShadowsW = -1;
        int cMidtones = -1; int cMidtonesW = -1;
        int cHighlights = -1; int cHighlightsW = -1;
        int cBalS = -1; int cBalM = -1; int cBalH = -1;

        int tEnabled = -1;
        int tOp = -1;
        int tReinhardWhite = -1;
        int tIntensity = -1;

        int lEnabled = -1; int lIntensity = -1;

        int bEnabled = -1; int bIntensity = -1; int bDirtIntensity = -1;

        int vEnabled = -1;
        int vIntensity = -1; int vRadius = -1;
        int vSmoothness = -1; int vFeather = -1; int vColor = -1;

        int chEnabled = -1; int chStrength = -1; int chEdgeBias = -1;

        int sEnabled = -1; int sStrength = -1; int sClamp = -1;

        int ldEnabled = -1; int ldK1 = -1; int ldK2 = -1; int ldCubic = -1;

        int fgEnabled = -1;
        int fgIntensity = -1; int fgSize = -1; int fgSpeed = -1;
        int fgShadowW = -1; int fgMidW = -1; int fgHighW = -1;

        int dEnabled = -1;
        int dFocalDist = -1; int dFocalRange = -1;
        int dAperture = -1; int dMaxBlur = -1;

        int mbEnabled = -1; int mbIntensity = -1; int mbSamples = -1;

        // God Rays (Raios Solares)
        int godRaysEnabled = -1;
        int godRaysSunScreenPos = -1;
        int godRaysIntensity = -1;
        int godRaysDensity = -1;
        int godRaysDecay = -1;
        int godRaysWeight = -1;
        int godRaysSunInFront = -1;
        int godRaysTint = -1;
        int godRaysMieG = -1;
        int godRaysHaloRadius = -1;
        int godRaysHaloIntensity = -1;
        int godRaysSunAltitude = -1;

        // Glare Rays (Raios de Brilho / Neons e Luzes)
        int glareEnabled = -1;
        int glareIntensity = -1;
        int glareLength = -1;
        int glareThreshold = -1;
    } m_CompLocs;

    struct BloomExtractLocs {
        int srcTex = -1; int threshold = -1; int knee = -1;
    } m_BloomExLocs;

    struct BloomBlurLocs {
        int srcTex = -1; int horizontal = -1; int radius = -1; int viewport = -1;
    } m_BloomBlurLocs;

    struct BloomCompositeLocs {
        int srcTex = -1; int bloomTex = -1;
        int intensity = -1; int dirtIntensity = -1;
    } m_BloomCompLocs;

    struct ToneMapOnlyLocs {
        int srcTex = -1;
        int tEnabled = -1; int tOp = -1;
        int tReinhardWhite = -1; int tIntensity = -1;
        int cGamma = -1;
    } m_TMOnlyLocs;
};
