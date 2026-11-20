#include "PostProcessingShader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>
#include <string>

namespace
{
    const char* kFullscreenVS =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "out vec2 vUV;\n"
        "void main()\n"
        "{\n"
        "    vUV = aPos * 0.5 + 0.5;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    const char* kCompositeFS =
        "#version 330 core\n"
        "\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "\n"
        "uniform sampler2D u_SrcTex;\n"
        "uniform sampler2D u_BloomTex;\n"
        "uniform sampler2D u_LUTTex;\n"
        "uniform sampler2D u_DepthTex;\n"
        "uniform sampler2D u_TAAHistoryColor;\n"
        "uniform sampler2D u_TAAHistoryDepth;\n"
        "uniform vec2 u_Viewport;\n"
        "uniform float u_Time;\n"
        "uniform float u_ShadowIntensity;\n"
        "\n"
        "// ---- Global Lighting / Legacy filter ----\n"
        "uniform int   u_G_Enabled;\n"
        "uniform float u_G_GlobalIntensity;\n"
        "uniform vec3  u_G_AmbientColor;\n"
        "uniform float u_G_AmbientIntensity;\n"
        "uniform float u_G_SkyLightIntensity;\n"
        "uniform float u_G_SceneExposure;\n"
        "uniform float u_G_Brightness;\n"
        "uniform float u_G_Contrast;\n"
        "uniform float u_G_Saturation;\n"
        "uniform float u_G_ExposureEV;\n"
        "uniform vec3  u_G_ColorTint;\n"
        "\n"
        "uniform int   u_G_FogEnabled;\n"
        "uniform vec3  u_G_FogColor;\n"
        "uniform float u_G_FogDensity;\n"
        "uniform float u_G_FogStart;\n"
        "uniform float u_G_FogEnd;\n"
        "uniform int u_G_FogMode;\n"
        "uniform mat4 u_FogInverseProjection;\n"
        "uniform mat4 u_FogInverseView;\n"
        "uniform mat4 u_FogProjection;\n"
        "uniform vec3 u_FogCameraPosition;\n"
        "uniform int u_FogOnlyPass;\n"
        "uniform int u_SSAOEnabled;\n"
        "uniform float u_SSAOIntensity;\n"
        "uniform float u_SSAORadius;\n"
        "uniform int u_SSR_Enabled;\n"
        "uniform float u_SSR_Strength;\n"
        "uniform float u_SSR_Distance;\n"
        "uniform int u_TAAEnabled;\n"
        "uniform float u_TAAHistoryWeight;\n"
        "uniform int u_TAAHistoryValid;\n"
        "uniform vec2 u_TAAHistoryUVOffset;\n"
        "uniform int u_AdvancedEffectQuality;\n"
        "\n"
        "// ---- Color Grading ----\n"
        "uniform float u_C_Exposure;\n"
        "uniform float u_C_Contrast;\n"
        "uniform float u_C_Saturation;\n"
        "uniform float u_C_Temperature;\n"
        "uniform float u_C_Tint;\n"
        "uniform float u_C_Brightness;\n"
        "uniform float u_C_Gamma;\n"
        "uniform vec3  u_C_Gain;\n"
        "uniform vec3  u_C_Lift;\n"
        "uniform vec3  u_C_PresetGamma;\n"
        "uniform vec3  u_C_Shadows;   uniform float u_C_ShadowsW;\n"
        "uniform vec3  u_C_Midtones;  uniform float u_C_MidtonesW;\n"
        "uniform vec3  u_C_Highlights;uniform float u_C_HighlightsW;\n"
        "uniform vec3  u_C_BalS;\n"
        "uniform vec3  u_C_BalM;\n"
        "uniform vec3  u_C_BalH;\n"
        "\n"
        "// ---- Tone Mapping ----\n"
        "uniform int   u_T_Enabled;\n"
        "uniform int   u_T_Op;\n"
        "uniform float u_T_ReinhardWhite;\n"
        "uniform float u_T_Intensity;\n"
        "\n"
        "// ---- LUT ----\n"
        "uniform int   u_L_Enabled;\n"
        "uniform float u_L_Intensity;\n"
        "\n"
        "// ---- Bloom composite ----\n"
        "uniform int   u_B_Enabled;\n"
        "uniform float u_B_Intensity;\n"
        "uniform float u_B_DirtIntensity;\n"
        "\n"
        "// ---- Vignette ----\n"
        "uniform int   u_V_Enabled;\n"
        "uniform float u_V_Intensity;\n"
        "uniform float u_V_Radius;\n"
        "uniform float u_V_Smoothness;\n"
        "uniform float u_V_Feather;\n"
        "uniform vec3  u_V_Color;\n"
        "\n"
        "// ---- Chromatic aberration ----\n"
        "uniform int   u_CH_Enabled;\n"
        "uniform float u_CH_Strength;\n"
        "uniform float u_CH_EdgeBias;\n"
        "\n"
        "// ---- Sharpen ----\n"
        "uniform int   u_S_Enabled;\n"
        "uniform float u_S_Strength;\n"
        "uniform float u_S_Clamp;\n"
        "\n"
        "// ---- Lens Distortion ----\n"
        "uniform int   u_LD_Enabled;\n"
        "uniform float u_LD_K1;\n"
        "uniform float u_LD_K2;\n"
        "uniform float u_LD_Cubic;\n"
        "\n"
        "// ---- Film Grain ----\n"
        "uniform int   u_FG_Enabled;\n"
        "uniform float u_FG_Intensity;\n"
        "uniform float u_FG_Size;\n"
        "uniform float u_FG_Speed;\n"
        "uniform float u_FG_ShadowW;\n"
        "uniform float u_FG_MidW;\n"
        "uniform float u_FG_HighW;\n"
        "\n"
        "// ---- DoF ----\n"
        "uniform int   u_D_Enabled;\n"
        "uniform float u_D_FocalDist;\n"
        "uniform float u_D_FocalRange;\n"
        "uniform float u_D_Aperture;\n"
        "uniform float u_D_MaxBlur;\n"
        "\n"
        "// ---- Motion Blur ----\n"
        "uniform int   u_MB_Enabled;\n"
        "uniform float u_MB_Intensity;\n"
        "uniform int   u_MB_Samples;\n"
        "\n"
        "// ---- God Rays (Raios Solares) ----\n"
        "uniform int   u_GodRaysEnabled;\n"
        "uniform vec2  u_GodRaysSunScreenPos;\n"
        "uniform float u_GodRaysIntensity;\n"
        "uniform float u_GodRaysDensity;\n"
        "uniform float u_GodRaysDecay;\n"
        "uniform float u_GodRaysWeight;\n"
        "uniform float u_GodRaysSunInFront;\n"
        "uniform vec3  u_GodRaysTint;\n"
        "uniform float u_GodRaysMieG;\n"
        "uniform float u_GodRaysHaloRadius;\n"
        "uniform float u_GodRaysHaloIntensity;\n"
        "uniform float u_GodRaysSunAltitude;\n"
        "\n"
        "// ---- Glare Rays (Raios de Brilho) ----\n"
        "uniform int   u_GlareEnabled;\n"
        "uniform float u_GlareIntensity;\n"
        "uniform float u_GlareLength;\n"
        "uniform float u_GlareThreshold;\n"
        "\n"
        "float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "\n"
        "// Limiar com joelho suave: mantem o brilho continuo em vez de cortar seco.\n"
        "vec3 SoftBrightPass(vec3 c, float threshold, float knee)\n"
        "{\n"
        "    float lum = max(max(c.r, c.g), c.b);\n"
        "    float soft = clamp((lum - threshold + knee) / max(2.0 * knee, 1e-4), 0.0, 1.0);\n"
        "    float weight = clamp(max(soft * soft * knee, lum - threshold) / max(lum, 1e-4), 0.0, 1.0);\n"
        "    return c * weight;\n"
        "}\n"
        "\n"
        "vec3 TemperatureTint(vec3 c, float temp, float tin)\n"
        "{\n"
        "    temp = clamp(temp, -1.0, 1.0);\n"
        "    tin  = clamp(tin,  -1.0, 1.0);\n"
        "    vec3 warm = vec3(1.0 + temp * 0.10, 1.0, 1.0 - temp * 0.10);\n"
        "    c *= warm;\n"
        "    c.r += tin * 0.05;\n"
        "    c.b -= tin * 0.05;\n"
        "    return c;\n"
        "}\n"
        "\n"
        "vec3 ApplyShaping(vec3 c)\n"
        "{\n"
        "    float l = Luminance(c);\n"
        "    vec3 shadows   = mix(vec3(1.0), u_C_Shadows,   smoothstep(0.0, u_C_ShadowsW,   l));\n"
        "    float midMask  = 1.0 - abs(l - u_C_MidtonesW) / max(u_C_MidtonesW + 1e-4, 1.0 - u_C_MidtonesW);\n"
        "    midMask = clamp(midMask, 0.0, 1.0);\n"
        "    vec3 midtones  = mix(vec3(1.0), u_C_Midtones,  midMask);\n"
        "    vec3 highlights= mix(vec3(1.0), u_C_Highlights,smoothstep(u_C_HighlightsW, 1.0, l));\n"
        "    vec3 shaped = c * shadows * midtones * highlights;\n"
        "    shaped += u_C_BalS * (1.0 - smoothstep(0.0, u_C_ShadowsW, l));\n"
        "    shaped += u_C_BalM * midMask;\n"
        "    shaped += u_C_BalH * smoothstep(u_C_HighlightsW, 1.0, l);\n"
        "    return shaped;\n"
        "}\n"
        "\n"
        "vec3 ApplyLUT(vec3 c)\n"
        "{\n"
        "    if (u_L_Enabled == 0) return c;\n"
        "    ivec2 ts = textureSize(u_LUTTex, 0);\n"
        "    int size   = max(1, ts.y);\n"
        "    int stride = max(1, ts.x / size);\n"
        "    vec3 clamped = clamp(c, 0.0, 1.0);\n"
        "    float sliceSizeF = 1.0 / float(stride);\n"
        "    float invSizeF   = 1.0 / float(size);\n"
        "    float zF = clamped.b * float(size - 1);\n"
        "    int   z0 = int(floor(zF));\n"
        "    int   z1 = min(z0 + 1, size - 1);\n"
        "    float zT = fract(zF);\n"
        "    vec2 uv0 = vec2(\n"
        "        (float(z0 % stride) + clamped.r) * sliceSizeF,\n"
        "        (float(z0 / stride) + clamped.g) * invSizeF\n"
        "    );\n"
        "    vec2 uv1 = vec2(\n"
        "        (float(z1 % stride) + clamped.r) * sliceSizeF,\n"
        "        (float(z1 / stride) + clamped.g) * invSizeF\n"
        "    );\n"
        "    vec3 c0 = texture(u_LUTTex, uv0).rgb;\n"
        "    vec3 c1 = texture(u_LUTTex, uv1).rgb;\n"
        "    vec3 lutColor = mix(c0, c1, zT);\n"
        "    return mix(c, lutColor, clamp(u_L_Intensity, 0.0, 1.0));\n"
        "}\n"
        "\n"
        "vec3 ToneMap(vec3 c)\n"
        "{\n"
        "    if (u_T_Enabled == 0) return c;\n"
        "    float inten = max(u_T_Intensity, 0.0001);\n"
        "    c *= inten;\n"
        "    switch (u_T_Op)\n"
        "    {\n"
        "        case 1:\n"
        "        {\n"
        "            float wp = max(u_T_ReinhardWhite * u_T_ReinhardWhite, 0.0001);\n"
        "            c = (c * (1.0 + c / wp)) / (1.0 + c);\n"
        "            break;\n"
        "        }\n"
        "        case 2:\n"
        "        {\n"
        "            vec3 x = max(c - 0.004, vec3(0.0));\n"
        "            c = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);\n"
        "            break;\n"
        "        }\n"
        "        case 3:\n"
        "        {\n"
        "            c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);\n"
        "            break;\n"
        "        }\n"
        "        case 4:\n"
        "        {\n"
        "            vec3 AgXIn = max(c, vec3(0.0));\n"
        "            mat3 AgXInMat = mat3(\n"
        "                 0.8566271543,  0.0951212405,  0.0482516052,\n"
        "                 0.1373189729,  0.7612419903,  0.1014390368,\n"
        "                 0.1118982129,  0.0767994084,  0.8113023787);\n"
        "            mat3 AgXOutMat = mat3(\n"
        "                 1.1968790051, -0.0528968585, -0.5096016591,\n"
        "                -0.0980208811,  1.1519031299, -0.0489974382,\n"
        "                -0.0027431337,  0.0000212837,  0.9165801058);\n"
        "            vec3 col = AgXInMat * AgXIn;\n"
        "            col = max(col, vec3(0.0));\n"
        "            col = log2(col + 1.0) / 3.0;\n"
        "            col = AgXOutMat * col;\n"
        "            c = clamp(col, 0.0, 1.0);\n"
        "            break;\n"
        "        }\n"
        "        default: break;\n"
        "    }\n"
        "    return clamp(c, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "vec3 SampleSource(vec2 uv) {\n"
        "    uv = clamp(uv, vec2(0.0), vec2(1.0));\n"
        "    return texture(u_SrcTex, uv).rgb;\n"
        "}\n"
        "\n"
        "vec3 DistortUV(inout vec2 uv)\n"
        "{\n"
        "    if (u_LD_Enabled == 0) return SampleSource(uv);\n"
        "    vec2 centered = uv * 2.0 - 1.0;\n"
        "    float r2 = dot(centered, centered);\n"
        "    float r4 = r2 * r2;\n"
        "    float distort = 1.0 + u_LD_K1 * r2 + u_LD_K2 * r4 + u_LD_Cubic * r2 * sqrt(r2);\n"
        "    vec2 newUV = (centered * distort) * 0.5 + 0.5;\n"
        "    if (any(lessThan(newUV, vec2(0.0))) || any(greaterThan(newUV, vec2(1.0))))\n"
        "        return vec3(0.0);\n"
        "    uv = newUV;\n"
        "    return SampleSource(uv);\n"
        "}\n"
        "\n"
        "vec3 Chromatic(vec2 uv)\n"
        "{\n"
        "    if (u_CH_Enabled == 0) return SampleSource(uv);\n"
        "    vec2 centered = uv * 2.0 - 1.0;\n"
        "    float edge = pow(length(centered), max(u_CH_EdgeBias, 0.0));\n"
        "    float strength = u_CH_Strength * edge;\n"
        "    vec2 dir = normalize(centered + vec2(1e-5));\n"
        "    vec2 channelOffset = dir * (strength * 30.0) / max(u_Viewport, vec2(1.0));\n"
        "    float r = SampleSource(uv - channelOffset).r;\n"
        "    float g = SampleSource(uv).g;\n"
        "    float b = SampleSource(uv + channelOffset).b;\n"
        "    return vec3(r, g, b);\n"
        "}\n"
        "\n"
        "vec3 ApplyVolumetricFog(vec3 sceneColor, vec2 uv)\n"
        "{\n"
        "    if (u_G_FogEnabled == 0 || (u_G_FogMode != 0 && u_G_FogDensity <= 0.0)) return sceneColor;\n"
        "    float depth = texture(u_DepthTex, uv).r;\n"
        "    vec2 ndcXY = uv * 2.0 - 1.0;\n"
        "    vec3 rayDirection;\n"
        "    float rayDistance;\n"
        "    if (depth >= 0.999999)\n"
        "    {\n"
        "        vec4 farPoint = u_FogInverseProjection * vec4(ndcXY, 1.0, 1.0);\n"
        "        vec3 viewDirection = normalize(farPoint.xyz / max(abs(farPoint.w), 1e-6));\n"
        "        rayDirection = normalize((u_FogInverseView * vec4(viewDirection, 0.0)).xyz);\n"
        "        float vertical = clamp(abs(rayDirection.y), 0.0, 1.0);\n"
        "        rayDistance = mix(u_G_FogEnd, u_G_FogStart, smoothstep(0.0, 0.85, vertical));\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        vec4 viewPoint = u_FogInverseProjection * vec4(ndcXY, depth * 2.0 - 1.0, 1.0);\n"
        "        viewPoint /= (abs(viewPoint.w) > 1e-6 ? viewPoint.w : 1e-6);\n"
        "        vec3 worldPoint = (u_FogInverseView * viewPoint).xyz;\n"
        "        vec3 cameraToPoint = worldPoint - u_FogCameraPosition;\n"
        "        rayDistance = length(cameraToPoint);\n"
        "        rayDirection = cameraToPoint / max(rayDistance, 1e-6);\n"
        "    }\n"
        "\n"
        "    float fogStart = max(u_G_FogStart, 0.0);\n"
        "    float fogEnd = max(u_G_FogEnd, fogStart + 1e-3);\n"
        "    float mediumLength = clamp(min(rayDistance, fogEnd) - fogStart, 0.0, fogEnd - fogStart);\n"
        "    float opticalDepth = max(u_G_FogDensity, 0.0) * mediumLength;\n"
        "    float transmittance;\n"
        "    if (u_G_FogMode == 0)\n"
        "        transmittance = 1.0 - clamp((rayDistance - fogStart) / max(fogEnd - fogStart, 1e-3), 0.0, 1.0);\n"
        "    else if (u_G_FogMode == 2)\n"
        "        transmittance = exp(-min(opticalDepth * opticalDepth, 80.0));\n"
        "    else\n"
        "        transmittance = exp(-min(opticalDepth, 80.0));\n"
        "    return mix(u_G_FogColor, sceneColor, transmittance);\n"
        "}\n"
        "\n"
        "vec3 ReconstructViewPosition(vec2 uv, float depth)\n"
        "{\n"
        "    vec4 p = u_FogInverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
        "    return p.xyz / (abs(p.w) > 1e-6 ? p.w : 1e-6);\n"
        "}\n"
        "\n"
        "float Hash(vec2 p);\n"
        "\n"
        "float ApplyScreenSpaceAO(vec2 uv, float depth)\n"
        "{\n"
        "    if (u_SSAOEnabled == 0) return 1.0;\n"
        "    vec3 p = ReconstructViewPosition(uv, min(depth, 0.9999));\n"
        "    vec3 n = normalize(cross(dFdx(p), dFdy(p)));\n"
        "    if (depth >= 0.99999) return 1.0;\n"
        "    if (dot(n, -p) < 0.0) n = -n;\n"
        "    float radius = clamp(u_SSAORadius, 0.1, 5.0);\n"
        "    float pixels = clamp(radius / max(abs(p.z), 1.0) * u_Viewport.y * 0.25, 1.0, 14.0);\n"
        "    vec2 stepUV = pixels / max(u_Viewport, vec2(1.0));\n"
        "    float rotation = Hash(gl_FragCoord.xy) * 6.2831853;\n"
        "    float occlusion = 0.0;\n"
        "    float validSamples = 0.0;\n"
        "    int sampleCount = 4 + clamp(u_AdvancedEffectQuality, 0, 2) * 2;\n"
        "    for (int i = 0; i < 8; ++i)\n"
        "    {\n"
        "        if (i >= sampleCount) break;\n"
        "        float angle = rotation + (float(i) + 0.5) * 0.78539816;\n"
        "        float radial = (i < 4) ? 0.62 : 1.0;\n"
        "        vec2 sampleUV = uv + vec2(cos(angle), sin(angle)) * stepUV * radial;\n"
        "        if (any(lessThan(sampleUV, vec2(0.001))) || any(greaterThan(sampleUV, vec2(0.999)))) continue;\n"
        "        float sampleDepth = texture(u_DepthTex, sampleUV).r;\n"
        "        if (sampleDepth >= 0.99999) continue;\n"
        "        vec3 q = ReconstructViewPosition(sampleUV, sampleDepth);\n"
        "        vec3 delta = q - p;\n"
        "        float distanceToSample = length(delta);\n"
        "        float hemisphere = max(dot(n, delta), 0.0);\n"
        "        float rangeWeight = 1.0 - smoothstep(radius * 0.25, radius, distanceToSample);\n"
        "        occlusion += step(0.015, hemisphere) * rangeWeight;\n"
        "        validSamples += 1.0;\n"
        "    }\n"
        "    float ao = occlusion / max(validSamples, 1.0);\n"
        "    return clamp(1.0 - ao * clamp(u_SSAOIntensity, 0.0, 1.0) * 0.72, 0.28, 1.0);\n"
        "}\n"
        "\n"
        "vec3 ApplyScreenSpaceReflections(vec3 sceneColor, vec2 uv, float depth)\n"
        "{\n"
        "    if (u_SSR_Enabled == 0 || u_SSR_Strength <= 0.001) return sceneColor;\n"
        "    vec3 p = ReconstructViewPosition(uv, min(depth, 0.9999));\n"
        "    vec3 n = normalize(cross(dFdx(p), dFdy(p)));\n"
        "    if (depth >= 0.99999) return sceneColor;\n"
        "    if (dot(n, -p) < 0.0) n = -n;\n"
        "    vec3 rayDirection = normalize(reflect(normalize(p), n));\n"
        "    float maxDistance = clamp(u_SSR_Distance, 1.0, 100.0);\n"
        "    int maxSteps = 8 + clamp(u_AdvancedEffectQuality, 0, 3) * 4;\n"
        "    float stepLength = maxDistance / float(maxSteps);\n"
        "    vec3 rayPosition = p + n * 0.035;\n"
        "    for (int i = 0; i < 20; ++i)\n"
        "    {\n"
        "        if (i >= maxSteps) break;\n"
        "        rayPosition += rayDirection * stepLength;\n"
        "        vec4 clip = u_FogProjection * vec4(rayPosition, 1.0);\n"
        "        if (clip.w <= 1e-5) break;\n"
        "        vec2 hitUV = clip.xy / clip.w * 0.5 + 0.5;\n"
        "        if (any(lessThan(hitUV, vec2(0.002))) || any(greaterThan(hitUV, vec2(0.998)))) break;\n"
        "        float hitDepth = texture(u_DepthTex, hitUV).r;\n"
        "        if (hitDepth >= 0.99999) continue;\n"
        "        vec3 scenePosition = ReconstructViewPosition(hitUV, hitDepth);\n"
        "        float separation = scenePosition.z - rayPosition.z;\n"
        "        float thickness = max(0.035, abs(rayPosition.z) * 0.008);\n"
        "        if (separation >= 0.0 && separation < thickness)\n"
        "        {\n"
        "            float edgeFade = smoothstep(0.0, 0.12, min(min(hitUV.x, hitUV.y), min(1.0 - hitUV.x, 1.0 - hitUV.y)));\n"
        "            float distanceFade = 1.0 - smoothstep(maxDistance * 0.65, maxDistance, length(rayPosition - p));\n"
        "            float confidence = edgeFade * distanceFade * clamp(1.0 - separation / thickness, 0.0, 1.0);\n"
        "            vec3 reflected = texture(u_SrcTex, hitUV).rgb;\n"
        "            return mix(sceneColor, reflected, clamp(u_SSR_Strength * confidence, 0.0, 0.6));\n"
        "        }\n"
        "    }\n"
        "    return sceneColor;\n"
        "}\n"
        "\n"
        "vec3 ApplyTemporalAA(vec3 currentColor, vec2 uv, float depth)\n"
        "{\n"
        "    if (u_TAAEnabled == 0 || u_TAAHistoryValid == 0 || depth >= 0.99999) return currentColor;\n"
        "    vec2 historyUV = uv + u_TAAHistoryUVOffset;\n"
        "    if (any(lessThan(historyUV, vec2(0.001))) || any(greaterThan(historyUV, vec2(0.999)))) return currentColor;\n"
        "    float oldDepth = texture(u_TAAHistoryDepth, historyUV).r;\n"
        "    if (oldDepth >= 0.99999 || abs(oldDepth - depth) > max(0.00035, depth * 0.008)) return currentColor;\n"
        "    vec2 texel = 1.0 / max(u_Viewport, vec2(1.0));\n"
        "    vec3 lo = currentColor;\n"
        "    vec3 hi = currentColor;\n"
        "    vec3 north = texture(u_SrcTex, uv + vec2(0.0, texel.y)).rgb;\n"
        "    vec3 south = texture(u_SrcTex, uv - vec2(0.0, texel.y)).rgb;\n"
        "    vec3 east = texture(u_SrcTex, uv + vec2(texel.x, 0.0)).rgb;\n"
        "    vec3 west = texture(u_SrcTex, uv - vec2(texel.x, 0.0)).rgb;\n"
        "    lo = min(lo, min(min(north, south), min(east, west)));\n"
        "    hi = max(hi, max(max(north, south), max(east, west)));\n"
        "    vec3 history = texture(u_TAAHistoryColor, historyUV).rgb;\n"
        "    history = clamp(history, lo - vec3(0.025), hi + vec3(0.025));\n"
        "    return mix(currentColor, history, clamp(u_TAAHistoryWeight, 0.0, 0.25));\n"
        "}\n"
        "\n"
        "vec3 ApplySceneEffects(vec3 color, vec2 uv)\n"
        "{\n"
        "    float depth = texture(u_DepthTex, uv).r;\n"
        "    color *= ApplyScreenSpaceAO(uv, depth);\n"
        "    color = ApplyScreenSpaceReflections(color, uv, depth);\n"
        "    color = ApplyVolumetricFog(color, uv);\n"
        "    color = ApplyTemporalAA(color, uv, depth);\n"
        "    return color;\n"
        "}\n"
        "\n"
        "float Hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
        "\n"
        "float InterleavedGradientNoise(vec2 pos, float time)\n"
        "{\n"
        "    vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);\n"
        "    return fract(m.z * fract(dot(pos, m.xy)) + fract(time));\n"
        "}\n"
        "\n"
        "vec3 Sharpen(vec3 c, vec2 uv, vec2 texel)\n"
        "{\n"
        "    if (u_S_Enabled == 0) return c;\n"
        "    vec3 n = SampleSource(uv + vec2(0, texel.y));\n"
        "    vec3 s = SampleSource(uv - vec2(0, texel.y));\n"
        "    vec3 e = SampleSource(uv + vec2(texel.x, 0));\n"
        "    vec3 w = SampleSource(uv - vec2(texel.x, 0));\n"
        "    vec3 blurred = (n + s + e + w) * 0.25;\n"
        "    vec3 diff = c - blurred;\n"
        "    diff = clamp(diff, vec3(-u_S_Clamp), vec3(u_S_Clamp));\n"
        "    return clamp(c + diff * u_S_Strength, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "void main()\n"
        "{\n"
        "    if (u_FogOnlyPass != 0)\n"
        "    {\n"
        "        vec3 sceneEffects = ApplySceneEffects(SampleSource(vUV), vUV);\n"
        "        FragColor = vec4(sceneEffects, 1.0);\n"
        "        return;\n"
        "    }\n"
        "    vec2 uv = vUV;\n"
        "    vec2 texel = 1.0 / max(u_Viewport, vec2(1.0));\n"
        "\n"
        "    // 1. Lens distortion + chromatic aberration (sample source)\n"
        "    vec2 preUV = uv;\n"
        "    vec3 col = DistortUV(preUV);\n"
        "    if (u_CH_Enabled != 0) col = Chromatic(preUV);\n"
        "    else col = SampleSource(preUV);\n"
        "\n"
        "    // Fog volumes are integrated once from the camera ray and the scene depth,\n"
        "    // so geometry and the procedural sky share exactly the same extinction model.\n"
        "    col = ApplySceneEffects(col, preUV);\n"
        "\n"
        "    // 2. DoF aproximation based on depth\n"
        "    if (u_D_Enabled != 0)\n"
        "    {\n"
        "        float depth = texture(u_DepthTex, preUV).r;\n"
        "        float linearDepth = depth;\n"
        "        float coc = abs(linearDepth - u_D_FocalDist) / max(u_D_FocalRange, 0.001);\n"
        "        coc = clamp(coc, 0.0, 1.0) * u_D_MaxBlur;\n"
        "        if (coc > 0.01)\n"
        "        {\n"
        "            vec3 blurred = vec3(0.0);\n"
        "            float total = 0.0;\n"
        "            const int taps = 13;\n"
        "            float ang = Hash(gl_FragCoord.xy) * 6.2831;\n"
        "            for (int i = 0; i < taps; ++i)\n"
        "            {\n"
        "                float a = ang + float(i) * (6.2831 / float(taps));\n"
        "                vec2 off = vec2(cos(a), sin(a)) * coc * float(i) / float(taps);\n"
        "                blurred += SampleSource(preUV + off * texel * 800.0);\n"
        "                total += 1.0;\n"
        "            }\n"
        "            col = mix(col, blurred / max(total, 1.0), clamp(coc * 2.0, 0.0, 1.0));\n"
        "        }\n"
        "    }\n"
        "\n"
        "    // 3. Motion Blur aproximation\n"
        "    if (u_MB_Enabled != 0 && u_MB_Intensity > 0.0)\n"
        "    {\n"
        "        vec2 dir = (uv - 0.5) * u_MB_Intensity * 0.02;\n"
        "        vec3 acc = col;\n"
        "        int samples = max(1, min(u_MB_Samples, 8));\n"
        "        float totalW = 1.0;\n"
        "        for (int i = 1; i < samples; ++i)\n"
        "        {\n"
        "            float t = float(i) / float(samples);\n"
        "            acc += SampleSource(preUV - dir * t);\n"
        "            totalW += 1.0;\n"
        "        }\n"
        "        col = acc / totalW;\n"
        "    }\n"
        "\n"
        "    // 4. Bloom additive composite\n"
        "    if (u_B_Enabled != 0 && u_B_Intensity > 0.0)\n"
        "    {\n"
        "        vec3 bloom = texture(u_BloomTex, preUV).rgb;\n"
        "        float dirt = 1.0;\n"
        "        col += bloom * u_B_Intensity * (1.0 + u_B_DirtIntensity * dirt);\n"
        "    }\n"
        "\n"
        "    // 4b. Raios Solares Fisicamente Baseados (Mie Scattering + Oclusao + Halo Solar)\n"
        "    // Implementacao de qualidade AAA baseada em radial ray marching com:\n"
        "    // - Espalhamento de Mie (phase function de Henyey-Greenstein) para cor dependente do angulo\n"
        "    // - Halo solar com corona difusa\n"
        "    // - Cor aquecida por altitude do sol (entardecer/amanhecer)\n"
        "    // - Oclusao fisica por depth buffer com jitter temporal IGN\n"
        "    // - Mascara de ceu adaptativa sem degraus\n"
        "    if (u_GodRaysEnabled != 0 && u_GodRaysIntensity > 0.001 && u_GodRaysSunInFront > 0.0)\n"
        "    {\n"
        "        vec2 toSun = u_GodRaysSunScreenPos - preUV;\n"
        "        float distToSun = length(toSun);\n"
        "\n"
        "        // Henyey-Greenstein phase function (Mie scattering)\n"
        "        // g > 0 = forward scattering (feixes mais fortes quando olhando para o sol)\n"
        "        float mieG = clamp(u_GodRaysMieG, 0.0, 0.98);\n"
        "        float cosTheta = distToSun > 0.0001 ? dot(normalize(toSun), vec2(0.0, 1.0)) : 1.0;\n"
        "        float g2 = mieG * mieG;\n"
        "        float miePhase = (1.0 - g2) / (4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * mieG * cosTheta, 1.5));\n"
        "        // Normaliza para [0, 1] usando valor de pico (theta=0, forward)\n"
        "        float mieForward = (1.0 - g2) / (4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * mieG, 1.5));\n"
        "        float mieWeight = clamp(miePhase / max(mieForward, 0.0001), 0.0, 2.0);\n"
        "\n"
        "        // Cor baseada na altitude do sol (amanhecer/entardecer = laranja, zenite = branco)\n"
        "        // u_GodRaysSunAltitude vai de 0.0 (horizonte) ate 1.0 (zenite)\n"
        "        float altFactor = clamp(u_GodRaysSunAltitude, 0.0, 1.0);\n"
        "        // Sunrise/sunset: laranja quente; zenite: dourado -> branco\n"
        "        vec3 sunsetTint  = vec3(1.0, 0.38, 0.08);  // laranja intenso do horizonte\n"
        "        vec3 middayTint  = vec3(1.0, 0.95, 0.80);  // branco dourado do zenite\n"
        "        vec3 altitudeTint = mix(sunsetTint, middayTint, smoothstep(0.0, 0.35, altFactor));\n"
        "        // O tint do usuario multiplica por cima\n"
        "        vec3 finalRayTint = u_GodRaysTint * altitudeTint;\n"
        "\n"
        "        // Ray marching com 48 amostras e jitter temporal (Interleaved Gradient Noise)\n"
        "        const int kRaySteps = 48;\n"
        "        vec2 stepUV = toSun * (u_GodRaysDensity / float(kRaySteps));\n"
        "        // IGN dither com variacao temporal para suavizar banding sem TAA\n"
        "        vec3 ign = vec3(0.06711056, 0.00583715, 52.9829189);\n"
        "        float dither = fract(ign.z * fract(dot(gl_FragCoord.xy + mod(u_Time * 7.3, 100.0), ign.xy)));\n"
        "        vec2 curUV = preUV + stepUV * dither;\n"
        "        float illumDecay = 1.0;\n"
        "        vec3 rayAccum = vec3(0.0);\n"
        "\n"
        "        for (int r = 0; r < kRaySteps; ++r)\n"
        "        {\n"
        "            curUV += stepUV;\n"
        "            vec2 clamped = clamp(curUV, vec2(0.001), vec2(0.999));\n"
        "            if (curUV != clamped) break;\n"
        "\n"
        "            float depthSample = texture(u_DepthTex, curUV).r;\n"
        "            vec3 colorSample  = SampleSource(curUV);\n"
        "            float lum = Luminance(colorSample);\n"
        "\n"
        "            // Mascara de fonte de luz:\n"
        "            // 1) Ceu completamente aberto (depth ~1.0) = fonte plena\n"
        "            // 2) Pixels HDR super-brilhantes (emissivos, sol) tambem contribuem\n"
        "            // Transicao suave para evitar aliasing na borda de silhuetas\n"
        "            float skyMask    = smoothstep(0.998, 0.9999, depthSample);\n"
        "            float emissiveMask = smoothstep(0.85, 1.4, lum) * (1.0 - skyMask);\n"
        "            float sourceMask = max(skyMask, emissiveMask * 0.6);\n"
        "\n"
        "            // A cor do raio sai do ceu amostrado * tint de altitude\n"
        "            vec3 rayColor = mix(colorSample, colorSample * finalRayTint, skyMask);\n"
        "\n"
        "            // Atenuacao exponencial: decai com distancia percorrida\n"
        "            rayAccum += rayColor * sourceMask * illumDecay * u_GodRaysWeight;\n"
        "            illumDecay *= u_GodRaysDecay;\n"
        "        }\n"
        "\n"
        "        // Aplica scattering de Mie (forward scattering deixa o halo mais forte)\n"
        "        vec3 godRaysColor = rayAccum * mieWeight;\n"
        "\n"
        "        // Mascara: sem feixe exatamente em cima do disco solar (evita espigao)\n"
        "        // Cresce suavemente ao se afastar, plateau longe\n"
        "        float innerMask = smoothstep(0.0, 0.08, distToSun);\n"
        "        float outerFade = 1.0 - smoothstep(0.85, 1.4, distToSun);\n"
        "\n"
        "        col += godRaysColor * u_GodRaysIntensity * innerMask * outerFade;\n"
        "\n"
        "        // Halo Solar: disco de corona difusa ao redor do sol\n"
        "        if (u_GodRaysHaloIntensity > 0.001)\n"
        "        {\n"
        "            float haloRadius = max(u_GodRaysHaloRadius, 0.01);\n"
        "            // Corona difusa: exponencial larga + anel estreito\n"
        "            float corona = exp(-distToSun * distToSun / (haloRadius * haloRadius * 0.5));\n"
        "            float ring   = exp(-pow((distToSun - haloRadius * 0.6) / (haloRadius * 0.2), 2.0));\n"
        "            float halo = corona * 0.7 + ring * 0.3;\n"
        "            // Halo so aparece no ceu (depth max)\n"
        "            float skyDepth = texture(u_DepthTex, preUV).r;\n"
        "            float skyFactor = smoothstep(0.998, 0.9999, skyDepth);\n"
        "            col += finalRayTint * halo * u_GodRaysHaloIntensity * skyFactor;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    // 4c. Raios de Brilho / Glare (Luzes Artificiais e Neon via Estrias Direcionais)\n"
        "    if (u_GlareEnabled != 0 && u_GlareIntensity > 0.001)\n"
        "    {\n"
        "        vec3 glareAccum = vec3(0.0);\n"
        "        // 6 direcoes formam uma estrela mais cheia que as 4 originais.\n"
        "        const vec2 kGlareDirs[6] = vec2[](\n"
        "            vec2( 1.0,  0.0),\n"
        "            vec2( 0.0,  1.0),\n"
        "            vec2( 0.7071,  0.7071),\n"
        "            vec2(-0.7071,  0.7071),\n"
        "            vec2( 0.9239,  0.3827),\n"
        "            vec2(-0.3827,  0.9239)\n"
        "        );\n"
        "        float stepLen = u_GlareLength * 4.0;\n"
        "        const int kGlareSteps = 12;\n"
        "        // Joelho suave no limiar: evita o corte seco que gera degraus nas estrias.\n"
        "        float knee = max(u_GlareThreshold * 0.5, 0.001);\n"
        "        for (int gd = 0; gd < 6; ++gd)\n"
        "        {\n"
        "            vec2 dir = kGlareDirs[gd] * texel * stepLen;\n"
        "            for (int gs = 1; gs <= kGlareSteps; ++gs)\n"
        "            {\n"
        "                float weight = 1.0 / (float(gs) * float(gs));\n"
        "                vec2 offset = dir * float(gs);\n"
        "                vec3 b1 = SoftBrightPass(SampleSource(preUV + offset), u_GlareThreshold, knee);\n"
        "                vec3 b2 = SoftBrightPass(SampleSource(preUV - offset), u_GlareThreshold, knee);\n"
        "                glareAccum += (b1 + b2) * weight;\n"
        "            }\n"
        "        }\n"
        "        // Normaliza pela contagem de direcoes (eram 4, agora 6).\n"
        "        col += glareAccum * u_GlareIntensity * (4.0 / 6.0);\n"
        "    }\n"
        "\n"
        "    // 5. Global Lighting exposure / legacy tint\n"
        "    if (u_G_Enabled != 0)\n"
        "    {\n"
        "        col *= u_G_Brightness * u_G_GlobalIntensity;\n"
        "        col *= exp2(u_G_SceneExposure + u_G_ExposureEV);\n"
        "        col *= u_G_ColorTint;\n"
        "        float l = Luminance(col);\n"
        "        col = mix(vec3(l), col, u_G_Saturation);\n"
        "        col = (col - 0.5) * u_G_Contrast + 0.5;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        col *= exp2(u_G_SceneExposure);\n"
        "    }\n"
        "\n"
        "    // 6. Color Grading order: exposure, gain/lift, brightness, shaping, temp/tint, contrast, saturation, gamma\n"
        "    col *= exp2(u_C_Exposure);\n"
        "    col  = col * u_C_Gain + u_C_Lift;\n"
        "    col *= u_C_Brightness;\n"
        "    col  = ApplyShaping(col);\n"
        "    col  = TemperatureTint(col, u_C_Temperature, u_C_Tint);\n"
        "    {\n"
        "        float l = Luminance(col);\n"
        "        col = mix(vec3(l), col, u_C_Saturation);\n"
        "        col = (col - 0.5) * u_C_Contrast + 0.5;\n"
        "    }\n"
        "    col = vec3(\n"
        "        pow(max(col.r, 0.0), 1.0 / max(u_C_Gamma, 0.01)),\n"
        "        pow(max(col.g, 0.0), 1.0 / max(u_C_Gamma, 0.01)),\n"
        "        pow(max(col.b, 0.0), 1.0 / max(u_C_Gamma, 0.01))\n"
        "    );\n"
        "    col = vec3(\n"
        "        pow(max(col.r, 0.0), 1.0 / max(u_C_PresetGamma.r, 0.01)),\n"
        "        pow(max(col.g, 0.0), 1.0 / max(u_C_PresetGamma.g, 0.01)),\n"
        "        pow(max(col.b, 0.0), 1.0 / max(u_C_PresetGamma.b, 0.01))\n"
        "    );\n"
        "\n"
        "    // 7. LUT 3D\n"
        "    col = ApplyLUT(clamp(col, 0.0, 1.0));\n"
        "\n"
        "    // 8. Tone Mapping HDR -> LDR\n"
        "    col = ToneMap(col);\n"
        "\n"
        "    // 9. Vignette LDR\n"
        "    if (u_V_Enabled != 0)\n"
        "    {\n"
        "        vec2 cv = preUV * 2.0 - 1.0;\n"
        "        cv.x *= u_Viewport.x / max(u_Viewport.y, 1.0);\n"
        "        float d = length(cv);\n"
        "        float edge = smoothstep(\n"
        "            u_V_Radius + u_V_Feather,\n"
        "            u_V_Radius + u_V_Feather + max(u_V_Smoothness, 0.001),\n"
        "            d\n"
        "        );\n"
        "        col = mix(col, u_V_Color, clamp(edge * u_V_Intensity, 0.0, 1.0));\n"
        "    }\n"
        "\n"
        "    // 10. Film Grain (post-tonemap, luminosity-based)\n"
        "    if (u_FG_Enabled != 0 && u_FG_Intensity > 0.0)\n"
        "    {\n"
        "        vec2 grainUV = gl_FragCoord.xy * max(u_FG_Size, 0.01) + u_Time * u_FG_Speed * 30.0;\n"
        "        float n1 = Hash(grainUV);\n"
        "        float n2 = Hash(grainUV + vec2(37.17, 12.91));\n"
        "        float grain = (n1 + n2 - 1.0);\n"
        "        float l = Luminance(col);\n"
        "        float response =\n"
        "            u_FG_ShadowW * (1.0 - smoothstep(0.0, 0.25, l)) +\n"
        "            u_FG_MidW    * smoothstep(0.0, 0.5, l) * (1.0 - smoothstep(0.5, 1.0, l)) +\n"
        "            u_FG_HighW   * smoothstep(0.75, 1.0, l);\n"
        "        col += grain * u_FG_Intensity * 0.18 * response;\n"
        "    }\n"
        "\n"
        "    // 11. Sharpen LDR\n"
        "    col = Sharpen(col, preUV, texel);\n"
        "\n"
        "    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);\n"
        "}\n";

    const char* kBloomExtractFS =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_SrcTex;\n"
        "uniform float u_Threshold;\n"
        "uniform float u_Knee;\n"
        "float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "void main()\n"
        "{\n"
        "    vec3 c = texture(u_SrcTex, vUV).rgb;\n"
        "    float l = Luminance(c);\n"
        "    float knee = max(u_Knee, 0.0001);\n"
        "    float softness = max(u_Threshold - knee, 0.0001);\n"
        "    float contribution = clamp(l - softness, 0.0, knee * 2.0);\n"
        "    contribution = contribution * contribution / (knee * 4.0 + 0.0001);\n"
        "    float mask = max(l - u_Threshold, 0.0);\n"
        "    FragColor = vec4(c * max(mask, contribution) / max(l, 0.0001), 1.0);\n"
        "}\n";

    const char* kBloomBlurFS =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_SrcTex;\n"
        "uniform int   u_Horizontal;\n"
        "uniform float u_Radius;\n"
        "uniform vec2  u_Viewport;\n"
        "void main()\n"
        "{\n"
        "    vec2 texel = 1.0 / max(u_Viewport, vec2(1.0));\n"
        "    vec2 off = (u_Horizontal != 0 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y)) * u_Radius;\n"
        "    vec3 sum = vec3(0.0);\n"
        "    sum += texture(u_SrcTex, vUV - off * 3.0).rgb * 0.06136;\n"
        "    sum += texture(u_SrcTex, vUV - off * 2.0).rgb * 0.24477;\n"
        "    sum += texture(u_SrcTex, vUV - off * 1.0).rgb * 0.38774;\n"
        "    sum += texture(u_SrcTex, vUV).rgb             * 0.38774;\n"
        "    sum += texture(u_SrcTex, vUV + off * 1.0).rgb * 0.38774;\n"
        "    sum += texture(u_SrcTex, vUV + off * 2.0).rgb * 0.24477;\n"
        "    sum += texture(u_SrcTex, vUV + off * 3.0).rgb * 0.06136;\n"
        "    FragColor = vec4(sum * 0.31312, 1.0);\n"
        "}\n";

    const char* kBloomCompositeFS =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_SrcTex;\n"
        "uniform sampler2D u_BloomTex;\n"
        "uniform float u_Intensity;\n"
        "uniform float u_DirtIntensity;\n"
        "void main()\n"
        "{\n"
        "    vec3 src   = texture(u_SrcTex, vUV).rgb;\n"
        "    vec3 bloom = texture(u_BloomTex, vUV).rgb;\n"
        "    float dirt = 1.0;\n"
        "    FragColor = vec4(src + bloom * u_Intensity * (1.0 + u_DirtIntensity * dirt), 1.0);\n"
        "}\n";

    const char* kToneMapOnlyFS =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_SrcTex;\n"
        "uniform int   u_T_Enabled;\n"
        "uniform int   u_T_Op;\n"
        "uniform float u_T_ReinhardWhite;\n"
        "uniform float u_T_Intensity;\n"
        "uniform float u_C_Gamma;\n"
        "void main()\n"
        "{\n"
        "    vec3 c = texture(u_SrcTex, vUV).rgb;\n"
        "    if (u_T_Enabled != 0) {\n"
        "        float inten = max(u_T_Intensity, 0.0001);\n"
        "        c *= inten;\n"
        "        if (u_T_Op == 1) {\n"
        "            float wp = max(u_T_ReinhardWhite * u_T_ReinhardWhite, 0.0001);\n"
        "            c = (c * (1.0 + c / wp)) / (1.0 + c);\n"
        "        } else if (u_T_Op == 2) {\n"
        "            vec3 x = max(c - 0.004, vec3(0.0));\n"
        "            c = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);\n"
        "        } else if (u_T_Op == 3) {\n"
        "            c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);\n"
        "        } else if (u_T_Op == 4) {\n"
        "            mat3 AgXInMat = mat3(\n"
        "                 0.8566271543,  0.0951212405,  0.0482516052,\n"
        "                 0.1373189729,  0.7612419903,  0.1014390368,\n"
        "                 0.1118982129,  0.0767994084,  0.8113023787);\n"
        "            mat3 AgXOutMat = mat3(\n"
        "                 1.1968790051, -0.0528968585, -0.5096016591,\n"
        "                -0.0980208811,  1.1519031299, -0.0489974382,\n"
        "                -0.0027431337,  0.0000212837,  0.9165801058);\n"
        "            vec3 col = AgXInMat * max(c, vec3(0.0));\n"
        "            col = log2(col + 1.0) / 3.0;\n"
        "            c = clamp(AgXOutMat * col, 0.0, 1.0);\n"
        "        }\n"
        "    }\n"
        "    c = pow(max(c, vec3(0.0)), vec3(1.0 / max(u_C_Gamma, 0.01)));\n"
        "    FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
        "}\n";

    bool CheckStage(unsigned int sh, const char* label)
    {
        GLint ok = GL_FALSE;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (ok == GL_TRUE) return true;
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(1, len)), '\0');
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        std::cerr << "[PPShader] Compile " << label << ": " << log << std::endl;
        return false;
    }

    bool CheckProgram(GLuint prog, const char* label)
    {
        GLint ok = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (ok == GL_TRUE) return true;
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(1, len)), '\0');
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        std::cerr << "[PPShader] Link " << label << ": " << log << std::endl;
        return false;
    }
}

