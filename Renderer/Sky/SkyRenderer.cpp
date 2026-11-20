#include "SkyRenderer.h"
#include <GLFW/glfw3.h>
#include "../../Engine/Lighting/LightingSystem.h"
#include <algorithm>
#include <iostream>

SkyRenderer::SkyRenderer()
{
}

SkyRenderer::~SkyRenderer()
{
    if (m_ProgramID != 0)
    {
        glDeleteProgram(m_ProgramID);
        m_ProgramID = 0;
    }
    if (m_VAO != 0)
    {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO != 0)
    {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
}

bool SkyRenderer::Init()
{
    // Verifica se o contexto OpenGL está ativo
    if (!gladLoadGL())
    {
        std::cout << "Erro: GLAD não foi carregado no SkyRenderer!" << std::endl;
        return false;
    }

    const char* vertexShaderSource =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "out vec2 TexCoord;\n"
        "void main()\n"
        "{\n"
        "    TexCoord = aPos * 0.5 + 0.5;\n"
        "    gl_Position = vec4(aPos, 0.9999, 1.0);\n"
        "}\n";

    const char* fragmentShaderSource =
        "#version 330 core\n"
        "in vec2 TexCoord;\n"
        "out vec4 FragColor;\n"
        "\n"
        "uniform mat4 u_InvView;\n"
        "uniform mat4 u_InvProjection;\n"
        "uniform vec3 u_SunDirection;\n"
        "uniform vec3 u_SunColor;\n"
        "uniform float u_SunIntensity;\n"
        "uniform vec3 u_MoonDirection;\n"
        "uniform vec3 u_MoonColor;\n"
        "uniform float u_MoonIntensity;\n"
        "uniform float u_SkyAmbientFactor;\n"
        "uniform float u_Time;\n"
        "uniform int u_CloudOctaves;\n"
        "uniform vec3 u_SkyZenithColor;\n"
        "uniform vec3 u_SkyHorizonColor;\n"
        "uniform vec3 u_SkyGroundColor;\n"
        "uniform int u_CloudsEnabled;\n"
        "uniform vec3 u_CloudColor;\n"
        "uniform float u_CloudAmount;\n"
        "uniform int u_CloudMovementEnabled;\n"
        "uniform float u_CloudSpeed;\n"
        "uniform int u_CloudVolumeEnabled;\n"
        "uniform float u_CloudVolumeBaseHeight;\n"
        "uniform float u_CloudVolumeThickness;\n"
        "uniform float u_CloudVolumeDensity;\n"
        "uniform float u_CloudVolumeScale;\n"
        "uniform int u_CloudVolumeSteps;\n"
        "uniform int u_GlobalLightEnabled;\n"
        "uniform float u_GlobalBrightness;\n"
        "uniform float u_GlobalContrast;\n"
        "uniform float u_GlobalSaturation;\n"
        "uniform float u_GlobalExposure;\n"
        "uniform vec3 u_GlobalTint;\n"
        "uniform float u_GlobalGamma;\n"
        "uniform float u_GlobalVignetteStrength;\n"
        "uniform float u_GlobalVignetteRadius;\n"
        "uniform vec2 u_ViewportSize;\n"
        "uniform int u_FogEnabled;\n"
        "uniform vec3 u_FogColor;\n"
        "uniform float u_FogDensity;\n"
        "uniform float u_FogStart;\n"
        "uniform float u_FogEnd;\n"
        "\n"
        "float hash(vec2 p)\n"
        "{\n"
        "    p = fract(p * vec2(123.34, 456.21));\n"
        "    p += dot(p, p + 45.32);\n"
        "    return fract(p.x * p.y);\n"
        "}\n"
        "\n"
        "float noise(vec2 p)\n"
        "{\n"
        "    vec2 i = floor(p);\n"
        "    vec2 f = fract(p);\n"
        "    f = f * f * (3.0 - 2.0 * f);\n"
        "    float a = hash(i);\n"
        "    float b = hash(i + vec2(1.0, 0.0));\n"
        "    float c = hash(i + vec2(0.0, 1.0));\n"
        "    float d = hash(i + vec2(1.0, 1.0));\n"
        "    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
        "}\n"
        "\n"
        "float fbm(vec2 p)\n"
        "{\n"
        "    float val = 0.0;\n"
        "    float amp = 0.5;\n"
        "    mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);\n"
        "    for (int i = 0; i < 5; ++i)\n"
        "    {\n"
        "        if (i >= u_CloudOctaves) break;\n"
        "        val += amp * noise(p);\n"
        "        p = rot * p * 2.02 + vec2(0.1, 0.2);\n"
        "        amp *= 0.5;\n"
        "    }\n"
        "    return val;\n"
        "}\n"
        "\n"
        "void main()\n"
        "{\n"
        "    vec4 clip = vec4(TexCoord * 2.0 - 1.0, 1.0, 1.0);\n"
        "    vec4 viewSpace = u_InvProjection * clip;\n"
        "    viewSpace /= viewSpace.w;\n"
        "    vec3 rayDir = normalize((u_InvView * vec4(viewSpace.xyz, 0.0)).xyz);\n"
        "\n"
        "    vec3 lightDir = normalize(u_SunDirection);\n"
        "    vec3 sunDir = -lightDir;\n"
        "    if (length(sunDir) < 0.001) sunDir = vec3(0.0, 1.0, 0.0);\n"
        "\n"
        "    vec3 zenithColor = u_SkyZenithColor;\n"
        "    vec3 horizonColor = u_SkyHorizonColor;\n"
        "    vec3 groundColor = u_SkyGroundColor;\n"
        "\n"
        "    float h = rayDir.y;\n"
        "    vec3 skyBase;\n"
        "    if (h >= 0.0)\n"
        "    {\n"
        "        skyBase = mix(horizonColor, zenithColor, pow(h, 0.55));\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        skyBase = mix(horizonColor, groundColor, pow(clamp(-h * 4.0, 0.0, 1.0), 0.7));\n"
        "    }\n"
        "\n"
        "    float cosTheta = dot(rayDir, sunDir);\n"
        "    float sunAltitude = clamp(sunDir.y, -0.25, 1.0);\n"
        "    // Extincao atmosferica simplificada: perto do horizonte o raio atravessa\n"
        "    // mais ar, entao o disco fica mais quente e escuro (nascer/por do sol).\n"
        "    float airMass = 1.0 / max(0.08, sunAltitude + 0.15);\n"
        "    float extinction = clamp(1.0 / (1.0 + airMass * 0.30), 0.0, 1.0);\n"
        "    vec3 sunDiscTint = mix(u_SunColor * vec3(1.25, 0.55, 0.25), u_SunColor,\n"
        "                           extinction * extinction);\n"
        "\n"
        "    // Disco solar com borda suavizada e escurecimento de limbo: o centro e\n"
        "    // mais quente que a borda, como no sol real, em vez de um bloco chapado.\n"
        "    const float kSunAngularRadius = 0.0400;\n"
        "    float sunAngle = acos(clamp(cosTheta, -1.0, 1.0));\n"
        "    float discMask = 1.0 - smoothstep(kSunAngularRadius * 0.90, kSunAngularRadius, sunAngle);\n"
        "    float rNorm = clamp(sunAngle / kSunAngularRadius, 0.0, 1.0);\n"
        "    float limbDarkening = mix(1.0, 0.42, pow(rNorm, 1.8));\n"
        "    float sunCore = discMask * limbDarkening;\n"
        "    float sunCorona = pow(max(0.0, cosTheta), 260.0) * 1.30;\n"
        "    float sunHalo   = pow(max(0.0, cosTheta), 14.0) * 0.40;\n"
        "    float sunWide   = pow(max(0.0, cosTheta), 3.0) * 0.16;\n"
        "    float sunGlow   = sunCorona + sunHalo + sunWide;\n"
        "\n"
        "    vec3 sunTint = sunDiscTint * u_SunIntensity;\n"
        "    vec3 sunContribution = sunTint * (sunCore * 6.5 + sunGlow * 1.6);\n"
        "\n"
        "    vec3 moonDir = -normalize(u_MoonDirection);\n"
        "    if (length(moonDir) < 0.001) moonDir = vec3(0.0, 1.0, 0.0);\n"
        "    float moonCos = dot(rayDir, moonDir);\n"
        "    float moonCore = smoothstep(0.9990, 0.99975, moonCos);\n"
        "    float moonHalo = pow(max(0.0, moonCos), 48.0) * 0.12;\n"
        "    vec3 moonContribution = u_MoonColor * u_MoonIntensity * (moonCore + moonHalo);\n"
        "\n"
        "    float horizonSunFactor = max(0.0, dot(vec3(rayDir.x, 0.0, rayDir.z), vec3(sunDir.x, 0.0, sunDir.z)));\n"
        "    skyBase += sunTint * pow(horizonSunFactor, 4.0) * 0.18 * max(0.0, 1.0 - abs(h) * 2.0);\n"
        "\n"
        "    vec3 finalSky = skyBase * u_SkyAmbientFactor + sunContribution + moonContribution;\n"
        "\n"
        "    if (u_CloudsEnabled != 0 && h > 0.02)\n"
        "    {\n"
        "        vec2 wind = u_CloudMovementEnabled != 0\n"
        "            ? vec2(u_Time * 0.012, u_Time * 0.006) * clamp(u_CloudSpeed, 0.0, 10.0)\n"
        "            : vec2(0.0);\n"
        "        float amount = clamp(u_CloudAmount, 0.0, 1.0);\n"
        "        if (u_CloudVolumeEnabled != 0)\n"
        "        {\n"
        "            // Camada volumétrica: percorre um intervalo de alturas no mundo e\n"
        "            // integra densidade ao longo do raio da câmera (OpenGL 3.3).\n"
        "            vec3 cameraPosition = u_InvView[3].xyz;\n"
        "            float layerBottom = max(0.0, u_CloudVolumeBaseHeight);\n"
        "            float layerThickness = max(1.0, u_CloudVolumeThickness);\n"
        "            float layerTop = layerBottom + layerThickness;\n"
        "            float dy = max(rayDir.y, 1e-4);\n"
        "            float t0 = (layerBottom - cameraPosition.y) / dy;\n"
        "            float t1 = (layerTop - cameraPosition.y) / dy;\n"
        "            float tStart = max(0.0, min(t0, t1));\n"
        "            float tEnd = min(15000.0, max(t0, t1));\n"
        "            if (cameraPosition.y >= layerBottom && cameraPosition.y <= layerTop)\n"
        "                tStart = 0.0;\n"
        "            if (tEnd > tStart + 0.5)\n"
        "            {\n"
        "                int steps = clamp(u_CloudVolumeSteps, 4, 16);\n"
        "                float segmentLength = (tEnd - tStart) / float(steps);\n"
        "                float transmittance = 1.0;\n"
        "                vec3 cloudRadiance = vec3(0.0);\n"
        "                float jitter = hash(gl_FragCoord.xy) * 0.85;\n"
        "                float scale = clamp(u_CloudVolumeScale, 0.25, 4.0) * 0.0007;\n"
        "                for (int i = 0; i < 16; ++i)\n"
        "                {\n"
        "                    if (i >= steps || transmittance < 0.035) break;\n"
        "                    float t = tStart + (float(i) + jitter) * segmentLength;\n"
        "                    vec3 worldPos = cameraPosition + rayDir * t;\n"
        "                    float height01 = clamp((worldPos.y - layerBottom) / layerThickness, 0.0, 1.0);\n"
        "                    vec2 cloudUV = worldPos.xz * scale + wind;\n"
        "                    // A variação de altura desloca as fatias de ruído e evita\n"
        "                    // uma lâmina plana sem o custo de uma textura 3D grande.\n"
        "                    float n = fbm(cloudUV + vec2(height01 * 0.73, -height01 * 0.51));\n"
        "                    float coverage = smoothstep(0.72 - amount * 0.45, 0.89 - amount * 0.36, n);\n"
        "                    float verticalShape = smoothstep(0.02, 0.20, height01) *\n"
        "                        (1.0 - smoothstep(0.62, 0.98, height01));\n"
        "                    float density = coverage * verticalShape * max(u_CloudVolumeDensity, 0.0);\n"
        "                    float sampleAlpha = 1.0 - exp(-density * segmentLength * 0.0018);\n"
        "                    float sunFacing = max(dot(normalize(vec3(rayDir.x, 0.35, rayDir.z)), sunDir), 0.0);\n"
        "                    float forwardScatter = pow(max(dot(rayDir, sunDir), 0.0), 6.0);\n"
        "                    vec3 cloudLight = u_CloudColor * (0.52 + max(sunDir.y, 0.0) * 0.38)\n"
        "                        + u_SunColor * u_SunIntensity * (0.035 + pow(sunFacing, 3.0) * 0.12 + forwardScatter * 0.22);\n"
        "                    cloudRadiance += transmittance * cloudLight * sampleAlpha;\n"
        "                    transmittance *= 1.0 - sampleAlpha;\n"
        "                }\n"
        "                finalSky = cloudRadiance + finalSky * transmittance;\n"
        "            }\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "            float cloudAltitude = 1.0 / (h + 0.12);\n"
        "            vec2 cloudUV = rayDir.xz * cloudAltitude * 0.35 + wind;\n"
        "            float n = fbm(cloudUV);\n"
        "            float cloudDensity = smoothstep(0.70 - amount * 0.40, 0.88 - amount * 0.35, n);\n"
        "            if (cloudDensity > 0.001)\n"
        "            {\n"
        "                float sunFacing = max(0.0, dot(normalize(vec3(rayDir.x, h * 0.5, rayDir.z)), sunDir));\n"
        "                vec3 cloudSunHighlight = vec3(1.0, 0.98, 0.95) * (1.0 + pow(sunFacing, 8.0) * 1.5) * u_SunIntensity;\n"
        "                vec3 cloudShade = mix(u_CloudColor, cloudSunHighlight, cloudDensity);\n"
        "                float distanceHaze = smoothstep(0.02, 0.25, h);\n"
        "                float alpha = cloudDensity * distanceHaze * 0.92;\n"
        "                finalSky = mix(finalSky, cloudShade, alpha);\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "\n"
        "    // O céu é uma superfície no infinito e não recebe a névoa por distância de mesh.\n"
        "    // Aproximamos o percurso atmosférico pela proximidade angular do horizonte.\n"
        "    if (u_FogEnabled != 0)\n"
        "    {\n"
        "        float horizonBand = 1.0 - smoothstep(0.015, 0.22, abs(rayDir.y));\n"
        "        float fogDistance = max(u_FogEnd, u_FogStart);\n"
        "        float fogRange = smoothstep(u_FogStart, max(u_FogEnd, u_FogStart + 0.01), fogDistance);\n"
        "        float fogDensity = 1.0 - exp(-max(fogDistance - u_FogStart, 0.0) * max(u_FogDensity, 0.0));\n"
        "        float horizonFog = clamp(horizonBand * fogRange * fogDensity, 0.0, 1.0);\n"
        "        finalSky = mix(finalSky, u_FogColor, horizonFog);\n"
        "    }\n"
        "\n"
        "    if (u_GlobalLightEnabled != 0)\n"
        "    {\n"
        "        finalSky *= u_GlobalBrightness * exp2(u_GlobalExposure) * u_GlobalTint;\n"
        "        float luminance = dot(finalSky, vec3(0.2126, 0.7152, 0.0722));\n"
        "        finalSky = mix(vec3(luminance), finalSky, u_GlobalSaturation);\n"
        "        finalSky = (finalSky - 0.5) * u_GlobalContrast + 0.5;\n"
        "    }\n"
        "    finalSky = pow(max(finalSky, vec3(0.0)), vec3(1.0 / max(u_GlobalGamma, 0.01)));\n"
        "    vec2 screenUV = gl_FragCoord.xy / max(u_ViewportSize, vec2(1.0));\n"
        "    vec2 vignetteUV = screenUV * 2.0 - 1.0;\n"
        "    vignetteUV.x *= u_ViewportSize.x / max(u_ViewportSize.y, 1.0);\n"
        "    float vignetteEdge = smoothstep(u_GlobalVignetteRadius, 1.25, length(vignetteUV));\n"
        "    finalSky *= 1.0 - vignetteEdge * clamp(u_GlobalVignetteStrength, 0.0, 1.0);\n"
        "\n"
        "    FragColor = vec4(max(finalSky, vec3(0.0)), 1.0);\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fs);

    m_ProgramID = glCreateProgram();
    glAttachShader(m_ProgramID, vs);
    glAttachShader(m_ProgramID, fs);
    glLinkProgram(m_ProgramID);

    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(m_ProgramID);
    m_LocInvView = glGetUniformLocation(m_ProgramID, "u_InvView");
    m_LocInvProjection = glGetUniformLocation(m_ProgramID, "u_InvProjection");
    m_LocSunDirection = glGetUniformLocation(m_ProgramID, "u_SunDirection");
    m_LocSunColor = glGetUniformLocation(m_ProgramID, "u_SunColor");
    m_LocSunIntensity = glGetUniformLocation(m_ProgramID, "u_SunIntensity");
    m_LocMoonDirection = glGetUniformLocation(m_ProgramID, "u_MoonDirection");
    m_LocMoonColor = glGetUniformLocation(m_ProgramID, "u_MoonColor");
    m_LocMoonIntensity = glGetUniformLocation(m_ProgramID, "u_MoonIntensity");
    m_LocSkyAmbientFactor = glGetUniformLocation(m_ProgramID, "u_SkyAmbientFactor");
    m_LocTime = glGetUniformLocation(m_ProgramID, "u_Time");
    m_LocCloudOctaves = glGetUniformLocation(m_ProgramID, "u_CloudOctaves");
    m_LocSkyZenithColor = glGetUniformLocation(m_ProgramID, "u_SkyZenithColor");
    m_LocSkyHorizonColor = glGetUniformLocation(m_ProgramID, "u_SkyHorizonColor");
    m_LocSkyGroundColor = glGetUniformLocation(m_ProgramID, "u_SkyGroundColor");
    m_LocCloudsEnabled = glGetUniformLocation(m_ProgramID, "u_CloudsEnabled");
    m_LocCloudColor = glGetUniformLocation(m_ProgramID, "u_CloudColor");
    m_LocCloudAmount = glGetUniformLocation(m_ProgramID, "u_CloudAmount");
    m_LocCloudMovementEnabled = glGetUniformLocation(m_ProgramID, "u_CloudMovementEnabled");
    m_LocCloudSpeed = glGetUniformLocation(m_ProgramID, "u_CloudSpeed");
    m_LocCloudVolumeEnabled = glGetUniformLocation(m_ProgramID, "u_CloudVolumeEnabled");
    m_LocCloudVolumeBaseHeight = glGetUniformLocation(m_ProgramID, "u_CloudVolumeBaseHeight");
    m_LocCloudVolumeThickness = glGetUniformLocation(m_ProgramID, "u_CloudVolumeThickness");
    m_LocCloudVolumeDensity = glGetUniformLocation(m_ProgramID, "u_CloudVolumeDensity");
    m_LocCloudVolumeScale = glGetUniformLocation(m_ProgramID, "u_CloudVolumeScale");
    m_LocCloudVolumeSteps = glGetUniformLocation(m_ProgramID, "u_CloudVolumeSteps");
    m_LocGlobalEnabled = glGetUniformLocation(m_ProgramID, "u_GlobalLightEnabled");
    m_LocGlobalBrightness = glGetUniformLocation(m_ProgramID, "u_GlobalBrightness");
    m_LocGlobalContrast = glGetUniformLocation(m_ProgramID, "u_GlobalContrast");
    m_LocGlobalSaturation = glGetUniformLocation(m_ProgramID, "u_GlobalSaturation");
    m_LocGlobalExposure = glGetUniformLocation(m_ProgramID, "u_GlobalExposure");
    m_LocGlobalTint = glGetUniformLocation(m_ProgramID, "u_GlobalTint");
    m_LocGlobalGamma = glGetUniformLocation(m_ProgramID, "u_GlobalGamma");
    m_LocGlobalVignetteStrength = glGetUniformLocation(m_ProgramID, "u_GlobalVignetteStrength");
    m_LocGlobalVignetteRadius = glGetUniformLocation(m_ProgramID, "u_GlobalVignetteRadius");
    m_LocViewportSize = glGetUniformLocation(m_ProgramID, "u_ViewportSize");
    m_LocFogEnabled = glGetUniformLocation(m_ProgramID, "u_FogEnabled");
    m_LocFogColor = glGetUniformLocation(m_ProgramID, "u_FogColor");
    m_LocFogDensity = glGetUniformLocation(m_ProgramID, "u_FogDensity");
    m_LocFogStart = glGetUniformLocation(m_ProgramID, "u_FogStart");
    m_LocFogEnd = glGetUniformLocation(m_ProgramID, "u_FogEnd");

    // Full-screen quad geometry
    const float quadVertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    std::cout << "[SkyRenderer] Procedural Skybox initialized!" << std::endl;
    return true;
}

void SkyRenderer::SetCloudQuality(int octaves)
{
    m_CloudQualityCap = std::max(0, std::min(5, octaves));
    m_CloudOctaves = m_CloudQualityCap;
}

void SkyRenderer::SetSkyAppearance(const GlobalLightingSettings& settings)
{
    m_SkyZenithColor = glm::vec3(settings.SkyZenithColor.X, settings.SkyZenithColor.Y, settings.SkyZenithColor.Z);
    m_SkyHorizonColor = glm::vec3(settings.SkyHorizonColor.X, settings.SkyHorizonColor.Y, settings.SkyHorizonColor.Z);
    m_SkyGroundColor = glm::vec3(settings.SkyGroundColor.X, settings.SkyGroundColor.Y, settings.SkyGroundColor.Z);
    m_CloudsEnabled = settings.CloudsEnabled;
    m_CloudColor = glm::vec3(settings.CloudColor.X, settings.CloudColor.Y, settings.CloudColor.Z);
    m_CloudAmount = std::clamp(settings.CloudAmount, 0.0f, 1.0f);
    m_CloudOctaves = std::min(std::clamp(settings.CloudOctaves, 1, 5), m_CloudQualityCap);
    m_CloudMovementEnabled = settings.CloudMovementEnabled;
    m_CloudSpeed = std::clamp(settings.CloudSpeed, 0.0f, 10.0f);
    m_CloudVolumeEnabled = settings.VolumetricCloudsEnabled && m_CloudQualityCap > 0;
    m_CloudVolumeBaseHeight = std::clamp(settings.VolumetricCloudBaseHeight, 0.0f, 10000.0f);
    m_CloudVolumeThickness = std::clamp(settings.VolumetricCloudThickness, 10.0f, 5000.0f);
    m_CloudVolumeDensity = std::clamp(settings.VolumetricCloudDensity, 0.0f, 3.0f);
    m_CloudVolumeScale = std::clamp(settings.VolumetricCloudScale, 0.25f, 4.0f);
    const int profileStepCap = m_CloudQualityCap >= 5 ? 16 :
        (m_CloudQualityCap >= 4 ? 10 : (m_CloudQualityCap >= 3 ? 6 : 0));
    m_CloudVolumeSteps = std::min(std::clamp(settings.VolumetricCloudSteps, 4, 16), profileStepCap);
}

void SkyRenderer::SetGlobalLightFilter(
    bool enabled,
    float brightness,
    float contrast,
    float saturation,
    float exposure,
    float tintR,
    float tintG,
    float tintB)
{
    m_GlobalEnabled = enabled;
    m_GlobalBrightness = brightness;
    m_GlobalContrast = contrast;
    m_GlobalSaturation = saturation;
    m_GlobalExposure = exposure;
    m_GlobalTint = glm::vec3(tintR, tintG, tintB);
}

void SkyRenderer::SetGlobalLightAtmosphere(
    float vignetteStrength,
    float vignetteRadius,
    float gamma,
    float viewportWidth,
    float viewportHeight)
{
    m_GlobalVignetteStrength = vignetteStrength;
    m_GlobalVignetteRadius = vignetteRadius;
    m_GlobalGamma = gamma;
    m_ViewportSize = glm::vec2(viewportWidth, viewportHeight);
}

void SkyRenderer::SetGlobalFog(bool enabled, const glm::vec3& color, float density,
                               float start, float end)
{
    m_FogEnabled = enabled;
    m_FogColor = color;
    m_FogDensity = std::max(0.0f, density);
    m_FogStart = std::max(0.0f, start);
    m_FogEnd = std::max(m_FogStart + 0.01f, end);
}

void SkyRenderer::Render(
    const glm::mat4& view,
    const glm::mat4& projection,
    const LightingManager& lighting,
    const LightingSettings* advancedSettings)
{
    if (m_ProgramID == 0 || m_VAO == 0)
        return;

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m_ProgramID);

    glm::mat4 invView = glm::inverse(view);
    glm::mat4 invProj = glm::inverse(projection);

    if (m_LocInvView >= 0)
        glUniformMatrix4fv(m_LocInvView, 1, GL_FALSE, &invView[0][0]);

    if (m_LocInvProjection >= 0)
        glUniformMatrix4fv(m_LocInvProjection, 1, GL_FALSE, &invProj[0][0]);

    glm::vec3 sunDirection(
        lighting.Sun.Direction.X, lighting.Sun.Direction.Y, lighting.Sun.Direction.Z);
    glm::vec3 sunColor(
        lighting.Sun.Color.X, lighting.Sun.Color.Y, lighting.Sun.Color.Z);
    float sunIntensity = lighting.Sun.Intensity;
    glm::vec3 moonDirection(
        lighting.Moon.Direction.X, lighting.Moon.Direction.Y, lighting.Moon.Direction.Z);
    glm::vec3 moonColor(
        lighting.Moon.Color.X, lighting.Moon.Color.Y, lighting.Moon.Color.Z);
    float moonIntensity = 0.0f;
    float skyAmbientFactor = 1.0f;
    if (advancedSettings && advancedSettings->Enabled && advancedSettings->DayNightCycleEnabled)
    {
        const DayNightSample sample = LightingSystem::EvaluateDayNightCycle(*advancedSettings);
        sunDirection = sample.RayDirection;
        sunColor = sample.Color;
        sunIntensity = sample.Intensity;
        moonDirection = -sample.RayDirection;
        moonIntensity = std::max(0.0f, lighting.Moon.Intensity) * sample.Night * 0.12f;
        if (lighting.Moon.UseTemperature)
            moonColor *= LightingSystem::ColorTemperatureToRGB(lighting.Moon.TemperatureKelvin);
        skyAmbientFactor = 0.025f + 0.975f * sample.Daylight;
    }

    if (m_LocSunDirection >= 0)
        glUniform3f(m_LocSunDirection, sunDirection.x, sunDirection.y, sunDirection.z);
    if (m_LocSunColor >= 0)
        glUniform3f(m_LocSunColor, sunColor.r, sunColor.g, sunColor.b);
    if (m_LocSunIntensity >= 0)
        glUniform1f(m_LocSunIntensity, sunIntensity);
    if (m_LocMoonDirection >= 0)
        glUniform3f(m_LocMoonDirection, moonDirection.x, moonDirection.y, moonDirection.z);
    if (m_LocMoonColor >= 0)
        glUniform3f(m_LocMoonColor, moonColor.r, moonColor.g, moonColor.b);
    if (m_LocMoonIntensity >= 0)
        glUniform1f(m_LocMoonIntensity, moonIntensity);
    if (m_LocSkyAmbientFactor >= 0)
        glUniform1f(m_LocSkyAmbientFactor, skyAmbientFactor);

    if (m_LocTime >= 0)
        glUniform1f(m_LocTime, (float)glfwGetTime());

    if (m_LocCloudOctaves >= 0)
        glUniform1i(m_LocCloudOctaves, m_CloudOctaves);
    if (m_LocSkyZenithColor >= 0)
        glUniform3f(m_LocSkyZenithColor, m_SkyZenithColor.r, m_SkyZenithColor.g, m_SkyZenithColor.b);
    if (m_LocSkyHorizonColor >= 0)
        glUniform3f(m_LocSkyHorizonColor, m_SkyHorizonColor.r, m_SkyHorizonColor.g, m_SkyHorizonColor.b);
    if (m_LocSkyGroundColor >= 0)
        glUniform3f(m_LocSkyGroundColor, m_SkyGroundColor.r, m_SkyGroundColor.g, m_SkyGroundColor.b);
    if (m_LocCloudsEnabled >= 0)
        glUniform1i(m_LocCloudsEnabled, m_CloudsEnabled ? 1 : 0);
    if (m_LocCloudColor >= 0)
        glUniform3f(m_LocCloudColor, m_CloudColor.r, m_CloudColor.g, m_CloudColor.b);
    if (m_LocCloudAmount >= 0)
        glUniform1f(m_LocCloudAmount, m_CloudAmount);
    if (m_LocCloudMovementEnabled >= 0)
        glUniform1i(m_LocCloudMovementEnabled, m_CloudMovementEnabled ? 1 : 0);
    if (m_LocCloudSpeed >= 0)
        glUniform1f(m_LocCloudSpeed, m_CloudSpeed);
    if (m_LocCloudVolumeEnabled >= 0)
        glUniform1i(m_LocCloudVolumeEnabled, m_CloudVolumeEnabled ? 1 : 0);
    if (m_LocCloudVolumeBaseHeight >= 0)
        glUniform1f(m_LocCloudVolumeBaseHeight, m_CloudVolumeBaseHeight);
    if (m_LocCloudVolumeThickness >= 0)
        glUniform1f(m_LocCloudVolumeThickness, m_CloudVolumeThickness);
    if (m_LocCloudVolumeDensity >= 0)
        glUniform1f(m_LocCloudVolumeDensity, m_CloudVolumeDensity);
    if (m_LocCloudVolumeScale >= 0)
        glUniform1f(m_LocCloudVolumeScale, m_CloudVolumeScale);
    if (m_LocCloudVolumeSteps >= 0)
        glUniform1i(m_LocCloudVolumeSteps, m_CloudVolumeSteps);
    if (m_LocGlobalEnabled >= 0)
        glUniform1i(m_LocGlobalEnabled, m_GlobalEnabled ? 1 : 0);
    if (m_LocGlobalBrightness >= 0)
        glUniform1f(m_LocGlobalBrightness, m_GlobalBrightness);
    if (m_LocGlobalContrast >= 0)
        glUniform1f(m_LocGlobalContrast, m_GlobalContrast);
    if (m_LocGlobalSaturation >= 0)
        glUniform1f(m_LocGlobalSaturation, m_GlobalSaturation);
    if (m_LocGlobalExposure >= 0)
        glUniform1f(m_LocGlobalExposure, m_GlobalExposure);
    if (m_LocGlobalTint >= 0)
        glUniform3f(m_LocGlobalTint, m_GlobalTint.r, m_GlobalTint.g, m_GlobalTint.b);
    if (m_LocGlobalGamma >= 0)
        glUniform1f(m_LocGlobalGamma, m_GlobalGamma);
    if (m_LocGlobalVignetteStrength >= 0)
        glUniform1f(m_LocGlobalVignetteStrength, m_GlobalVignetteStrength);
    if (m_LocGlobalVignetteRadius >= 0)
        glUniform1f(m_LocGlobalVignetteRadius, m_GlobalVignetteRadius);
    if (m_LocViewportSize >= 0)
        glUniform2f(m_LocViewportSize, m_ViewportSize.x, m_ViewportSize.y);
    if (m_LocFogEnabled >= 0)
        glUniform1i(m_LocFogEnabled, m_FogEnabled ? 1 : 0);
    if (m_LocFogColor >= 0)
        glUniform3f(m_LocFogColor, m_FogColor.r, m_FogColor.g, m_FogColor.b);
    if (m_LocFogDensity >= 0)
        glUniform1f(m_LocFogDensity, m_FogDensity);
    if (m_LocFogStart >= 0)
        glUniform1f(m_LocFogStart, m_FogStart);
    if (m_LocFogEnd >= 0)
        glUniform1f(m_LocFogEnd, m_FogEnd);

    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}
