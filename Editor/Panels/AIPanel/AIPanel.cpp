#include "../../../Engine/Localization/Localization.h"
#include "AIPanel.h"
#include "AIHttp.h"
#include "NexoLocal.h"
#include "../../../Core/Application.h"
#include "../../../Scene/Scene/Scene.h"
#include "../../../Scene/Entity/Entity.h"
#include "../../../Scene/Camera/Camera.h"
#include "../../../ThirdParty/json/json.hpp"
#include "../../../Engine/Localization/Localization.h"
#include "../../../Engine/Scripting/ScriptingRuntime.h"
#include "../../../Assets/AssetDatabase/AssetDatabase.h"
#include "../../../Renderer/Texture/TextureCache.h"
#include "../../Modeling/ModelingOperations.h"
#include "../../Modeling/ModelingSelection.h"
#include "../../Modeling/MeshTopology.h"
#include "../../Modeling/ModelingToolset.h"
#include "../../InspectorPanel/InspectorPanel.h"

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GUID Windows_SDK_GUID
#include <windows.h>
#undef GUID
#include <shellapi.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    constexpr const char* GEMINI_DEFAULT_MODEL = "gemini-3.6-flash";
    constexpr const char* GEMINI_DEFAULT_ENDPOINT =
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent";

    std::string BuildGeminiEndpoint(const char* model)
    {
        const std::string selectedModel = (model && model[0] != '\0')
            ? model
            : GEMINI_DEFAULT_MODEL;
        return std::string("https://generativelanguage.googleapis.com/v1beta/models/") +
            selectedModel + ":generateContent";
    }

    enum class APIProvider
    {
        OpenAI = 0,
        Gemini = 1,
        Anthropic = 2,
        Custom = 3
    };

    struct ProviderConfig
    {
        const char* Name;
        const char* DefaultModel;
        const char* DefaultEndpoint;
        bool UseBearerToken;
        bool UseURLKey;
    };

    static const ProviderConfig PROVIDER_CONFIGS[] = {
        {"OpenAI", "gpt-4o-mini", "https://api.openai.com/v1/chat/completions", true, false},
        {"Gemini", GEMINI_DEFAULT_MODEL, GEMINI_DEFAULT_ENDPOINT, false, true},
        {"Anthropic", "claude-3-5-sonnet-20241022", "https://api.anthropic.com/v1/messages", true, false},
        {"Custom", "", "", true, false}
    };

    struct ChatMessage
    {
        bool FromUser = false;
        std::string Text;
    };

    struct PendingResult
    {
        bool Ready = false;
        std::string Reply;
        json Actions = json::array();
        std::string Error;
    };

    struct PanelState
    {
        int ProviderIndex = (int)APIProvider::Gemini;
        bool UseLocalNexo = true;
        bool OpenSearchInBrowser = false;
        int VisualPass = 0;
        std::string VisualStatus = "Nenhuma referência visual analisada ainda.";
        char ApiKey[512] = {};
        char Model[128] = "gemini-3.6-flash";
        char Endpoint[512] = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent";
        char Prompt[8192] = {};
        std::vector<ChatMessage> Messages;
        bool SettingsLoaded = false;
        bool RequestInFlight = false;
        std::mutex ResultMutex;
        PendingResult Result;
    };

    std::string UrlEncode(const std::string& value)
    {
        static const char hex[] = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : value)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += static_cast<char>(c);
            else if (c == ' ')
                out += '+';
            else
            {
                out += '%';
                out += hex[(c >> 4) & 0x0F];
                out += hex[c & 0x0F];
            }
        }
        return out;
    }

    void OpenNexoSearch(const std::string& query)
    {
        if (query.empty())
            return;
#ifdef _WIN32
        // Mantém a referência visual no navegador: o Nexo pesquisa somente o
        // termo essencial e abre diretamente a aba Imagens.
        const std::string url = "https://www.google.com/search?tbm=isch&q=" + UrlEncode(query);
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
        (void)query;
#endif
    }

    PanelState& State()
    {
        static PanelState s;
        return s;
    }

    fs::path SettingsPath()
    {
        const std::string appdata = AIGetAppDataPath();
        if (appdata.empty())
            return fs::current_path() / "ai_panel.json";

        fs::path dir = fs::path(appdata) / "MaraeEngine";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir / "ai_panel.json";
    }

    void SaveSettings();

    void LoadSettings()
    {
        PanelState& s = State();
        if (s.SettingsLoaded)
            return;
        s.SettingsLoaded = true;

        std::ifstream in(SettingsPath());
        if (!in.is_open())
            return;

        try
        {
            json j = json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.is_object())
                return;

            auto copy = [](char* dst, size_t dstSize, const std::string& src)
            {
                if (src.empty())
                    return;
                strncpy_s(dst, dstSize, src.c_str(), _TRUNCATE);
            };

            copy(s.ApiKey, sizeof(s.ApiKey), j.value("apiKey", std::string()));
            copy(s.Model, sizeof(s.Model), j.value("model", std::string(GEMINI_DEFAULT_MODEL)));
            copy(s.Endpoint, sizeof(s.Endpoint), j.value("endpoint", std::string(GEMINI_DEFAULT_ENDPOINT)));
            s.ProviderIndex = j.value("provider", (int)APIProvider::Gemini);
            s.UseLocalNexo = j.value("useLocalNexo", true);
            s.OpenSearchInBrowser = j.value("openSearchInBrowser", false);

            // Migra configurações antigas salvas pela engine. Esses modelos
            // podem retornar 404 ou não estar disponíveis para novas contas.
            if (s.ProviderIndex == (int)APIProvider::Gemini &&
                (std::string(s.Model) == "gemini-1.0-pro" ||
                 std::string(s.Model) == "gemini-2.5-flash" ||
                 std::string(s.Endpoint).find("gemini-1.0-pro") != std::string::npos ||
                 std::string(s.Endpoint).find("gemini-2.5-flash") != std::string::npos))
            {
                strncpy_s(s.Model, sizeof(s.Model), GEMINI_DEFAULT_MODEL, _TRUNCATE);
                strncpy_s(s.Endpoint, sizeof(s.Endpoint), GEMINI_DEFAULT_ENDPOINT, _TRUNCATE);
                SaveSettings();
            }
        }
        catch (...)
        {
        }
    }

    void SaveSettings()
    {
        const PanelState& s = State();
        json j;
        j["apiKey"] = s.ApiKey;
        j["model"] = s.Model;
        j["endpoint"] = s.Endpoint;
        j["provider"] = s.ProviderIndex;
        j["useLocalNexo"] = s.UseLocalNexo;
        j["openSearchInBrowser"] = s.OpenSearchInBrowser;

        std::ofstream out(SettingsPath(), std::ios::trunc);
        if (out.is_open())
            out << j.dump(2);
    }

    void ApplyProviderConfig(int providerIndex)
    {
        PanelState& s = State();
        if (providerIndex >= 0 && providerIndex < 4)
        {
            s.ProviderIndex = providerIndex;
            const ProviderConfig& config = PROVIDER_CONFIGS[providerIndex];
            strncpy_s(s.Model, sizeof(s.Model), config.DefaultModel, _TRUNCATE);
            strncpy_s(s.Endpoint, sizeof(s.Endpoint), config.DefaultEndpoint, _TRUNCATE);
            SaveSettings();
        }
    }

    bool ShouldSkipPath(const fs::path& path)
    {
        const std::string name = path.filename().string();
        if (name == ".git" || name == ".vs" || name == "x64" || name == "Debug" ||
            name == "Release" || name == "ThirdParty" || name == "bin" || name == "obj" ||
            name == "Library" || name == ".cursor")
        {
            return true;
        }
        return false;
    }

    void CollectFiles(const fs::path& root, std::vector<std::string>& out, int& remaining)
    {
        if (remaining <= 0 || !fs::exists(root))
            return;

        std::error_code ec;
        for (fs::directory_iterator it(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (remaining <= 0)
                break;

            const fs::path& entry = it->path();
            if (ShouldSkipPath(entry))
                continue;

            if (it->is_directory(ec))
            {
                CollectFiles(entry, out, remaining);
                continue;
            }

            if (!it->is_regular_file(ec))
                continue;

            const std::string ext = entry.extension().string();
            static const char* allowed[] = {
                ".cpp", ".h", ".hpp", ".cs", ".json", ".scene", ".mat", ".txt", ".md",
                ".obj", ".gltf", ".glb", ".fbx", ".wav", ".mp3", ".ogg", ".png", ".jpg"
            };
            bool ok = false;
            for (const char* e : allowed)
            {
                if (_stricmp(ext.c_str(), e) == 0)
                {
                    ok = true;
                    break;
                }
            }
            if (!ok)
                continue;

            std::error_code relEc;
            fs::path rel = fs::relative(entry, root, relEc);
            out.push_back(relEc ? entry.string() : rel.generic_string());
            --remaining;
        }
    }

    fs::path FindEngineRoot()
    {
        std::vector<fs::path> seeds = {
            fs::current_path(),
            fs::path(AIGetExecutablePath()).parent_path()
        };

        for (fs::path dir : seeds)
        {
            for (int i = 0; i < 8 && !dir.empty(); ++i)
            {
                if (fs::exists(dir / "MaraeEngine.vcxproj") || fs::exists(dir / "Engine"))
                    return dir;
                dir = dir.parent_path();
            }
        }
        return {};
    }

    std::string SanitizeUtf8(const std::string& input)
    {
        std::string output;
        output.reserve(input.size());
        for (size_t i = 0; i < input.size();)
        {
            const unsigned char lead = static_cast<unsigned char>(input[i]);
            size_t length = 0;
            if (lead < 0x80) length = 1;
            else if (lead >= 0xC2 && lead <= 0xDF) length = 2;
            else if (lead >= 0xE0 && lead <= 0xEF) length = 3;
            else if (lead >= 0xF0 && lead <= 0xF4) length = 4;

            bool valid = length > 0 && i + length <= input.size();
            if (valid)
            {
                for (size_t j = 1; j < length; ++j)
                {
                    const unsigned char continuation = static_cast<unsigned char>(input[i + j]);
                    if ((continuation & 0xC0) != 0x80)
                    {
                        valid = false;
                        break;
                    }
                }
                // Impede sobrelongas, surrogate halves e valores acima de U+10FFFF.
                if (valid && length == 3)
                {
                    const unsigned char second = static_cast<unsigned char>(input[i + 1]);
                    if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second >= 0xA0)) valid = false;
                }
                if (valid && length == 4)
                {
                    const unsigned char second = static_cast<unsigned char>(input[i + 1]);
                    if ((lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second > 0x8F)) valid = false;
                }
            }

            if (!valid)
            {
                output += '?';
                ++i;
                continue;
            }
            output.append(input, i, length);
            i += length;
        }
        return output;
    }

    std::string ReadSmallFile(const fs::path& path, size_t maxBytes)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
            return {};

        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.size() > maxBytes)
        {
            data.resize(maxBytes);
            data += "\n... [arquivo truncado]";
        }
        return SanitizeUtf8(data);
    }

    std::string BuildMaraeApiManual()
    {
        std::ostringstream out;
        out << "API C# ATUAL da Marae (use SOMENTE isto em scripts; nao use namespace MaraeScripts nem classes soltas sem herdar ScriptBehaviour).\n";
        out << "Namespace: MaraeEngine. Compat Unity: UnityEngine.MonoBehaviour herda ScriptBehaviour; UnityEngine.Time e UnityEngine.Debug existem, mas prefira MaraeEngine.*\n\n";
        out << "class ScriptBehaviour {\n";
        out << "  Entity? gameObject / GameObject; Transform? transform;\n";
        out << "  virtual OnEnable/OnDisable/OnStart/Start/OnUpdate(float dt)/Update/OnDestroy\n";
        out << "  virtual OnCollisionEnter/Stay/Exit(Collision); OnTriggerEnter/Stay/Exit(Collider)\n";
        out << "}\n";
        out << "class Entity { ulong id; string name; bool activeSelf; string tag; int layer; Transform transform;\n";
        out << "  bool HasComponent<T>(); T? GetComponent<T>(); T = MeshComponent, AudioComponent, LightComponent, MaterialComponent, ScriptComponent;\n";
        out << "  static Entity? Find(name); FindWithTag(tag); FindGameObjectsWithTag(tag); FindEntityByID(id);\n";
        out << "  static void Destroy(Entity); static Entity Instantiate(Entity); }\n";
        out << "class GameObject { mesmos Find/Instantiate/Destroy estaticos que Entity; }\n";
        out << "class Transform { Vector3 position, localPosition, localScale, localEulerAngles, eulerAngles, forward, right, up;\n";
        out << "  Quaternion rotation, localRotation; Translate(x,y,z, Space); Rotate(x,y,z, Space); LookAt; parent; GetChild; childCount; }\n";
        out << "class Time { float deltaTime, unscaledDeltaTime, time, fixedDeltaTime, timeScale; bool isPaused; }\n";
        out << "class Input { GetKey/GetKeyDown/GetKeyUp(KeyCode); GetMouseButton(0/1/2); mousePosition; GetAxis(\"Horizontal\"|\"Vertical\"|\"Mouse X\"|\"Mouse Y\"); }\n";
        out << "KeyCode: W A S D Space LeftShift LeftControl E Q Escape Return Up/Down/Left/Right Arrow, Mouse0.\n";
        out << "MeshComponent: meshName, visible, Color color;\n";
        out << "AudioComponent: Play(); Stop(); bool isPlaying, loop; float volume, pitch;\n";
        out << "LightComponent: LightType type (Directional/Point/Spot); intensity; Color color; range; spotAngle;\n";
        out << "MaterialComponent: Color albedo; float metallic, smoothness;\n";
        out << "Vector3: X Y Z, zero, one, up, forward, right, normalized, magnitude. Quaternion.Euler / LookRotation.\n";
        out << "Exemplo de movimento:\n";
        out << "using MaraeEngine;\n";
        out << "public class PlayerMove : ScriptBehaviour {\n";
        out << "  public float speed = 6f;\n";
        out << "  public override void OnUpdate(float dt) {\n";
        out << "    float h = Input.GetAxis(\"Horizontal\");\n";
        out << "    float v = Input.GetAxis(\"Vertical\");\n";
        out << "    transform.Translate(h * speed * dt, 0f, v * speed * dt);\n";
        out << "    if (Input.GetKeyDown(KeyCode.Space)) transform.Translate(0f, 2f, 0f);\n";
        out << "    var audio = gameObject.GetComponent<AudioComponent>(); audio?.Play();\n";
        out << "  }\n}\n";
        out << "PROIBIDO: UnityEngine.GameObject.Find sem Marae, Rigidbody Unity, Transform.position como Vector3 Unity antigo, namespace MaraeScripts, OnUpdate sem herdar ScriptBehaviour.\n";
        return out.str();
    }

    std::string BuildEngineContext(Application& app)
    {
        std::ostringstream out;
        const ProjectConfig& project = app.GetProjectConfig();

        out << "Voce e a assistente da Marae Engine (editor ImGui + runtime C++ + scripts C# .NET 8).\n";
        out << "Responda em portugues. O estado abaixo e o projeto ABERTO agora, nao um dump antigo da engine.\n";
        out << "SEMPRE responda com um JSON unico: {\"reply\":\"...\",\"actions\":[...]}\n";
        out << "Quando o usuario pedir script/gameplay, GERAR o arquivo com write_script usando a API C# atual e anexar com attach.\n\n";
        out << "REGRA DE FERRAMENTAS: use primeiro as acoes novas e especificas abaixo; nao responda apenas com texto quando uma acao puder executar o pedido. Cada item de actions deve ter op e os parametros necessarios. Nunca use op fora desta lista.\n";
        out << "ACOES DE CENA:\n";
        out << "create_cube/sphere/cylinder/cone/plane/quad/triangle/wedge/torus/capsule/empty/camera (name,x,y,z,sx,sy,sz,r,g,b)\n";
        out << "set_camera (name, enabled, preview, fov, nearClip, farClip, orthographic, orthographicSize)\n";
        out << "create_point_light / create_spot_light / create_directional_light / create_area_light (name,x,y,z,intensity,range,r,g,b,shadows)\n";
        out << "set_name, set_position, set_scale, set_rotation (graus Euler), set_color (0-1)\n";
        out << "select, delete, duplicate, set_parent (name, parent), create_folder, create_model\n";
        out << "set_active (name, active), set_tag (name, tag), set_layer (name, layer), set_opacity (name, opacity), set_raycast (name, enabled)\n";
        out << "set_script (name, path), create_script (name), write_script (path, code, attach opcional), open_script (path), reload_scripts\n";
        out << "set_light (name, type=point|spot|directional|area, intensity, range, r,g,b, shadows, temperature, innerAngle, outerAngle)\n";
        out << "set_sun (intensity, r,g,b, dx,dy,dz), set_ambient (r,g,b, intensity), set_lighting (enabled, quality=performance|balanced|high|ultra)\n";
        out << "set_physics (enabled, gravity, isStatic, mass, collider=box|sphere|capsule|cylinder|cone|mesh, friction, restitution)\n";
        out << "set_material (name, r,g,b, metallic, roughness, type=plastic|metal|wood|neon, texture)\n";
        out << "add_audio (name, path, volume, loop, playOnStart, spatialBlend), play_audio, stop_audio\n";
        out << "set_global_light (enabled, exposure, brightness, contrast, saturation, temperature, tint, bloom, vignette, grain, chromatic, fogDensity, r,g,b)\n";
        out << "add_pp_volume (name, global, priority, blendWeight), set_post_processing (name, exposure, contrast, saturation, temperature, tint, toneMapping=none|reinhard|aces|filmic, bloom, vignette, chromatic, grain, lut)\n";
        out << "add_audio (name, path, volume, pitch, loop, spatialBlend, minDistance, maxDistance, doppler, mixerGroup), play_audio, stop_audio\n";
        out << "add_keyframe, import_model (path), play, pause, edit\n";
        out << "MODELADOR (atua no mesh da entidade name ou no objeto selecionado): use model_mode (enabled), model_select (name, selectionMode=0 vertices|1 edges|2 faces, selection=[indices]) antes de editar quando necessario; model_clear_selection.\n";
        out << "use selectionMode=0 vertices, 1 edges, 2 faces; selection=[indices] opcional nas operacoes.\n";
        out << "model_transform (name, selectionMode, selection, tx,ty,tz,sx,sy,sz,rx,ry,rz,proportional,radius), model_extrude (distance), model_inset (amount), model_bevel (amount), model_subdivide, model_delete_faces, model_inflate (distance)\n";
        out << "model_merge, model_dissolve, model_fill, model_loop_cut, model_knife, model_slide (amount), model_duplicate (x,y,z), model_bridge_edges, model_array (count,x,y,z), model_solidify (thickness), model_decimate (ratio)\n";
        out << "model_mirror_topology (axis=0|1|2, plane), model_mirror (axis), model_radial_resolution (sectors=3..128), model_increase_resolution (levels), model_reduce_resolution (strength), model_optimize\n";
        out << "model_flip_normals, model_recalculate_normals, model_smooth_normals (strength), model_flat_shading, model_smooth_shading. Operacoes de face usam toda a malha se nenhuma face for selecionada.\n";
        out << "ACADEMIA DO MODELADOR: voce conhece o significado das ferramentas e deve combina-las em etapas, como um modelador 3D.\n";
        out << "- model_transform move/escala/gira a selecao; use tx/ty/tz, sx/sy/sz e rx/ry/rz em graus.\n";
        out << "- model_extrude cria volume a partir de faces; model_inset cria uma borda interna; model_bevel arredonda/biseliza quinas.\n";
        out << "- model_loop_cut adiciona cortes para controlar forma; model_slide desloca cortes; model_fill fecha buracos; model_bridge_edges conecta bordas.\n";
        out << "- model_array repete uma peça; model_duplicate duplica selecao; model_mirror_topology espelha a topologia; model_solidify da espessura a superficies.\n";
        out << "- model_smooth_shading melhora a aparencia; model_recalculate_normals corrige a iluminacao; model_optimize limpa a malha.\n";
        out << "REGRA DE CONSTRUCAO: para casa, lanterna ou objeto complexo, crie varias pecas primitivas, de nomes claros, ajuste escala/posicao/material, crie um create_model e use set_parent para agrupar todas as pecas. Nao tente fazer tudo em uma unica malha se pecas separadas forem mais robustas.\n";
        out << "RECEITA CASA: create_model Casa; cubo para paredes; cone/piramide para telhado; cubos para porta e janelas; set_parent em todas as pecas; materiais coerentes.\n";
        out << "RECEITA LANTERNA: create_model Lanterna; cylinder para corpo; torus para aros; sphere/cylinder para luz e tampa; create_point_light; agrupe tudo e configure emissao/material.\n";
        out << "RECEITA OBJETO MODELADO: create_cube, model_mode enabled, model_select em faces, model_extrude + model_inset + model_bevel + model_smooth_shading; salve e nomeie a peca.\n";
        out << "Sempre use nomes unicos, posicoes que mantenham as pecas proximas, selecione o alvo pelo campo name e finalize com model_recalculate_normals.\n";
        out << "Se nao alterar nada, actions=[]. Nao invente entidades.\n\n";
        out << BuildMaraeApiManual() << "\n";

        out << "Projeto do usuario:\n";
        out << "- pasta: " << (project.ProjectPath.empty() ? fs::current_path().string() : project.ProjectPath) << "\n";
        out << "- cena: " << (project.ScenePath.empty() ? "(nenhuma)" : project.ScenePath) << "\n";
        out << "- estado do editor: ";
        switch (app.GetEditorState())
        {
        case Application::EditorState::Play: out << "Play\n"; break;
        case Application::EditorState::Pause: out << "Pause\n"; break;
        default: out << "Edit\n"; break;
        }

        Camera& cam = app.GetCamera();
        out << "- camera pos: " << cam.Position.X << ", " << cam.Position.Y << ", " << cam.Position.Z << "\n";

        auto& entities = app.GetScene().GetEntities();
        out << "\nCena atual (" << entities.size() << " entidades):\n";
        int listed = 0;
        for (Entity& entity : entities)
        {
            if (entity.IsEditorOnly())
                continue;
            if (listed++ >= 80)
            {
                out << "- ... mais entidades omitidas\n";
                break;
            }

            Transform& t = entity.GetTransform();
            const Vector3& color = entity.GetColorComponent().Color;
            out << "- " << entity.GetName()
                << " pos=(" << t.Position.X << "," << t.Position.Y << "," << t.Position.Z << ")"
                << " rot=(" << t.EulerRotation.X << "," << t.EulerRotation.Y << "," << t.EulerRotation.Z << ")"
                << " scale=(" << t.Scale.X << "," << t.Scale.Y << "," << t.Scale.Z << ")"
                << " color=(" << color.X << "," << color.Y << "," << color.Z << ")";
            if (!entity.GetMeshComponent().MeshPath.empty())
                out << " mesh=" << entity.GetMeshComponent().MeshPath;
            if (entity.HasLight())
            {
                LightComponent& light = entity.GetLightComponent();
                out << " [luz " << (light.Type == LightType::Spot ? "spot" : "point")
                    << " intensity=" << light.Intensity
                    << " range=" << light.Range << "]";
            }
            if (entity.HasAudio())
            {
                AudioComponent& audio = entity.GetAudioComponent();
                out << " [audio clip=" << (audio.ClipPath.empty() ? audio.ClipID : audio.ClipPath)
                    << " vol=" << audio.Volume
                    << (audio.Loop ? " loop" : "")
                    << "]";
            }
            if (entity.IsGlobalLight())
                out << " [luz global]";
            if (entity.HasPostProcessingVolume())
                out << " [volume PP]";
            if (entity.GetAnimationComponent().Playing)
                out << " [anim playing]";
            if (!entity.Active)
                out << " [inactive]";
            if (entity.Opacity <= 0.001f)
                out << " [hidden]";
            else if (entity.Opacity < 0.999f)
                out << " [opacity=" << entity.Opacity << "]";
            if (!entity.RaycastTarget)
                out << " [raycast off]";
            out << " layer=" << entity.Layer;
            if (!entity.Tag.empty())
                out << " tag=" << entity.Tag;
            if (entity.IsStatic)
                out << " [static]";
            if (!entity.PhysicsEnabled)
                out << " [physics off]";
            if (!entity.GetScriptComponent().ScriptPath.empty())
                out << " script=" << entity.GetScriptComponent().ScriptPath;
            out << "\n";
        }

        if (app.GetSelectedEntityIndex() >= 0)
        {
            Entity& selected = app.GetSelectedEntity();
            if (!selected.IsEditorOnly())
                out << "\nEntidade selecionada: " << selected.GetName() << "\n";
        }
        else
        {
            out << "\nNenhuma entidade selecionada.\n";
        }

        if (const AssetInfo* asset = app.GetSelectedAsset())
            out << "Asset selecionado: " << asset->Name << " (" << asset->Path << ")\n";

        LightingManager& lighting = app.GetScene().GetLightingManager();
        out << "Iluminacao global: enabled=" << (lighting.Settings.Enabled ? "true" : "false")
            << " sunIntensity=" << lighting.Sun.Intensity
            << " sunDir=(" << lighting.Sun.Direction.X << "," << lighting.Sun.Direction.Y << "," << lighting.Sun.Direction.Z << ")"
            << " ambient=(" << lighting.Ambient.Color.X << "," << lighting.Ambient.Color.Y << "," << lighting.Ambient.Color.Z << ")\n";
        out << "Scripting .NET: " << (ScriptingRuntime::Instance().IsHostFxrLoaded() ? "ativo" : "desativado (HostFxr)") << "\n";

        const auto& assets = AssetDatabase::GetAssets();
        if (!assets.empty())
        {
            out << "\nAssets do projeto:\n";
            int shown = 0;
            for (const AssetInfo& asset : assets)
            {
                if (shown++ >= 60)
                {
                    out << "- ... mais assets omitidos\n";
                    break;
                }
                out << "- " << asset.Name << " [" << asset.Path << "]\n";
            }
        }

        fs::path projectRoot = project.ProjectPath.empty() ? fs::current_path() : fs::path(project.ProjectPath);
        std::vector<std::string> projectFiles;
        int remaining = 80;
        CollectFiles(projectRoot, projectFiles, remaining);
        out << "\nArquivos do projeto:\n";
        for (const std::string& file : projectFiles)
            out << "- " << file << "\n";

        const fs::path scriptsDir = projectRoot / "Scripts";
        int scriptsRead = 0;
        if (fs::exists(scriptsDir))
        {
            std::error_code ec;
            for (fs::directory_iterator it(scriptsDir, ec); !ec && it != fs::directory_iterator() && scriptsRead < 8; it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                    continue;
                if (_stricmp(it->path().extension().string().c_str(), ".cs") != 0)
                    continue;
                out << "\n--- Conteudo de Scripts/" << it->path().filename().string() << " ---\n";
                out << ReadSmallFile(it->path(), 2500) << "\n";
                ++scriptsRead;
            }
        }

        const fs::path engineRoot = FindEngineRoot();
        if (!engineRoot.empty())
        {
            out << "\nEngine Marae (codigo em " << engineRoot.string() << "):\n";
            std::vector<std::string> engineFiles;
            remaining = 50;
            CollectFiles(engineRoot / "Engine", engineFiles, remaining);
            CollectFiles(engineRoot / "Scene", engineFiles, remaining);
            CollectFiles(engineRoot / "Editor", engineFiles, remaining);
            CollectFiles(engineRoot / "Renderer", engineFiles, remaining);
            for (const std::string& file : engineFiles)
                out << "- " << file << "\n";
        }

        return out.str();
    }

    json ExtractActionsPayload(const std::string& text, std::string& reply)
    {
        reply = text;
        const size_t start = text.find('{');
        const size_t end = text.rfind('}');
        if (start == std::string::npos || end == std::string::npos || end <= start)
            return json::array();

        try
        {
            json parsed = json::parse(text.substr(start, end - start + 1), nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object())
                return json::array();

            if (parsed.contains("reply") && parsed["reply"].is_string())
                reply = parsed["reply"].get<std::string>();

            if (parsed.contains("actions") && parsed["actions"].is_array())
                return parsed["actions"];
        }
        catch (...)
        {
        }
        return json::array();
    }

    std::string ParseApiContent(const std::string& response, std::string& error)
    {
        json parsed = json::parse(response, nullptr, false);
        if (parsed.is_discarded())
        {
            error = "A API devolveu uma resposta que nao e JSON.";
            return {};
        }

        if (parsed.contains("error"))
        {
            if (parsed["error"].is_object())
                error = parsed["error"].value("message", "Erro da API.");
            else
                error = parsed["error"].dump();
            return {};
        }

        // OpenAI format
        if (parsed.contains("choices") && parsed["choices"].is_array() && !parsed["choices"].empty())
        {
            const json& message = parsed["choices"][0]["message"];
            if (message.contains("content"))
            {
                const json& content = message["content"];
                if (content.is_string())
                    return content.get<std::string>();
                if (content.is_array())
                {
                    std::string joined;
                    for (const json& part : content)
                    {
                        if (part.is_string())
                            joined += part.get<std::string>();
                        else if (part.is_object() && part.contains("text") && part["text"].is_string())
                            joined += part["text"].get<std::string>();
                    }
                    if (!joined.empty())
                        return joined;
                }
            }
        }

        // Gemini format
        if (parsed.contains("candidates") && parsed["candidates"].is_array() && !parsed["candidates"].empty())
        {
            const json& candidate = parsed["candidates"][0];
            if (candidate.contains("content") && candidate["content"].contains("parts") &&
                candidate["content"]["parts"].is_array() && !candidate["content"]["parts"].empty())
            {
                const json& parts = candidate["content"]["parts"];
                std::string joined;
                for (const json& part : parts)
                {
                    if (part.contains("text") && part["text"].is_string())
                        joined += part["text"].get<std::string>();
                }
                if (!joined.empty())
                    return joined;
            }
        }

        // Anthropic Claude format
        if (parsed.contains("content") && parsed["content"].is_array() && !parsed["content"].empty())
        {
            const json& content = parsed["content"];
            std::string joined;
            for (const json& block : content)
            {
                if (block.contains("text") && block["text"].is_string())
                    joined += block["text"].get<std::string>();
            }
            if (!joined.empty())
                return joined;
        }

        error = "A resposta da API nao contem texto.";
        return {};
    }

    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    Entity* FindEntityByName(Scene& scene, const std::string& name)
    {
        if (name.empty())
            return nullptr;

        Entity* exact = nullptr;
        Entity* caseInsensitive = nullptr;
        Entity* contains = nullptr;
        int containsCount = 0;
        const std::string needle = ToLower(name);

        for (Entity& entity : scene.GetEntities())
        {
            if (entity.IsEditorOnly())
                continue;
            if (entity.GetName() == name)
                exact = &entity;
            const std::string lowered = ToLower(entity.GetName());
            if (lowered == needle)
                caseInsensitive = &entity;
            if (lowered.find(needle) != std::string::npos)
            {
                contains = &entity;
                ++containsCount;
            }
        }

        if (exact)
            return exact;
        if (caseInsensitive)
            return caseInsensitive;
        if (containsCount == 1)
            return contains;
        return nullptr;
    }

    int FindEntityIndex(Application& app, Entity* entity)
    {
        if (!entity)
            return -1;
        auto& entities = app.GetScene().GetEntities();
        for (int i = 0; i < (int)entities.size(); ++i)
        {
            if (&entities[i] == entity)
                return i;
        }
        return -1;
    }

    void ApplyCreateExtras(Entity* entity, const json& action)
    {
        if (!entity)
            return;

        if (action.contains("name") && action["name"].is_string())
            entity->SetName(action.value("name", entity->GetName()));

        if (action.contains("x") || action.contains("y") || action.contains("z"))
        {
            entity->GetTransform().Position = Vector3(
                action.value("x", entity->GetTransform().Position.X),
                action.value("y", entity->GetTransform().Position.Y),
                action.value("z", entity->GetTransform().Position.Z));
        }

        if (action.contains("sx") || action.contains("sy") || action.contains("sz"))
        {
            entity->GetTransform().Scale = Vector3(
                action.value("sx", entity->GetTransform().Scale.X),
                action.value("sy", entity->GetTransform().Scale.Y),
                action.value("sz", entity->GetTransform().Scale.Z));
        }

        if (action.contains("r") || action.contains("g") || action.contains("b"))
        {
            const float r = action.value("r", 0.8f);
            const float g = action.value("g", 0.8f);
            const float b = action.value("b", 0.8f);
            entity->GetColorComponent().Color = Vector3(r, g, b);
            entity->GetMaterialComponent().MaterialData.Color[0] = r;
            entity->GetMaterialComponent().MaterialData.Color[1] = g;
            entity->GetMaterialComponent().MaterialData.Color[2] = b;
            entity->GetMaterialComponent().MaterialData.Color[3] = 1.0f;
        }
    }

    void ApplyEuler(Entity& entity, float x, float y, float z)
    {
        Transform& t = entity.GetTransform();
        t.EulerRotation = Vector3(x, y, z);
        t.LocalEulerRotation = t.EulerRotation;
        t.Rotation = glm::quat(glm::radians(glm::vec3(x, y, z)));
        t.LocalRotation = t.Rotation;
    }

    static LightType ParseLightType(const std::string& value)
    {
        const std::string type = ToLower(value);
        if (type == "spot") return LightType::Spot;
        if (type == "directional" || type == "sun") return LightType::Directional;
        if (type == "area") return LightType::Area;
        return LightType::Point;
    }

    static GlobalLightComponent::ToneMappingOperator ParseToneMapping(const std::string& value)
    {
        const std::string tone = ToLower(value);
        if (tone == "none" || tone == "off") return GlobalLightComponent::ToneMappingOperator::None;
        if (tone == "reinhard") return GlobalLightComponent::ToneMappingOperator::Reinhard;
        if (tone == "filmic") return GlobalLightComponent::ToneMappingOperator::Filmic;
        return GlobalLightComponent::ToneMappingOperator::ACES;
    }

    static void ApplyLightFields(Entity& entity, const json& action)
    {
        entity.AddLight(LightType::Point);
        LightComponent& light = entity.GetLightComponent();
        if (action.contains("type")) light.Type = ParseLightType(action.value("type", std::string("point")));
        if (action.contains("intensity")) light.Intensity = std::max(0.0f, action.value("intensity", light.Intensity));
        if (action.contains("range")) light.Range = std::max(0.01f, action.value("range", light.Range));
        if (action.contains("shadows")) light.CastShadows = action.value("shadows", light.CastShadows);
        if (action.contains("temperature"))
        {
            light.UseTemperature = true;
            light.TemperatureKelvin = std::clamp(action.value("temperature", light.TemperatureKelvin), 1000.0f, 20000.0f);
        }
        if (action.contains("r")) light.Color[0] = std::clamp(action.value("r", light.Color[0]), 0.0f, 1.0f);
        if (action.contains("g")) light.Color[1] = std::clamp(action.value("g", light.Color[1]), 0.0f, 1.0f);
        if (action.contains("b")) light.Color[2] = std::clamp(action.value("b", light.Color[2]), 0.0f, 1.0f);
        if (action.contains("innerAngle")) light.InnerAngleDeg = std::clamp(action.value("innerAngle", light.InnerAngleDeg), 0.0f, 179.0f);
        if (action.contains("outerAngle")) light.OuterAngleDeg = std::clamp(action.value("outerAngle", light.OuterAngleDeg), light.InnerAngleDeg, 179.0f);
    }

    static void ApplyPostFields(PostProcessingVolumeComponent& pp, const json& action)
    {
        pp.Global.Override = true;
        pp.Color.Override = true;
        pp.Tone.Override = true;
        pp.Bloom.Override = true;
        pp.Vignette.Override = true;
        pp.Chromatic.Override = true;
        pp.FilmGrain.Override = true;
        if (action.contains("exposure")) pp.Color.Exposure = action.value("exposure", pp.Color.Exposure);
        if (action.contains("contrast")) pp.Color.Contrast = action.value("contrast", pp.Color.Contrast);
        if (action.contains("saturation")) pp.Color.Saturation = action.value("saturation", pp.Color.Saturation);
        if (action.contains("temperature")) pp.Color.Temperature = action.value("temperature", pp.Color.Temperature);
        if (action.contains("tint")) pp.Color.Tint = action.value("tint", pp.Color.Tint);
        if (action.contains("toneMapping"))
        {
            const auto tone = ParseToneMapping(action.value("toneMapping", std::string("aces")));
            pp.Tone.Op = tone == GlobalLightComponent::ToneMappingOperator::None ? ToneMappingSettings::NONE :
                         tone == GlobalLightComponent::ToneMappingOperator::Reinhard ? ToneMappingSettings::REINHARD :
                         tone == GlobalLightComponent::ToneMappingOperator::Filmic ? ToneMappingSettings::FILMIC :
                         ToneMappingSettings::ACES;
        }
        if (action.contains("bloom")) { pp.Bloom.Enabled = true; pp.Bloom.Intensity = action.value("bloom", pp.Bloom.Intensity); }
        if (action.contains("vignette")) { pp.Vignette.Enabled = true; pp.Vignette.Intensity = action.value("vignette", pp.Vignette.Intensity); }
        if (action.contains("chromatic")) { pp.Chromatic.Enabled = true; pp.Chromatic.Strength = action.value("chromatic", pp.Chromatic.Strength); }
        if (action.contains("grain")) { pp.FilmGrain.Enabled = true; pp.FilmGrain.Intensity = action.value("grain", pp.FilmGrain.Intensity); }
        if (action.contains("lut") && action["lut"].is_string()) { pp.Lut.Enabled = true; pp.Lut.AssetPath = action.value("lut", std::string()); }
    }

    static void ApplyAudioFields(AudioSourceComponent& audio, const json& action)
    {
        if (action.contains("path")) audio.ClipPath = action.value("path", audio.ClipPath);
        if (action.contains("clip")) audio.ClipID = action.value("clip", audio.ClipID);
        if (action.contains("volume")) audio.Volume = std::clamp(action.value("volume", audio.Volume), 0.0f, 1.0f);
        if (action.contains("pitch")) audio.Pitch = std::max(0.01f, action.value("pitch", audio.Pitch));
        if (action.contains("loop")) audio.Loop = action.value("loop", audio.Loop);
        if (action.contains("spatialBlend")) audio.SpatialBlend = std::clamp(action.value("spatialBlend", audio.SpatialBlend), 0.0f, 1.0f);
        if (action.contains("minDistance")) audio.MinDistance = std::max(0.01f, action.value("minDistance", audio.MinDistance));
        if (action.contains("maxDistance")) audio.MaxDistance = std::max(audio.MinDistance, action.value("maxDistance", audio.MaxDistance));
        if (action.contains("doppler")) audio.DopplerFactor = std::max(0.0f, action.value("doppler", audio.DopplerFactor));
        if (action.contains("mixerGroup")) audio.MixerGroupID = action.value("mixerGroup", audio.MixerGroupID);
        if (action.contains("playOnStart")) audio.PlayOnStart = action.value("playOnStart", audio.PlayOnStart);
    }

    static bool ApplyModelingAction(Application& app, Entity& entity, const json& action)
    {
        auto& meshData = entity.GetMeshComponent().MeshData;
        if (!meshData || !meshData->IsValid())
            return false;

        const std::string op = ToLower(action.value("op", std::string()));
        std::vector<Vertex> vertices = meshData->GetVertices();
        if (vertices.empty()) return false;

        ModelingSelection selection;
        selection.Mode = static_cast<ModelingSelectionMode>(std::clamp(
            action.value("selectionMode", InspectorPanel::GetModelingSelectionMode()), 0, 2));
        if (action.contains("selection") && action["selection"].is_array())
        {
            for (const json& index : action["selection"])
                if (index.is_number_integer()) selection.Add(index.get<int>());
        }
        else if (InspectorPanel::GetModelingEntityID() == entity.GetID())
        {
            selection.Indices = InspectorPanel::GetModelingSelection();
        }

        MeshTopology topology = MeshTopology::FromVertices(vertices);
        const size_t selectionLimit = selection.Mode == ModelingSelectionMode::Vertex
            ? topology.Vertices.size()
            : selection.Mode == ModelingSelectionMode::Edge
                ? topology.GetEdges().size()
                : topology.Faces.size();
        selection.Indices.erase(
            std::remove_if(selection.Indices.begin(), selection.Indices.end(),
                [selectionLimit](int index)
                {
                    return index < 0 || static_cast<size_t>(index) >= selectionLimit;
                }),
            selection.Indices.end());
        selection.Normalize();
        if (selection.Indices.empty() && selection.Mode == ModelingSelectionMode::Face)
        {
            const int faceCount = static_cast<int>(topology.Faces.size());
            for (int i = 0; i < faceCount; ++i) selection.Add(i);
        }
        selection.Normalize();

        std::vector<Vertex> result;
        bool changed = true;
        if (op == "model_subdivide") { ModelingToolset::LoopCut(topology, selection); result = topology.ToVertices(false); }
        else if (op == "model_extrude") { ModelingToolset::ExtrudeSelectedFaces(topology, selection, action.value("distance", 0.1f)); result = topology.ToVertices(false); }
        else if (op == "model_inset") { ModelingToolset::InsetSelectedFaces(topology, selection, action.value("amount", 0.1f)); result = topology.ToVertices(false); }
        else if (op == "model_bevel") { ModelingToolset::BevelSelected(topology, selection, action.value("amount", 0.1f)); result = topology.ToVertices(false); }
        else if (op == "model_delete_faces") { ModelingToolset::DissolveSelected(topology, selection); result = topology.ToVertices(false); }
        else if (op == "model_inflate") result = ModelingOperations::InflateSelected(vertices, action.value("distance", 0.05f), selection.Indices);
        else if (op == "model_increase_resolution") result = ModelingOperations::IncreaseResolution(vertices, action.value("levels", 1));
        else if (op == "model_reduce_resolution") result = ModelingOperations::ReduceResolution(vertices, action.value("strength", 0.15f));
        else if (op == "model_radial_resolution") result = ModelingOperations::RebuildRadialResolution(vertices, action.value("sectors", 16));
        else if (op == "model_flip_normals") { result = vertices; ModelingOperations::FlipNormals(result); }
        else if (op == "model_smooth_normals") { result = vertices; ModelingOperations::Smooth(result, action.value("strength", 0.5f)); }
        else if (op == "model_recalculate_normals") { result = vertices; ModelingOperations::RecalculateNormals(result); }
        else if (op == "model_optimize") { result = vertices; ModelingOperations::Optimize(result); }
        else if (op == "model_mirror") result = ModelingOperations::Mirror(vertices, action.value("axis", 0));
        else
        {
            changed = false;
            if (op == "model_transform")
            {
                ModelingToolset::TransformSelected(topology, selection,
                    glm::vec3(action.value("tx", 0.0f), action.value("ty", 0.0f), action.value("tz", 0.0f)),
                    glm::vec3(action.value("sx", 1.0f), action.value("sy", 1.0f), action.value("sz", 1.0f)),
                    glm::vec3(action.value("rx", 0.0f), action.value("ry", 0.0f), action.value("rz", 0.0f)),
                    action.value("proportional", false), action.value("radius", 1.0f));
                changed = true;
            }
            else if (op == "model_merge") { ModelingToolset::MergeSelected(topology, selection); changed = true; }
            else if (op == "model_dissolve") { ModelingToolset::DissolveSelected(topology, selection); changed = true; }
            else if (op == "model_fill") { ModelingToolset::FillSelected(topology, selection); changed = true; }
            else if (op == "model_loop_cut") { ModelingToolset::LoopCut(topology, selection); changed = true; }
            else if (op == "model_knife") { ModelingToolset::Knife(topology, selection); changed = true; }
            else if (op == "model_slide") { ModelingToolset::Slide(topology, selection, action.value("amount", 0.5f)); changed = true; }
            else if (op == "model_duplicate") { ModelingToolset::DuplicateSelected(topology, selection, glm::vec3(action.value("x", 1.0f), action.value("y", 0.0f), action.value("z", 0.0f))); changed = true; }
            else if (op == "model_array") { ModelingToolset::Array(topology, std::clamp(action.value("count", 2), 1, 64), glm::vec3(action.value("x", 1.0f), action.value("y", 0.0f), action.value("z", 0.0f))); changed = true; }
            else if (op == "model_solidify") { ModelingToolset::Solidify(topology, action.value("thickness", 0.05f)); changed = true; }
            else if (op == "model_decimate") { ModelingToolset::Decimate(topology, action.value("ratio", 0.5f)); changed = true; }
            else if (op == "model_bridge_edges") { ModelingToolset::BridgeSelectedEdges(topology, selection); changed = true; }
            else if (op == "model_flat_shading") { ModelingToolset::SetFlatShading(topology); changed = true; }
            else if (op == "model_smooth_shading") { ModelingToolset::SetSmoothShading(topology); changed = true; }
            else if (op == "model_mirror_topology") { ModelingToolset::Mirror(topology, action.value("axis", 0), action.value("plane", 0.0f)); changed = true; }
            if (changed) result = topology.ToVertices(op == "model_smooth_shading");
        }

        if (!changed || result.empty()) return false;
        meshData->UpdateVertices(result, "AI Modeler Operation");
        // A IA pode executar várias operações em sequência; persistir aqui
        // evita que a malha volte ao estado anterior ao recarregar a cena.
        InspectorPanel::SaveModelingMesh(app, entity);
        InspectorPanel::SetModelingSelection(entity.GetID(), static_cast<int>(selection.Mode), {});
        app.MarkDirty();
        return true;
    }

    void ApplyActions(Application& app, const json& actions)
    {
        if (!actions.is_array() || actions.empty())
            return;

        for (const json& action : actions)
        {
            if (!action.is_object())
                continue;

            try
            {

            const std::string op = action.value("op", std::string());
            auto lastCreated = [&]() -> Entity*
            {
                auto& entities = app.GetScene().GetEntities();
                for (auto it = entities.rbegin(); it != entities.rend(); ++it)
                {
                    if (!it->IsEditorOnly())
                        return &(*it);
                }
                return nullptr;
            };

            if (op == "open_web_search")
            {
                OpenNexoSearch(action.value("query", std::string()));
                continue;
            }

            if (op == "create_folder") app.CreateFolderEntity();
            else if (op == "create_model") app.CreateModelEntity();
            else if (op == "create_cube") app.CreateCubeEntity();
            else if (op == "create_sphere") app.CreateSphereEntity();
            else if (op == "create_cylinder") app.CreateCylinderEntity();
            else if (op == "create_cone") app.CreateConeEntity();
            else if (op == "create_plane") app.CreatePlaneEntity();
            else if (op == "create_quad") app.CreateQuadEntity();
            else if (op == "create_triangle") app.CreateTriangleEntity();
            else if (op == "create_wedge") app.CreateWedgeEntity();
            else if (op == "create_torus") app.CreateTorusEntity();
            else if (op == "create_capsule") app.CreateCapsuleEntity();
            else if (op == "create_empty" || op == "create_camera" || op == "create_point_light" || op == "create_spot_light" ||
                     op == "create_directional_light" || op == "create_area_light")
            {
                if (op == "create_camera") app.CreateCameraEntity();
                else app.CreateEmptyEntity();
            }

            if (op.rfind("create_", 0) == 0)
            {
                Entity* created = lastCreated();
                ApplyCreateExtras(created, action);

                if (created && (op == "create_point_light" || op == "create_spot_light" ||
                                op == "create_directional_light" || op == "create_area_light"))
                {
                    json lightAction = action;
                    lightAction["type"] = op == "create_spot_light" ? "spot" :
                                           op == "create_directional_light" ? "directional" :
                                           op == "create_area_light" ? "area" : "point";
                    ApplyLightFields(*created, lightAction);
                    if (created->GetName().rfind("Empty", 0) == 0)
                        created->SetName(op == "create_spot_light" ? "Spot Light" :
                                        op == "create_directional_light" ? "Directional Light" :
                                        op == "create_area_light" ? "Area Light" : "Point Light");
                }
            }

            if (op == "play")
            {
                app.SetEditorState(Application::EditorState::Play);
                continue;
            }
            if (op == "pause")
            {
                app.SetEditorState(Application::EditorState::Pause);
                continue;
            }
            if (op == "edit" || op == "stop")
            {
                app.SetEditorState(Application::EditorState::Edit);
                continue;
            }

            if (op == "add_keyframe")
            {
                app.AddKeyframeAtCurrentTime();
                continue;
            }

            if (op == "import_model")
            {
                const std::string path = action.value("path", std::string());
                if (!path.empty())
                    app.ImportModelEntity(path);
                continue;
            }

            Entity* target = nullptr;
            if (action.contains("name"))
                target = FindEntityByName(app.GetScene(), action.value("name", std::string()));
            if (!target && app.GetSelectedEntityIndex() >= 0)
            {
                Entity& selected = app.GetSelectedEntity();
                if (!selected.IsEditorOnly())
                    target = &selected;
            }

            if (op == "model_mode")
            {
                InspectorPanel::SetModelingMode(action.value("enabled", true));
                continue;
            }

            if (op == "model_select" || op == "model_clear_selection")
            {
                if (target)
                {
                    std::vector<int> components;
                    if (op == "model_select" && action.contains("selection") && action["selection"].is_array())
                        for (const json& value : action["selection"])
                            if (value.is_number_integer()) components.push_back(value.get<int>());
                    const int mode = std::clamp(action.value("selectionMode", InspectorPanel::GetModelingSelectionMode()), 0, 2);
                    InspectorPanel::SetModelingSelection(target->GetID(), mode, components);
                }
                continue;
            }

            if (op.rfind("model_", 0) == 0)
            {
                if (target) ApplyModelingAction(app, *target, action);
                continue;
            }

            if (op == "select" && target)
                app.SetSelectedEntity(FindEntityIndex(app, target));

            if (op == "delete" && target)
            {
                app.SetSelectedEntity(FindEntityIndex(app, target));
                app.DeleteSelectedEntity();
                continue;
            }

            if (op == "duplicate" && target)
            {
                app.SetSelectedEntity(FindEntityIndex(app, target));
                app.DuplicateSelectedEntity();
                continue;
            }

            if (op == "set_name" && target && action.contains("newName"))
                target->SetName(action.value("newName", target->GetName()));

            if (op == "set_position" && target)
            {
                target->GetTransform().Position = Vector3(
                    action.value("x", target->GetTransform().Position.X),
                    action.value("y", target->GetTransform().Position.Y),
                    action.value("z", target->GetTransform().Position.Z));
            }

            if (op == "set_scale" && target)
            {
                target->GetTransform().Scale = Vector3(
                    action.value("x", action.value("sx", target->GetTransform().Scale.X)),
                    action.value("y", action.value("sy", target->GetTransform().Scale.Y)),
                    action.value("z", action.value("sz", target->GetTransform().Scale.Z)));
            }

            if (op == "set_rotation" && target)
            {
                ApplyEuler(
                    *target,
                    action.value("x", target->GetTransform().EulerRotation.X),
                    action.value("y", target->GetTransform().EulerRotation.Y),
                    action.value("z", target->GetTransform().EulerRotation.Z));
            }

            if (op == "set_color" && target)
            {
                const float r = action.value("r", 0.8f);
                const float g = action.value("g", 0.8f);
                const float b = action.value("b", 0.8f);
                target->GetColorComponent().Color = Vector3(r, g, b);
                target->GetMaterialComponent().MaterialData.Color[0] = r;
                target->GetMaterialComponent().MaterialData.Color[1] = g;
                target->GetMaterialComponent().MaterialData.Color[2] = b;
                target->GetMaterialComponent().MaterialData.Color[3] = 1.0f;
            }

            if (op == "set_material" && target)
            {
                Material& material = target->GetMaterialComponent().MaterialData;
                if (action.contains("metallic")) material.Metallic = std::clamp(action.value("metallic", material.Metallic), 0.0f, 1.0f);
                if (action.contains("roughness")) material.Roughness = std::clamp(action.value("roughness", material.Roughness), 0.0f, 1.0f);
                if (action.contains("specular")) material.Specular = std::clamp(action.value("specular", material.Specular), 0.0f, 1.0f);
                if (action.contains("emission")) material.EmissionStrength = std::max(0.0f, action.value("emission", material.EmissionStrength));
                material.PBRParametersEnabled = true;
                if (action.contains("texture")) material.TexturePath = action.value("texture", material.TexturePath);
                if (action.contains("type"))
                {
                    const std::string type = ToLower(action.value("type", std::string("plastic")));
                    material.Type = type == "metal" ? MaterialType::Metal : type == "wood" ? MaterialType::Wood : type == "neon" ? MaterialType::Neon : MaterialType::Plastic;
                }
                if (action.contains("r") || action.contains("g") || action.contains("b"))
                {
                    material.Color[0] = action.value("r", material.Color[0]);
                    material.Color[1] = action.value("g", material.Color[1]);
                    material.Color[2] = action.value("b", material.Color[2]);
                    material.Color[3] = 1.0f;
                    target->GetColorComponent().Color = Vector3(material.Color[0], material.Color[1], material.Color[2]);
                }
            }

            if (op == "set_parent" && target)
            {
                const std::string parentName = action.value("parent", std::string());
                if (parentName.empty())
                    app.GetScene().RemoveParent(target->GetID());
                else if (Entity* parent = FindEntityByName(app.GetScene(), parentName))
                    app.GetScene().SetParent(target->GetID(), parent->GetID());
            }

            if (op == "set_camera" && target)
            {
                target->AddCamera();
                auto& camera = target->GetCameraComponent();
                camera.Enabled = action.value("enabled", camera.Enabled);
                camera.PreviewEnabled = action.value("preview", camera.PreviewEnabled);
                camera.Orthographic = action.value("orthographic", camera.Orthographic);
                camera.FieldOfView = std::clamp(action.value("fov", camera.FieldOfView), 1.0f, 179.0f);
                camera.NearClip = std::max(0.001f, action.value("nearClip", camera.NearClip));
                camera.FarClip = std::max(camera.NearClip + 0.01f, action.value("farClip", camera.FarClip));
                camera.OrthographicSize = std::max(0.1f, action.value("orthographicSize", camera.OrthographicSize));
            }

            if (op == "set_active" && target) target->Active = action.value("active", target->Active);
            if (op == "set_tag" && target) target->Tag = action.value("tag", target->Tag);
            if (op == "set_layer" && target) target->Layer = action.value("layer", target->Layer);
            if (op == "set_opacity" && target) target->Opacity = std::clamp(action.value("opacity", target->Opacity), 0.0f, 1.0f);
            if (op == "set_raycast" && target) target->RaycastTarget = action.value("enabled", target->RaycastTarget);

            if (op == "set_script" && target)
                target->GetScriptComponent().ScriptPath = action.value("path", std::string());

            if (op == "write_script" || op == "create_script")
            {
                fs::path root = app.GetProjectConfig().ProjectPath.empty()
                    ? fs::current_path() : fs::path(app.GetProjectConfig().ProjectPath);
                fs::path requested = action.value("path", std::string());
                if (requested.empty())
                    requested = fs::path("Scripts") / (action.value("name", std::string("NewScript")) + ".cs");
                fs::path scriptPath = requested.is_absolute() ? requested : root / requested;
                scriptPath = scriptPath.lexically_normal();
                const fs::path scriptsRoot = (root / "Scripts").lexically_normal();
                const std::string scriptText = action.value("code", std::string(
                    "using MaraeEngine;\n\npublic class NewScript : ScriptBehaviour\n{\n    public override void OnUpdate(float dt)\n    {\n    }\n}\n"));
                const std::string normalizedPath = scriptPath.generic_string();
                const std::string normalizedRoot = scriptsRoot.generic_string();
                if (normalizedPath.rfind(normalizedRoot, 0) == 0)
                {
                    std::error_code ec;
                    fs::create_directories(scriptPath.parent_path(), ec);
                    std::ofstream file(scriptPath, std::ios::binary | std::ios::trunc);
                    if (file.is_open())
                    {
                        file << scriptText;
                        file.close();
                        if (target && action.value("attach", true))
                            target->GetScriptComponent().ScriptPath = fs::relative(scriptPath, root, ec).generic_string();
                        app.OpenScriptInInternalEditor(scriptPath.string());
                    }
                }
                continue;
            }

            if (op == "open_script")
            {
                fs::path root = app.GetProjectConfig().ProjectPath.empty()
                    ? fs::current_path() : fs::path(app.GetProjectConfig().ProjectPath);
                fs::path requested = action.value("path", std::string());
                if (!requested.empty())
                    app.OpenScriptInInternalEditor((requested.is_absolute() ? requested : root / requested).string());
                continue;
            }

            if (op == "reload_scripts")
            {
                app.ReloadScriptsFromAI();
                continue;
            }

            if (op == "set_light")
            {
                if (!target)
                    continue;
                ApplyLightFields(*target, action);
            }

            if (op == "set_global_light")
            {
                Entity* global = target;
                if (!global)
                {
                    for (Entity& candidate : app.GetScene().GetEntities())
                        if (candidate.IsGlobalLight()) { global = &candidate; break; }
                }
                if (global)
                {
                    global->SetGlobalLight(true);
                    GlobalLightComponent& light = global->GetGlobalLightComponent();
                    if (action.contains("enabled")) light.Lighting.Enabled = action.value("enabled", light.Lighting.Enabled);
                    if (action.contains("exposure")) light.Exposure = action.value("exposure", light.Exposure);
                    if (action.contains("brightness")) light.Brightness = action.value("brightness", light.Brightness);
                    if (action.contains("contrast")) light.Contrast = action.value("contrast", light.Contrast);
                    if (action.contains("saturation")) light.Saturation = action.value("saturation", light.Saturation);
                    if (action.contains("temperature")) light.Temperature = action.value("temperature", light.Temperature);
                    if (action.contains("tint")) light.Tint = action.value("tint", light.Tint);
                    if (action.contains("bloom")) light.BloomStrength = std::max(0.0f, action.value("bloom", light.BloomStrength));
                    if (action.contains("vignette")) light.VignetteStrength = std::clamp(action.value("vignette", light.VignetteStrength), 0.0f, 1.0f);
                    if (action.contains("grain")) { light.FilmGrainEnabled = true; light.FilmGrainStrength = action.value("grain", light.FilmGrainStrength); }
                    if (action.contains("chromatic")) { light.ChromaticAberrationEnabled = true; light.ChromaticAberrationStrength = action.value("chromatic", light.ChromaticAberrationStrength); }
                    if (action.contains("fogDensity")) light.FogDensity = std::max(0.0f, action.value("fogDensity", light.FogDensity));
                    if (action.contains("r")) light.FogColor.X = action.value("r", light.FogColor.X);
                    if (action.contains("g")) light.FogColor.Y = action.value("g", light.FogColor.Y);
                    if (action.contains("b")) light.FogColor.Z = action.value("b", light.FogColor.Z);
                }
            }

            if (op == "set_sun")
            {
                LightingManager& lighting = app.GetScene().GetLightingManager();
                auto& sun = lighting.Sun;
                sun.Intensity = std::max(0.0f, action.value("intensity", sun.Intensity));
                sun.Direction = Vector3(action.value("dx", sun.Direction.X), action.value("dy", sun.Direction.Y), action.value("dz", sun.Direction.Z));
                if (action.contains("r")) sun.Color.X = action.value("r", sun.Color.X);
                if (action.contains("g")) sun.Color.Y = action.value("g", sun.Color.Y);
                if (action.contains("b")) sun.Color.Z = action.value("b", sun.Color.Z);
            }

            if (op == "set_ambient")
            {
                LightingManager& lighting = app.GetScene().GetLightingManager();
                lighting.Ambient.Intensity = std::max(0.0f, action.value("intensity", lighting.Ambient.Intensity));
                if (action.contains("r")) lighting.Ambient.Color.X = action.value("r", lighting.Ambient.Color.X);
                if (action.contains("g")) lighting.Ambient.Color.Y = action.value("g", lighting.Ambient.Color.Y);
                if (action.contains("b")) lighting.Ambient.Color.Z = action.value("b", lighting.Ambient.Color.Z);
            }

            if (op == "set_lighting")
            {
                LightingSettings& settings = app.GetScene().GetLightingManager().Settings;
                if (action.contains("enabled")) settings.Enabled = action.value("enabled", settings.Enabled);
                if (action.contains("dynamicShadows")) settings.DynamicShadows = action.value("dynamicShadows", settings.DynamicShadows);
                if (action.contains("contactShadows")) settings.ContactShadows = action.value("contactShadows", settings.ContactShadows);
                if (action.contains("quality"))
                {
                    const std::string quality = ToLower(action.value("quality", std::string("balanced")));
                    settings.Quality = quality == "performance" ? LightingQuality::Performance :
                                       quality == "high" ? LightingQuality::High :
                                       quality == "ultra" ? LightingQuality::Ultra : LightingQuality::Balanced;
                }
                settings.Clamp();
            }

            if (op == "add_pp_volume")
            {
                Entity* volume = target;
                if (!volume) { app.CreateEmptyEntity(); volume = lastCreated(); }
                if (volume)
                {
                    volume->AddPostProcessingVolume();
                    if (volume->GetName().rfind("Empty", 0) == 0) volume->SetName(action.value("name", std::string("Post Processing Volume")));
                    auto& pp = volume->GetPostProcessingVolume();
                    pp.IsGlobal = action.value("global", pp.IsGlobal);
                    pp.Priority = action.value("priority", pp.Priority);
                    pp.BlendWeight = std::clamp(action.value("blendWeight", pp.BlendWeight), 0.0f, 1.0f);
                    ApplyPostFields(pp, action);
                }
            }

            if (op == "set_post_processing" && target && target->HasPostProcessingVolume())
                ApplyPostFields(target->GetPostProcessingVolume(), action);

            if (op == "add_audio")
            {
                Entity* audioEntity = target;
                if (!audioEntity) { app.CreateEmptyEntity(); audioEntity = lastCreated(); }
                if (audioEntity)
                {
                    audioEntity->AddAudio();
                    if (audioEntity->GetName().rfind("Empty", 0) == 0) audioEntity->SetName(action.value("name", std::string("Audio Source")));
                    ApplyAudioFields(audioEntity->GetAudioComponent(), action);
                }
            }

            if ((op == "play_audio" || op == "stop_audio") && target && target->HasAudio())
            {
                AudioSourceComponent& audio = target->GetAudioComponent();
                if (op == "stop_audio")
                {
                    if (audio.RuntimeVoiceID != AUDIO_INVALID_VOICE)
                        app.GetAudioSystem().Stop(audio.RuntimeVoiceID);
                    audio.RuntimeVoiceID = AUDIO_INVALID_VOICE;
                    audio.PlayState = AudioPlayState::Stopped;
                }
                else if (!audio.ClipPath.empty() || !audio.ClipID.empty())
                {
                    AudioSourceParams params;
                    params.ClipPath = audio.ResolveClipPath();
                    params.Position = glm::vec3(target->GetTransform().Position.X, target->GetTransform().Position.Y, target->GetTransform().Position.Z);
                    params.Volume = audio.Volume; params.Pitch = audio.Pitch; params.Loop = audio.Loop;
                    params.SpatialBlend = audio.SpatialBlend; params.MinDistance = audio.MinDistance; params.MaxDistance = audio.MaxDistance;
                    params.DopplerFactor = audio.DopplerFactor; params.GroupID = audio.MixerGroupID;
                    audio.RuntimeVoiceID = app.GetAudioSystem().Play(params);
                    audio.PlayState = AudioPlayState::Playing;
                }
            }

            if (op == "set_physics" && target)
            {
                if (action.contains("enabled"))
                    target->PhysicsEnabled = action.value("enabled", target->PhysicsEnabled);
                if (action.contains("gravity"))
                    target->UseGravity = action.value("gravity", target->UseGravity);
                if (action.contains("isStatic"))
                    target->IsStatic = action.value("isStatic", target->IsStatic);
                if (action.contains("mass"))
                    target->Mass = action.value("mass", target->Mass);
            }
            }
            catch (...)
            {
            }
        }

        app.GetScene().UpdateTransforms();
        app.MarkDirty();
    }

    void NexoVisualRefine(Application& app)
    {
        PanelState& s = State();
        if (app.GetSelectedEntityIndex() < 0)
        {
            s.VisualStatus = "Passe visual aguardando um objeto selecionado na cena.";
            return;
        }

        Entity& selected = app.GetSelectedEntity();
        const std::string name = selected.GetName();
        json actions = json::array({
            {{"op", "model_mode"}, {"enabled", true}, {"name", name}},
            {{"op", "model_bevel"}, {"name", name}, {"amount", 0.025f}},
            {{"op", "model_smooth_shading"}, {"name", name}},
            {{"op", "model_recalculate_normals"}, {"name", name}},
            {{"op", "model_optimize"}, {"name", name}}
        });
        ApplyActions(app, actions);
        ++s.VisualPass;
        s.VisualStatus = "Passe " + std::to_string(s.VisualPass) +
            " aplicado em " + name + ": bevel econômico, suavização, normais e otimização. "
            "A comparação semântica com os pixels da referência ainda requer uma captura de imagem.";
    }

    void StartRequest(Application& app)
    {
        PanelState& s = State();
        if (s.RequestInFlight)
            return;

        std::string prompt = SanitizeUtf8(s.Prompt);
        while (!prompt.empty() && (unsigned char)prompt.back() <= ' ')
            prompt.pop_back();
        if (prompt.empty())
            return;

        if (s.UseLocalNexo)
        {
            std::string localReply;
            std::string localActionsText;
            std::string searchQuery;
            if (NexoLocalPlan(prompt, localReply, localActionsText, searchQuery))
            {
                s.Messages.push_back({ true, prompt });
                s.Prompt[0] = '\0';
                s.Messages.push_back({ false, localReply });

                json localActions = json::parse(localActionsText.empty() ? "[]" : localActionsText, nullptr, false);
                if (localActions.is_discarded() || !localActions.is_array())
                    localActions = json::array();
                if (s.OpenSearchInBrowser && !searchQuery.empty())
                    localActions.insert(localActions.begin(),
                        {{"op", "open_web_search"}, {"query", searchQuery}});
                ApplyActions(app, localActions);
                return;
            }
        }

        if (s.ApiKey[0] == '\0')
        {
            s.Messages.push_back({ false, "Coloque uma chave de API OpenAI-compativel acima para conversar." });
            return;
        }

        s.Messages.push_back({ true, prompt });
        s.Prompt[0] = '\0';
        s.RequestInFlight = true;
        {
            std::lock_guard<std::mutex> lock(s.ResultMutex);
            s.Result = PendingResult{};
        }

        const std::string endpoint = s.Endpoint;
        const bool isGemini = endpoint.find("googleapis.com") != std::string::npos;
        const bool isAnthropic = endpoint.find("anthropic.com") != std::string::npos;
        const bool isCustom = s.ProviderIndex == (int)APIProvider::Custom;
        const std::string engineContext = SanitizeUtf8(BuildEngineContext(app));

        json body;
        std::string finalEndpoint = endpoint;
        std::string finalApiKey = s.ApiKey;

        if (isGemini)
        {
            // O campo Modelo é a fonte da verdade para o Gemini. Assim, trocar
            // o modelo no painel também troca o caminho /models/<modelo>.
            if (endpoint.find("generativelanguage.googleapis.com/v1beta/models/") != std::string::npos)
                finalEndpoint = BuildGeminiEndpoint(s.Model);

            // Gemini format
            json contents = json::array();
            contents.push_back({
                {"parts", {{{"text", engineContext}}}}
            });

            const int historyStart = (int)s.Messages.size() > 12 ? (int)s.Messages.size() - 12 : 0;
            for (int i = historyStart; i < (int)s.Messages.size(); ++i)
            {
                contents.push_back({
                    {"parts", {{{"text", SanitizeUtf8(s.Messages[i].Text)}}}}
                });
            }

            body["contents"] = contents;
            body["generationConfig"] = {
                {"temperature", 0.2},
                {"maxOutputTokens", 8192},
                {"responseMimeType", "application/json"}
            };

            // Gemini uses API key as URL parameter
            if (finalEndpoint.find("?key=") == std::string::npos)
            {
                finalEndpoint += "?key=" + finalApiKey;
            }
            finalApiKey = ""; // No Bearer token for Gemini
        }
        else if (isAnthropic)
        {
            // Anthropic Claude format
            json messages = json::array();
            messages.push_back({
                {"role", "user"},
                {"content", engineContext}
            });

            const int historyStart = (int)s.Messages.size() > 12 ? (int)s.Messages.size() - 12 : 0;
            for (int i = historyStart; i < (int)s.Messages.size(); ++i)
            {
                messages.push_back({
                    {"role", s.Messages[i].FromUser ? "user" : "assistant"},
                    {"content", SanitizeUtf8(s.Messages[i].Text)}
                });
            }

            body["model"] = s.Model;
            body["max_tokens"] = 4096;
            body["messages"] = messages;
        }
        else
        {
            // OpenAI format (default)
            json messages = json::array();
            messages.push_back({
                {"role", "system"},
                {"content", engineContext}
            });

            const int historyStart = (int)s.Messages.size() > 12 ? (int)s.Messages.size() - 12 : 0;
            for (int i = historyStart; i < (int)s.Messages.size(); ++i)
            {
                messages.push_back({
                    {"role", s.Messages[i].FromUser ? "user" : "assistant"},
                    {"content", SanitizeUtf8(s.Messages[i].Text)}
                });
            }

            body["model"] = s.Model;
            body["temperature"] = 0.2;
            body["messages"] = messages;

            if (endpoint.find("openai.com") != std::string::npos)
                body["response_format"] = { {"type", "json_object"} };
        }

        std::string payload;
        try
        {
            payload = body.dump();
        }
        catch (const std::exception& error)
        {
            s.RequestInFlight = false;
            s.Messages.push_back({ false, std::string("Nao foi possivel preparar a requisicao da IA: ") + error.what() });
            return;
        }

        std::thread([=]()
        {
            PanelState& state = State();
            std::string error;
            std::string response;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                error.clear();
                response = AIHttpPostJson(finalEndpoint, finalApiKey, payload, error);
                if (error.empty())
                    break;

                const bool retryable =
                    error.rfind("HTTP 429", 0) == 0 ||
                    error.rfind("HTTP 500", 0) == 0 ||
                    error.rfind("HTTP 502", 0) == 0 ||
                    error.rfind("HTTP 503", 0) == 0 ||
                    error.rfind("HTTP 504", 0) == 0;
                if (!retryable || attempt == 2)
                    break;

                // Backoff: 1,5 s e 3 s. 503/429 normalmente são transitórios.
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1500 * (1 << attempt)));
            }

            PendingResult result;
            if (!error.empty())
            {
                result.Error = error;
            }
            else
            {
                const std::string content = ParseApiContent(response, error);
                if (!error.empty())
                {
                    result.Error = error;
                }
                else
                {
                    result.Reply = content;
                    result.Actions = ExtractActionsPayload(content, result.Reply);
                }
            }
            result.Ready = true;

            std::lock_guard<std::mutex> lock(state.ResultMutex);
            state.Result = std::move(result);
        }).detach();
    }

    void ConsumeResult(Application& app)
    {
        PanelState& s = State();
        PendingResult result;
        {
            std::lock_guard<std::mutex> lock(s.ResultMutex);
            if (!s.Result.Ready)
                return;
            result = std::move(s.Result);
            s.Result = PendingResult{};
        }

        s.RequestInFlight = false;
        if (!result.Error.empty())
        {
            std::string userMsg = result.Error;
            if (result.Error.find("503") != std::string::npos || result.Error.find("high demand") != std::string::npos || result.Error.find("UNAVAILABLE") != std::string::npos)
            {
                userMsg = "[Aviso dos Servidores do Google]\n"
                          "O modelo da IA esta temporariamente com alta demanda (Erro 503). "
                          "Os servidores do Google costumam normalizar em poucos segundos.\n"
                          "Por favor, aguarde cerca de 5 a 10 segundos e tente enviar novamente!";
            }
            else if (result.Error.find("429") != std::string::npos || result.Error.find("RESOURCE_EXHAUSTED") != std::string::npos)
            {
                userMsg = "[Limite de Requisicoes Atingido (Erro 429)]\n"
                          "O limite gratuito por minuto da sua chave foi temporariamente excedido. "
                          "Aguarde cerca de 30 segundos antes de enviar uma nova mensagem.";
            }
            else if (result.Error.find("404") != std::string::npos || result.Error.find("NOT_FOUND") != std::string::npos)
            {
                userMsg = "[Modelo Nao Encontrado (Erro 404)]\n"
                          "O modelo configurado nao foi encontrado na API. "
                          "Verifique se o nome do modelo esta correto (ex: gemini-2.5-flash ou gemini-1.5-flash).";
            }
            else if (result.Error.find("400") != std::string::npos && result.Error.find("API_KEY_INVALID") != std::string::npos)
            {
                userMsg = "[Chave de API Invalida]\n"
                          "A chave de API informada nao e valida. Verifique a chave no menu de configuracao da IA.";
            }

            s.Messages.push_back({ false, userMsg });
            return;
        }

        s.Messages.push_back({ false, result.Reply.empty() ? "(sem texto)" : result.Reply });
        ApplyActions(app, result.Actions);
    }
}