PostProcessingShader::PostProcessingShader() = default;

PostProcessingShader::~PostProcessingShader()
{
    Destroy();
}

bool PostProcessingShader::CompileProgram(GLuint& out, const char* label, const char* vs, const char* fs)
{
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vs, nullptr);
    glCompileShader(v);
    const bool vOK = CheckStage(v, (std::string(label) + ".vs").c_str());

    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fs, nullptr);
    glCompileShader(f);
    const bool fOK = CheckStage(f, (std::string(label) + ".fs").c_str());

    out = glCreateProgram();
    glAttachShader(out, v);
    glAttachShader(out, f);
    glLinkProgram(out);
    const bool pOK = CheckProgram(out, label);

    glDeleteShader(v);
    glDeleteShader(f);
    return vOK && fOK && pOK;
}

bool PostProcessingShader::Create()
{
    InvalidateLocations();
    bool ok = true;
    ok &= CompileProgram(m_Programs[(unsigned int)Pass::Composite],       "pp_composite",    kFullscreenVS, kCompositeFS);
    ok &= CompileProgram(m_Programs[(unsigned int)Pass::BloomExtract],    "pp_bloom_extract",kFullscreenVS, kBloomExtractFS);
    ok &= CompileProgram(m_Programs[(unsigned int)Pass::BloomBlur],       "pp_bloom_blur",   kFullscreenVS, kBloomBlurFS);
    ok &= CompileProgram(m_Programs[(unsigned int)Pass::BloomComposite],  "pp_bloom_comp",   kFullscreenVS, kBloomCompositeFS);
    ok &= CompileProgram(m_Programs[(unsigned int)Pass::ToneMapOnly],     "pp_tonemap_only", kFullscreenVS, kToneMapOnlyFS);

    CacheCompositeLocations();
    CacheBloomExtractLocations();
    CacheBloomBlurLocations();
    CacheBloomCompositeLocations();
    CacheToneMapOnlyLocations();
    return ok;
}

void PostProcessingShader::Destroy()
{
    for (auto& p : m_Programs)
    {
        if (p != 0) { glDeleteProgram(p); p = 0; }
    }
    InvalidateLocations();
}

void PostProcessingShader::InvalidateLocations()
{
    m_CompLocs = {};
    m_BloomExLocs = {};
    m_BloomBlurLocs = {};
    m_BloomCompLocs = {};
    m_TMOnlyLocs = {};
}

#define GL(m) glGetUniformLocation(prog, #m)

void PostProcessingShader::CacheCompositeLocations()
{
    GLuint prog = m_Programs[(unsigned int)Pass::Composite];
    if (prog == 0) return;
    glUseProgram(prog);
    auto& L = m_CompLocs;
    L.srcTex = GL(u_SrcTex); L.bloomTex = GL(u_BloomTex); L.lutTex = GL(u_LUTTex); L.depthTex = GL(u_DepthTex);
    L.viewport = GL(u_Viewport); L.time = GL(u_Time); L.shadowIntensity = GL(u_ShadowIntensity);

    L.gEnabled = GL(u_G_Enabled);
    L.gGlobalIntensity = GL(u_G_GlobalIntensity);
    L.gAmbientColor = GL(u_G_AmbientColor);
    L.gAmbientIntensity = GL(u_G_AmbientIntensity);
    L.gSkyLightIntensity = GL(u_G_SkyLightIntensity);
    L.gSceneExposure = GL(u_G_SceneExposure);
    L.gBrightness = GL(u_G_Brightness);
    L.gContrast = GL(u_G_Contrast);
    L.gSaturation = GL(u_G_Saturation);
    L.gExposureEV = GL(u_G_ExposureEV);
    L.gColorTint = GL(u_G_ColorTint);
    L.gFogEnabled = GL(u_G_FogEnabled);
    L.gFogColor = GL(u_G_FogColor);
    L.gFogDensity = GL(u_G_FogDensity);
    L.gFogStart = GL(u_G_FogStart);
    L.gFogEnd = GL(u_G_FogEnd);
    L.gFogMode = GL(u_G_FogMode);
    L.fogInverseProjection = GL(u_FogInverseProjection);
    L.fogInverseView = GL(u_FogInverseView);
    L.fogProjection = GL(u_FogProjection);
    L.fogCameraPosition = GL(u_FogCameraPosition);
    L.fogOnlyPass = GL(u_FogOnlyPass);
    L.ssaoEnabled = GL(u_SSAOEnabled);
    L.ssaoIntensity = GL(u_SSAOIntensity);
    L.ssaoRadius = GL(u_SSAORadius);
    L.ssrEnabled = GL(u_SSR_Enabled);
    L.ssrStrength = GL(u_SSR_Strength);
    L.ssrDistance = GL(u_SSR_Distance);
    L.taaEnabled = GL(u_TAAEnabled);
    L.taaHistoryWeight = GL(u_TAAHistoryWeight);
    L.taaHistoryValid = GL(u_TAAHistoryValid);
    L.taaHistoryUVOffset = GL(u_TAAHistoryUVOffset);
    L.advancedEffectQuality = GL(u_AdvancedEffectQuality);

    L.cExposure = GL(u_C_Exposure); L.cContrast = GL(u_C_Contrast); L.cSaturation = GL(u_C_Saturation);
    L.cTemperature = GL(u_C_Temperature); L.cTint = GL(u_C_Tint); L.cBrightness = GL(u_C_Brightness); L.cGamma = GL(u_C_Gamma);
    L.cGain = GL(u_C_Gain); L.cLift = GL(u_C_Lift); L.cPresetGamma = GL(u_C_PresetGamma);
    L.cShadows = GL(u_C_Shadows); L.cShadowsW = GL(u_C_ShadowsW);
    L.cMidtones = GL(u_C_Midtones); L.cMidtonesW = GL(u_C_MidtonesW);
    L.cHighlights = GL(u_C_Highlights); L.cHighlightsW = GL(u_C_HighlightsW);
    L.cBalS = GL(u_C_BalS); L.cBalM = GL(u_C_BalM); L.cBalH = GL(u_C_BalH);

    L.tEnabled = GL(u_T_Enabled); L.tOp = GL(u_T_Op);
    L.tReinhardWhite = GL(u_T_ReinhardWhite); L.tIntensity = GL(u_T_Intensity);

    L.lEnabled = GL(u_L_Enabled); L.lIntensity = GL(u_L_Intensity);

    L.bEnabled = GL(u_B_Enabled); L.bIntensity = GL(u_B_Intensity); L.bDirtIntensity = GL(u_B_DirtIntensity);

    L.vEnabled = GL(u_V_Enabled); L.vIntensity = GL(u_V_Intensity); L.vRadius = GL(u_V_Radius);
    L.vSmoothness = GL(u_V_Smoothness); L.vFeather = GL(u_V_Feather); L.vColor = GL(u_V_Color);

    L.chEnabled = GL(u_CH_Enabled); L.chStrength = GL(u_CH_Strength); L.chEdgeBias = GL(u_CH_EdgeBias);

    L.sEnabled = GL(u_S_Enabled); L.sStrength = GL(u_S_Strength); L.sClamp = GL(u_S_Clamp);

    L.ldEnabled = GL(u_LD_Enabled); L.ldK1 = GL(u_LD_K1); L.ldK2 = GL(u_LD_K2); L.ldCubic = GL(u_LD_Cubic);

    L.fgEnabled = GL(u_FG_Enabled); L.fgIntensity = GL(u_FG_Intensity); L.fgSize = GL(u_FG_Size); L.fgSpeed = GL(u_FG_Speed);
    L.fgShadowW = GL(u_FG_ShadowW); L.fgMidW = GL(u_FG_MidW); L.fgHighW = GL(u_FG_HighW);

    L.dEnabled = GL(u_D_Enabled); L.dFocalDist = GL(u_D_FocalDist); L.dFocalRange = GL(u_D_FocalRange);
    L.dAperture = GL(u_D_Aperture); L.dMaxBlur = GL(u_D_MaxBlur);

    L.mbEnabled = GL(u_MB_Enabled); L.mbIntensity = GL(u_MB_Intensity); L.mbSamples = GL(u_MB_Samples);

    L.godRaysEnabled = GL(u_GodRaysEnabled);
    L.godRaysSunScreenPos = GL(u_GodRaysSunScreenPos);
    L.godRaysIntensity = GL(u_GodRaysIntensity);
    L.godRaysDensity = GL(u_GodRaysDensity);
    L.godRaysDecay = GL(u_GodRaysDecay);
    L.godRaysWeight = GL(u_GodRaysWeight);
    L.godRaysSunInFront = GL(u_GodRaysSunInFront);
    L.godRaysTint = GL(u_GodRaysTint);
    L.godRaysMieG = GL(u_GodRaysMieG);
    L.godRaysHaloRadius = GL(u_GodRaysHaloRadius);
    L.godRaysHaloIntensity = GL(u_GodRaysHaloIntensity);
    L.godRaysSunAltitude = GL(u_GodRaysSunAltitude);
    L.glareEnabled = GL(u_GlareEnabled);
    L.glareIntensity = GL(u_GlareIntensity);
    L.glareLength = GL(u_GlareLength);
    L.glareThreshold = GL(u_GlareThreshold);

    if (L.srcTex >= 0) glUniform1i(L.srcTex, 0);
    if (L.bloomTex >= 0) glUniform1i(L.bloomTex, 1);
    if (L.lutTex >= 0) glUniform1i(L.lutTex, 2);
    if (L.depthTex >= 0) glUniform1i(L.depthTex, 3);
    const int taaColor = GL(u_TAAHistoryColor);
    const int taaDepth = GL(u_TAAHistoryDepth);
    if (taaColor >= 0) glUniform1i(taaColor, 4);
    if (taaDepth >= 0) glUniform1i(taaDepth, 5);
}