void AIPanel::Draw(Application& app)
{
    LoadSettings();
    ConsumeResult(app);

    ImGui::Begin(L("ai.title"), nullptr, ImGuiWindowFlags_NoCollapse);
    app.SetInspectorWidth(ImGui::GetWindowWidth());

    if (State().Messages.empty())
    {
        State().Messages.push_back({
            false,
            "Oi. Eu sou o Nexo local. Vejo a cena e a estrutura da Marae e tento construir o que você pedir. "
            "Posso pesquisar uma referência visual em Imagens e começar por uma receita segura; por exemplo: \"cria uma lanterna 3D com Spot Light\"."
        });
    }

    ImGui::TextUnformatted(L("ai.assistant"));
    ImGui::TextDisabled("%s", L("ai.description"));
    bool localModeChanged = ImGui::Checkbox(UI("Nexo local (limitado, sem modelo/API)"), &State().UseLocalNexo);
    ImGui::SameLine();
    bool searchModeChanged = ImGui::Checkbox(UI("Manter referência visual aberta (Imagens)"), &State().OpenSearchInBrowser);
    if (localModeChanged || searchModeChanged)
        SaveSettings();
    if (State().UseLocalNexo)
        ImGui::TextDisabled("Nexo: IA imatura e especializada; constrói receitas seguras e não responde qualquer pergunta.");
    else
        ImGui::TextDisabled("Modo IA externa: usa a chave configurada e pode planejar ações estruturadas.");
    ImGui::Text(UI("Ciclo visual: %s"), State().VisualPass == 0 ? "referência/construção" : "refinamento local");
    if (ImGui::Button(UI("Refinar passe visual"), ImVec2(170.0f, 0.0f)))
        NexoVisualRefine(app);
    ImGui::SameLine();
    ImGui::TextDisabled("Passe %d", State().VisualPass);
    ImGui::TextWrapped(UI("%s"), State().VisualStatus.c_str());
    ImGui::Separator();

    const bool missingKey = !State().UseLocalNexo && State().ApiKey[0] == '\0';
    if (ImGui::CollapsingHeader(L("ai.api"), missingKey ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        bool settingsChanged = false;

        // Provider selection
        const char* providers[] = {"OpenAI", "Gemini", "Anthropic", "Custom"};
        int currentProvider = State().ProviderIndex;
        if (ImGui::Combo(L("common.provider"), &currentProvider, providers, 4))
        {
            State().ProviderIndex = currentProvider;
            ApplyProviderConfig(currentProvider);
            settingsChanged = true;
        }

        if (ImGui::InputText(L("common.api_key"), State().ApiKey, sizeof(State().ApiKey), ImGuiInputTextFlags_Password))
            settingsChanged = true;
        if (ImGui::InputText(L("common.model"), State().Model, sizeof(State().Model)))
            settingsChanged = true;
        if (ImGui::InputText(L("common.endpoint"), State().Endpoint, sizeof(State().Endpoint)))
            settingsChanged = true;
        if (settingsChanged)
            SaveSettings();
        ImGui::TextDisabled("%s", L("ai.configure_hint"));
    }

    ImGui::Separator();

    const float footer = 118.0f;
    const float historyHeight = (std::max)(120.0f, ImGui::GetContentRegionAvail().y - footer);

    ImGui::BeginChild("AIHistory", ImVec2(0.0f, historyHeight), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < State().Messages.size(); ++i)
    {
        const ChatMessage& msg = State().Messages[i];
        ImGui::PushID((int)i);
        ImGui::TextColored(
            msg.FromUser ? ImVec4(0.70f, 0.85f, 1.0f, 1.0f) : ImVec4(0.85f, 0.92f, 0.75f, 1.0f),
            msg.FromUser ? "Voce" : "IA");
        // Campos somente leitura mantêm o histórico copiável: o usuário pode
        // selecionar trechos com o mouse/teclado e usar Ctrl+C/Ctrl+V, sem
        // permitir que a resposta da IA seja alterada acidentalmente.
        const float wrapWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
        const ImVec2 messageSize = ImGui::CalcTextSize(msg.Text.c_str(), nullptr, false, wrapWidth);
        ImGui::InputTextMultiline(
            UI("##AIMessage"),
            const_cast<char*>(msg.Text.c_str()),
            msg.Text.size() + 1,
            ImVec2(-1.0f, (std::max)(ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f, messageSize.y + ImGui::GetStyle().FramePadding.y * 2.0f)),
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
        ImGui::Spacing();
        ImGui::PopID();
    }

    if (State().RequestInFlight)
        ImGui::TextDisabled("%s", L("ai.thinking"));

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::TextUnformatted(L("common.question"));
    ImGui::InputTextMultiline(UI("##AIPrompt"), State().Prompt, sizeof(State().Prompt), ImVec2(-1.0f, 64.0f));
    const bool promptFocused = ImGui::IsItemFocused();

    const bool canSend = !State().RequestInFlight && State().Prompt[0] != '\0';
    if (!canSend)
        ImGui::BeginDisabled();
    if (ImGui::Button(L("common.send"), ImVec2(120.0f, 0.0f)))
        StartRequest(app);
    if (!canSend)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(L("common.clear")))
        State().Messages.clear();

    if (promptFocused && canSend && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        StartRequest(app);

    ImGui::End();
}