void PostProcessingShader::CacheBloomExtractLocations()
{
    GLuint prog = m_Programs[(unsigned int)Pass::BloomExtract];
    if (prog == 0) return;
    glUseProgram(prog);
    auto& L = m_BloomExLocs;
    L.srcTex = GL(u_SrcTex);
    L.threshold = GL(u_Threshold);
    L.knee = GL(u_Knee);
    if (L.srcTex >= 0) glUniform1i(L.srcTex, 0);
}

void PostProcessingShader::CacheBloomBlurLocations()
{
    GLuint prog = m_Programs[(unsigned int)Pass::BloomBlur];
    if (prog == 0) return;
    glUseProgram(prog);
    auto& L = m_BloomBlurLocs;
    L.srcTex = GL(u_SrcTex); L.horizontal = GL(u_Horizontal); L.radius = GL(u_Radius); L.viewport = GL(u_Viewport);
    if (L.srcTex >= 0) glUniform1i(L.srcTex, 0);
}

void PostProcessingShader::CacheBloomCompositeLocations()
{
    GLuint prog = m_Programs[(unsigned int)Pass::BloomComposite];
    if (prog == 0) return;
    glUseProgram(prog);
    auto& L = m_BloomCompLocs;
    L.srcTex = GL(u_SrcTex); L.bloomTex = GL(u_BloomTex);
    L.intensity = GL(u_Intensity); L.dirtIntensity = GL(u_DirtIntensity);
    if (L.srcTex >= 0) glUniform1i(L.srcTex, 0);
    if (L.bloomTex >= 0) glUniform1i(L.bloomTex, 1);
}

void PostProcessingShader::CacheToneMapOnlyLocations()
{
    GLuint prog = m_Programs[(unsigned int)Pass::ToneMapOnly];
    if (prog == 0) return;
    glUseProgram(prog);
    auto& L = m_TMOnlyLocs;
    L.srcTex = GL(u_SrcTex);
    L.tEnabled = GL(u_T_Enabled); L.tOp = GL(u_T_Op);
    L.tReinhardWhite = GL(u_T_ReinhardWhite); L.tIntensity = GL(u_T_Intensity);
    L.cGamma = GL(u_C_Gamma);
    if (L.srcTex >= 0) glUniform1i(L.srcTex, 0);
}

#undef GL

void PostProcessingShader::UseCompositePass()
{
    auto p = m_Programs[(unsigned int)Pass::Composite];
    if (p) glUseProgram(p);
}
void PostProcessingShader::UseBloomExtractPass()
{
    auto p = m_Programs[(unsigned int)Pass::BloomExtract];
    if (p) glUseProgram(p);
}
void PostProcessingShader::UseBloomBlurPass(bool horizontal)
{
    auto p = m_Programs[(unsigned int)Pass::BloomBlur];
    if (p)
    {
        glUseProgram(p);
        if (m_BloomBlurLocs.horizontal >= 0) glUniform1i(m_BloomBlurLocs.horizontal, horizontal ? 1 : 0);
    }
}
void PostProcessingShader::UseBloomCompositePass()
{
    auto p = m_Programs[(unsigned int)Pass::BloomComposite];
    if (p) glUseProgram(p);
}
void PostProcessingShader::UseToneMapOnlyPass()
{
    auto p = m_Programs[(unsigned int)Pass::ToneMapOnly];
    if (p) glUseProgram(p);
}

void PostProcessingShader::SetSourceTexture(int unit) { (void)unit; }
void PostProcessingShader::SetBloomTexture(int unit)  { (void)unit; }
void PostProcessingShader::SetLUTTexture(int unit)    { (void)unit; }
void PostProcessingShader::SetDepthTexture(int unit)  { (void)unit; }

void PostProcessingShader::SetViewportSize(float w, float h)
{
    if (m_CompLocs.viewport >= 0)  glUniform2f(m_CompLocs.viewport, w, h);
    if (m_BloomBlurLocs.viewport >= 0) glUniform2f(m_BloomBlurLocs.viewport, w, h);
}
void PostProcessingShader::SetElapsedTime(float t)
{
    if (m_CompLocs.time >= 0) glUniform1f(m_CompLocs.time, t);
}

void PostProcessingShader::SetFogCamera(const glm::mat4& inverseProjection,
                                        const glm::mat4& inverseView,
                                        const glm::mat4& projection,
                                        const glm::vec3& cameraPosition)
{
    if (m_CompLocs.fogInverseProjection >= 0)
        glUniformMatrix4fv(m_CompLocs.fogInverseProjection, 1, GL_FALSE, &inverseProjection[0][0]);
    if (m_CompLocs.fogInverseView >= 0)
        glUniformMatrix4fv(m_CompLocs.fogInverseView, 1, GL_FALSE, &inverseView[0][0]);
    if (m_CompLocs.fogProjection >= 0)
        glUniformMatrix4fv(m_CompLocs.fogProjection, 1, GL_FALSE, &projection[0][0]);
    if (m_CompLocs.fogCameraPosition >= 0)
        glUniform3f(m_CompLocs.fogCameraPosition, cameraPosition.x, cameraPosition.y, cameraPosition.z);
}

void PostProcessingShader::SetShadowIntensity(float v)
{
    if (m_CompLocs.shadowIntensity >= 0) glUniform1f(m_CompLocs.shadowIntensity, v);
}

void PostProcessingShader::SetFogOnlyPass(bool enabled)
{
    if (m_CompLocs.fogOnlyPass >= 0)
        glUniform1i(m_CompLocs.fogOnlyPass, enabled ? 1 : 0);
}

void PostProcessingShader::SetTemporalHistoryValid(bool valid)
{
    if (m_CompLocs.taaHistoryValid >= 0)
        glUniform1i(m_CompLocs.taaHistoryValid, valid ? 1 : 0);
}

void PostProcessingShader::SetAdvancedEffectQuality(int qualityLevel)
{
    if (m_CompLocs.advancedEffectQuality >= 0)
        glUniform1i(m_CompLocs.advancedEffectQuality, std::clamp(qualityLevel, 0, 3));
}

void PostProcessingShader::SetTemporalHistoryUVOffset(const glm::vec2& offset)
{
    if (m_CompLocs.taaHistoryUVOffset >= 0)
        glUniform2f(m_CompLocs.taaHistoryUVOffset, offset.x, offset.y);
}

void PostProcessingShader::SetGodRaysSunScreen(const glm::vec2& sunScreenPos, bool sunInFront)
{
    auto& L = m_CompLocs;
    if (L.godRaysSunScreenPos >= 0)
        glUniform2f(L.godRaysSunScreenPos, sunScreenPos.x, sunScreenPos.y);
    if (L.godRaysSunInFront >= 0)
        glUniform1f(L.godRaysSunInFront, sunInFront ? 1.0f : 0.0f);
}

#define U1F(loc, v) if (loc >= 0) glUniform1f(loc, v)
#define U1I(loc, v) if (loc >= 0) glUniform1i(loc, v)
#define U3F(loc, v) if (loc >= 0) glUniform3f(loc, v.X, v.Y, v.Z)

void PostProcessingShader::SetGlobalLighting(const GlobalLightingSettings& g)
{
    auto& L = m_CompLocs;
    U1I(L.gEnabled, g.Enabled ? 1 : 0);
    U1F(L.gGlobalIntensity, g.GlobalIntensity);
    U3F(L.gAmbientColor, g.AmbientColor);
    U1F(L.gAmbientIntensity, g.AmbientIntensity);
    U1F(L.gSkyLightIntensity, g.SkyLightIntensity);
    U1F(L.gSceneExposure, g.SceneExposure);
    U1F(L.gBrightness, g.Brightness);
    U1F(L.gContrast, g.LegacyContrast);
    U1F(L.gSaturation, g.LegacySaturation);
    U1F(L.gExposureEV, g.LegacyExposureEV);
    U3F(L.gColorTint, g.ColorTint);
    U1I(L.gFogEnabled, g.FogEnabled ? 1 : 0);
    U3F(L.gFogColor, g.FogColor);
    U1F(L.gFogDensity, g.FogDensity);
    U1F(L.gFogStart, g.FogStart);
    U1F(L.gFogEnd, g.FogEnd);
    U1I(L.gFogMode, std::clamp(g.FogMode, 0, 2));
    U1I(L.ssaoEnabled, g.ScreenSpaceAOEnabled ? 1 : 0);
    U1F(L.ssaoIntensity, std::clamp(g.ScreenSpaceAOIntensity, 0.0f, 1.0f));
    U1F(L.ssaoRadius, std::clamp(g.ScreenSpaceAORadius, 0.1f, 5.0f));
    U1I(L.ssrEnabled, g.ScreenSpaceReflectionsEnabled ? 1 : 0);
    U1F(L.ssrStrength, std::clamp(g.ScreenSpaceReflectionsStrength, 0.0f, 0.6f));
    U1F(L.ssrDistance, std::clamp(g.ScreenSpaceReflectionsDistance, 1.0f, 100.0f));
    U1I(L.taaEnabled, g.TemporalAAEnabled ? 1 : 0);
    U1F(L.taaHistoryWeight, std::clamp(g.TemporalAAHistoryWeight, 0.0f, 0.25f));
    U1I(L.godRaysEnabled, g.GodRaysEnabled ? 1 : 0);
    U1F(L.godRaysIntensity, std::clamp(g.GodRaysIntensity, 0.0f, 3.0f));
    U1F(L.godRaysDensity, std::clamp(g.GodRaysDensity, 0.05f, 2.0f));
    U1F(L.godRaysDecay, std::clamp(g.GodRaysDecay, 0.5f, 0.99f));
    U1F(L.godRaysWeight, std::clamp(g.GodRaysWeight, 0.05f, 1.0f));
    if (L.godRaysTint >= 0) glUniform3f(L.godRaysTint, g.GodRaysTint.X, g.GodRaysTint.Y, g.GodRaysTint.Z);
    U1F(L.godRaysMieG, std::clamp(g.GodRaysMieG, 0.0f, 0.98f));
    U1F(L.godRaysHaloRadius, std::clamp(g.GodRaysHaloRadius, 0.01f, 1.0f));
    U1F(L.godRaysHaloIntensity, std::clamp(g.GodRaysHaloIntensity, 0.0f, 5.0f));
    U1F(L.godRaysSunAltitude, std::clamp(g.GodRaysSunAltitude, 0.0f, 1.0f));
    U1I(L.glareEnabled, g.GlareRaysEnabled ? 1 : 0);
    U1F(L.glareIntensity, std::clamp(g.GlareRaysIntensity, 0.0f, 3.0f));
    U1F(L.glareLength, std::clamp(g.GlareRaysLength, 0.05f, 2.0f));
    U1F(L.glareThreshold, std::clamp(g.GlareRaysThreshold, 0.5f, 5.0f));
}

void PostProcessingShader::SetColorGrading(const ColorGradingSettings& c)
{
    auto& L = m_CompLocs;
    U1F(L.cExposure, c.Exposure);
    U1F(L.cContrast, c.Contrast);
    U1F(L.cSaturation, c.Saturation);
    U1F(L.cTemperature, c.Temperature);
    U1F(L.cTint, c.Tint);
    U1F(L.cBrightness, c.Brightness);
    U1F(L.cGamma, c.Gamma);
    U3F(L.cGain, c.Gain);
    U3F(L.cLift, c.Lift);
    U3F(L.cPresetGamma, c.PresetGamma);
    U3F(L.cShadows, c.Shadows);    U1F(L.cShadowsW, c.ShadowsWeight);
    U3F(L.cMidtones, c.Midtones);  U1F(L.cMidtonesW, c.MidtonesWeight);
    U3F(L.cHighlights, c.Highlights); U1F(L.cHighlightsW, c.HighlightsWeight);
    U3F(L.cBalS, c.BalanceShadows);
    U3F(L.cBalM, c.BalanceMidtones);
    U3F(L.cBalH, c.BalanceHighlights);

    if (m_TMOnlyLocs.cGamma >= 0) glUniform1f(m_TMOnlyLocs.cGamma, c.Gamma);
}

void PostProcessingShader::SetToneMapping(const ToneMappingSettings& t)
{
    auto& L = m_CompLocs;
    U1I(L.tEnabled, t.Enabled ? 1 : 0);
    U1I(L.tOp, static_cast<int>(t.Op));
    U1F(L.tReinhardWhite, t.ReinhardWhitePoint);
    U1F(L.tIntensity, t.Intensity);

    auto& T = m_TMOnlyLocs;
    U1I(T.tEnabled, t.Enabled ? 1 : 0);
    U1I(T.tOp, static_cast<int>(t.Op));
    U1F(T.tReinhardWhite, t.ReinhardWhitePoint);
    U1F(T.tIntensity, t.Intensity);
}

void PostProcessingShader::SetLUT(const LUTSettings& l)
{
    auto& L = m_CompLocs;
    U1I(L.lEnabled, l.Enabled ? 1 : 0);
    U1F(L.lIntensity, l.Intensity);
}

void PostProcessingShader::SetBloom(const BloomSettings& b)
{
    auto& L = m_CompLocs;
    U1I(L.bEnabled, b.Enabled ? 1 : 0);
    U1F(L.bIntensity, b.Intensity);
    U1F(L.bDirtIntensity, b.DirtIntensity);

    auto& BE = m_BloomExLocs;
    U1F(BE.threshold, b.Threshold);
    U1F(BE.knee, b.Knee);

    auto& BC = m_BloomCompLocs;
    U1F(BC.intensity, b.Intensity);
    U1F(BC.dirtIntensity, b.DirtIntensity);

    if (m_BloomBlurLocs.radius >= 0) glUniform1f(m_BloomBlurLocs.radius, b.Radius);
}

void PostProcessingShader::SetVignette(const VignetteSettings& v)
{
    auto& L = m_CompLocs;
    U1I(L.vEnabled, v.Enabled ? 1 : 0);
    U1F(L.vIntensity, v.Intensity);
    U1F(L.vRadius, v.Radius);
    U1F(L.vSmoothness, v.Smoothness);
    U1F(L.vFeather, v.Feather);
    U3F(L.vColor, v.Color);
}

void PostProcessingShader::SetChromatic(const ChromaticAberrationSettings& c)
{
    auto& L = m_CompLocs;
    U1I(L.chEnabled, c.Enabled ? 1 : 0);
    U1F(L.chStrength, c.Strength);
    U1F(L.chEdgeBias, c.EdgeBias);
}

void PostProcessingShader::SetSharpen(const SharpenSettings& s)
{
    auto& L = m_CompLocs;
    U1I(L.sEnabled, s.Enabled ? 1 : 0);
    U1F(L.sStrength, s.Strength);
    U1F(L.sClamp, s.Clamp);
}

void PostProcessingShader::SetLensDistort(const LensDistortionSettings& l)
{
    auto& L = m_CompLocs;
    U1I(L.ldEnabled, l.Enabled ? 1 : 0);
    U1F(L.ldK1, l.K1);
    U1F(L.ldK2, l.K2);
    U1F(L.ldCubic, l.Cubic);
}

void PostProcessingShader::SetFilmGrain(const FilmGrainSettings& f)
{
    auto& L = m_CompLocs;
    U1I(L.fgEnabled, f.Enabled ? 1 : 0);
    U1F(L.fgIntensity, f.Intensity);
    U1F(L.fgSize, f.GrainSize);
    U1F(L.fgSpeed, f.Speed);
    U1F(L.fgShadowW, f.ShadowWeight);
    U1F(L.fgMidW, f.MidWeight);
    U1F(L.fgHighW, f.HighWeight);
}

void PostProcessingShader::SetDoF(const DoFSettings& d)
{
    auto& L = m_CompLocs;
    U1I(L.dEnabled, d.Enabled ? 1 : 0);
    U1F(L.dFocalDist, d.FocalDistance);
    U1F(L.dFocalRange, d.FocalRange);
    U1F(L.dAperture, d.Aperture);
    U1F(L.dMaxBlur, d.MaxBlur);
}

void PostProcessingShader::SetMotionBlur(const MotionBlurSettings& m)
{
    auto& L = m_CompLocs;
    U1I(L.mbEnabled, m.Enabled ? 1 : 0);
    U1F(L.mbIntensity, m.Intensity);
    U1I(L.mbSamples, m.Samples);
}

#undef U1F
#undef U1I
#undef U3F
