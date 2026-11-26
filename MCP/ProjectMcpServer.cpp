#include "ProjectMcpServer.h"

#include "../ThirdParty/json/json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    namespace fs = std::filesystem;
    using Json = nlohmann::json;

    constexpr size_t MaxMessageBytes = 1024 * 1024;
    constexpr size_t MaxTextFileBytes = 256 * 1024;
    constexpr size_t MaxSceneBytes = 8 * 1024 * 1024;
    constexpr size_t MaxListedFiles = 2000;
    constexpr const char* ModernProtocolVersion = "2026-07-28";
    constexpr const char* LatestHandshakeVersion = "2025-11-25";

    std::string PathUtf8(const fs::path& path)
    {
        const auto value = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    }

    fs::path Utf8Path(const std::string& value)
    {
        const auto utf8 = std::u8string(
            reinterpret_cast<const char8_t*>(value.data()), value.size());
        return fs::path(utf8);
    }

    std::string LowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    bool IsWithin(const fs::path& root, const fs::path& candidate)
    {
        const std::string rootText = LowerAscii(PathUtf8(root.lexically_normal()));
        const std::string candidateText = LowerAscii(PathUtf8(candidate.lexically_normal()));
        if (candidateText == rootText)
            return true;
        std::string prefix = rootText;
        if (!prefix.empty() && prefix.back() != '/')
            prefix.push_back('/');
        return candidateText.size() > prefix.size() &&
               candidateText.compare(0, prefix.size(), prefix) == 0;
    }

    fs::path ResolveProjectPath(const fs::path& root, const std::string& relative,
                                const std::string& requiredDirectory = {})
    {
        if (relative.empty())
            throw std::runtime_error("O caminho relativo não pode ficar vazio.");
        const fs::path relativePath = Utf8Path(relative);
        if (relativePath.is_absolute())
            throw std::runtime_error("O servidor aceita somente caminhos relativos ao projeto.");
        for (const fs::path& part : relativePath)
            if (part == ".." || part == ".")
                throw std::runtime_error("O caminho contém um segmento não permitido.");

        std::error_code error;
        const fs::path candidate = fs::weakly_canonical(root / relativePath, error);
        if (error || !IsWithin(root, candidate))
            throw std::runtime_error("O caminho sai da pasta do projeto ou não pode ser validado.");

        if (!requiredDirectory.empty())
        {
            const fs::path required = fs::weakly_canonical(root / Utf8Path(requiredDirectory), error);
            if (error || !IsWithin(required, candidate))
                throw std::runtime_error("O caminho não pertence à área permitida para esta operação.");
        }
        return candidate;
    }

    std::string ReadFile(const fs::path& path, size_t limit)
    {
        std::error_code error;
        const uintmax_t size = fs::file_size(path, error);
        if (error)
            throw std::runtime_error("Não foi possível obter o tamanho do arquivo: " + PathUtf8(path));
        if (size > limit)
            throw std::runtime_error("O arquivo excede o limite de leitura permitido.");
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Não foi possível abrir o arquivo: " + PathUtf8(path));
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    void AtomicWrite(const fs::path& destination, const std::string& content, bool keepBackup)
    {
        std::error_code error;
        fs::create_directories(destination.parent_path(), error);
        if (error)
            throw std::runtime_error("Não foi possível criar a pasta de destino: " + error.message());

        fs::path temporary = destination;
        temporary += ".mcp.tmp";
        fs::path backup = destination;
        backup += ".mcp.bak";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                throw std::runtime_error("Não foi possível abrir o arquivo temporário.");
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.flush();
            if (!file)
            {
                file.close();
                fs::remove(temporary, error);
                throw std::runtime_error("Falha ao gravar o conteúdo temporário.");
            }
        }

        if (keepBackup && fs::exists(destination, error) && !error)
        {
            fs::copy_file(destination, backup, fs::copy_options::overwrite_existing, error);
            if (error)
            {
                fs::remove(temporary, error);
                throw std::runtime_error("Não foi possível criar a cópia .mcp.bak.");
            }
        }

#ifdef _WIN32
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (fs::exists(destination) ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), flags))
        {
            const DWORD code = GetLastError();
            fs::remove(temporary, error);
            throw std::runtime_error("Não foi possível ativar o arquivo gravado (Windows " +
                                     std::to_string(code) + ").");
        }
#else
        fs::rename(temporary, destination, error);
        if (error)
        {
            fs::remove(temporary);
            throw std::runtime_error("Não foi possível ativar o arquivo gravado: " + error.message());
        }
#endif
    }

    bool ParseFloats(const std::string& line, size_t expected, std::vector<float>& values)
    {
        std::istringstream stream(line);
        values.clear();
        float value = 0.0f;
        while (stream >> value)
        {
            if (!std::isfinite(value))
                return false;
            values.push_back(value);
        }
        return values.size() == expected;
    }

    struct SceneEntityRecord
    {
        std::string name;
        std::string primitive = "Cube";
        std::vector<float> position;
        std::vector<float> rotation;
        std::vector<float> scale;
        std::vector<float> color;
        std::string meshPath;
        size_t firstLine = 0;
        size_t endLine = 0;
        size_t occurrence = 0;
        uint64_t savedId = 0;
        uint64_t parentId = 0;
        std::vector<std::string> components;
    };

    bool LooksLikeEntity(const std::vector<std::string>& lines, size_t index,
                         std::vector<float>* position = nullptr,
                         std::vector<float>* rotation = nullptr,
                         std::vector<float>* scale = nullptr,
                         std::vector<float>* color = nullptr)
    {
        if (index + 4 >= lines.size() || lines[index].empty() || lines[index][0] == '@')
            return false;
        std::vector<float> p, r, s, c;
        if (!ParseFloats(lines[index + 1], 3, p) ||
            !ParseFloats(lines[index + 2], 4, r) ||
            !ParseFloats(lines[index + 3], 3, s) ||
            !ParseFloats(lines[index + 4], 3, c))
            return false;
        if (position) *position = std::move(p);
        if (rotation) *rotation = std::move(r);
        if (scale) *scale = std::move(s);
        if (color) *color = std::move(c);
        return true;
    }

    std::vector<SceneEntityRecord> ParseScene(const std::vector<std::string>& lines)
    {
        std::vector<SceneEntityRecord> entities;
        std::set<std::string> names;
        for (size_t i = 0; i < lines.size();)
        {
            SceneEntityRecord entity;
            if (!LooksLikeEntity(lines, i, &entity.position, &entity.rotation,
                                 &entity.scale, &entity.color))
            {
                ++i;
                continue;
            }
            entity.name = lines[i];
            entity.firstLine = i;
            entity.meshPath = lines[i + 5];
            size_t end = i + 6;
            for (; end < lines.size(); ++end)
            {
                if (LooksLikeEntity(lines, end))
                    break;
                const std::string& metadata = lines[end];
                if (metadata.rfind("@primitive ", 0) == 0)
                {
                    std::istringstream value(metadata.substr(10));
                    value >> std::quoted(entity.primitive);
                }
                else if (metadata.rfind("@entity_id ", 0) == 0)
                    std::from_chars(metadata.data() + 11, metadata.data() + metadata.size(), entity.savedId);
                else if (metadata.rfind("@parent ", 0) == 0)
                    std::from_chars(metadata.data() + 8, metadata.data() + metadata.size(), entity.parentId);
                else if (metadata.rfind("@", 0) == 0)
                {
                    const size_t endTag = metadata.find(' ', 1);
                    entity.components.push_back(metadata.substr(1, endTag == std::string::npos ? endTag : endTag - 1));
                }
            }
            entity.endLine = end;
            for (const SceneEntityRecord& prior : entities)
                if (prior.name == entity.name)
                    ++entity.occurrence;
            names.insert(entity.name);
            entities.push_back(std::move(entity));
            i = end;
        }
        return entities;
    }

    std::vector<std::string> ReadSceneLines(const fs::path& path)
    {
        const std::string content = ReadFile(path, MaxSceneBytes);
        std::vector<std::string> lines;
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(std::move(line));
        }
        return lines;
    }

    std::string JoinLines(const std::vector<std::string>& lines)
    {
        std::string output;
        size_t size = 0;
        for (const std::string& line : lines) size += line.size() + 1;
        output.reserve(size);
        for (const std::string& line : lines)
        {
            output += line;
            output.push_back('\n');
        }
        return output;
    }

    Json VectorJson(const std::vector<float>& values)
    {
        return Json(values);
    }

    std::string FormatVector(const std::vector<float>& values)
    {
        std::ostringstream stream;
        stream << std::setprecision(9);
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i) stream << ' ';
            stream << values[i];
        }
        return stream.str();
    }

    std::vector<float> RequireVector(const Json& arguments, const char* key, size_t size)
    {
        if (!arguments.contains(key) || !arguments[key].is_array() || arguments[key].size() != size)
            throw std::runtime_error(std::string("O parâmetro '") + key + "' deve ser um vetor com " + std::to_string(size) + " números.");
        std::vector<float> values;
        values.reserve(size);
        for (const Json& item : arguments[key])
        {
            if (!item.is_number())
                throw std::runtime_error(std::string("O vetor '") + key + "' contém valor que não é número.");
            const float value = item.get<float>();
            if (!std::isfinite(value) || std::abs(value) > 1000000.0f)
                throw std::runtime_error(std::string("O vetor '") + key + "' contém valor fora do limite.");
            values.push_back(value);
        }
        return values;
    }

    std::string ReadProjectDescription(const fs::path& root)
    {
        const fs::path projectFile = root / "Project.marae";
        std::error_code error;
        if (!fs::is_regular_file(projectFile, error) || error)
            return {};
        return ReadFile(projectFile, 64 * 1024);
    }

    struct McpContext
    {
        fs::path root;
        bool initialized = false;
        std::string negotiatedVersion;

        fs::path ScenePath(const Json& args) const
        {
            const std::string relative = args.value("scenePath", std::string{});
            const fs::path path = ResolveProjectPath(root, relative, "Assets/Scenes");
            if (path.extension() != ".scene" || !fs::is_regular_file(path))
                throw std::runtime_error("A cena solicitada não existe ou não tem extensão .scene.");
            return path;
        }

        Json ProjectInfo() const
        {
            Json scenes = Json::array();
            std::error_code error;
            const fs::path sceneRoot = root / "Assets" / "Scenes";
            if (fs::is_directory(sceneRoot, error) && !error)
            {
                for (fs::recursive_directory_iterator it(sceneRoot, fs::directory_options::skip_permission_denied, error), end;
                     it != end && !error && scenes.size() < MaxListedFiles; it.increment(error))
                {
                    if (!it->is_regular_file(error) || error || it->path().extension() != ".scene")
                        continue;
                    scenes.push_back(PathUtf8(fs::relative(it->path(), root, error)));
                    if (error) error.clear();
                }
            }
            return Json{{"name", root.filename().string()},
                        {"root", PathUtf8(root)},
                        {"hasProjectFile", fs::is_regular_file(root / "Project.marae")},
                        {"scenes", scenes}};
        }

        Json ListScenes() const
        {
            Json result = Json::array();
            std::error_code error;
            const fs::path sceneRoot = root / "Assets" / "Scenes";
            if (!fs::is_directory(sceneRoot, error) || error)
                return result;
            for (fs::recursive_directory_iterator it(sceneRoot, fs::directory_options::skip_permission_denied, error), end;
                 it != end && !error && result.size() < MaxListedFiles; it.increment(error))
            {
                if (!it->is_regular_file(error) || error || it->path().extension() != ".scene")
                    continue;
                const fs::path relative = fs::relative(it->path(), root, error);
                if (!error)
                    result.push_back(Json{{"path", PathUtf8(relative)}, {"name", PathUtf8(it->path().stem())}});
                else
                    error.clear();
            }
            return result;
        }

        Json ListAssets(const Json& args) const
        {
            const std::string subdirectory = args.value("subdirectory", std::string("Assets"));
            const fs::path start = ResolveProjectPath(root, subdirectory, "Assets");
            if (!fs::is_directory(start))
                throw std::runtime_error("A pasta de recursos não existe.");
            const size_t requestedLimit = args.value("limit", static_cast<size_t>(500));
            const size_t limit = std::clamp<size_t>(requestedLimit, 1, MaxListedFiles);
            Json items = Json::array();
            std::error_code error;
            for (fs::recursive_directory_iterator it(start, fs::directory_options::skip_permission_denied, error), end;
                 it != end && !error && items.size() < limit; it.increment(error))
            {
                if (it->is_symlink(error) || !it->is_regular_file(error) || error)
                {
                    error.clear();
                    continue;
                }
                const fs::path path = fs::weakly_canonical(it->path(), error);
                if (error || !IsWithin(root / "Assets", path))
                {
                    error.clear();
                    continue;
                }
                const uintmax_t bytes = fs::file_size(path, error);
                if (error) { error.clear(); continue; }
                items.push_back(Json{{"path", PathUtf8(fs::relative(path, root, error))},
                                     {"extension", PathUtf8(path.extension())},
                                     {"bytes", bytes}});
                if (error) error.clear();
            }
            return Json{{"items", items}, {"limit", limit}, {"truncated", items.size() == limit}};
        }

        Json InspectScene(const Json& args) const
        {
            const fs::path path = ScenePath(args);
            const auto lines = ReadSceneLines(path);
            const auto entities = ParseScene(lines);
            Json output = Json::array();
            for (const SceneEntityRecord& entity : entities)
            {
                output.push_back(Json{{"name", entity.name}, {"occurrence", entity.occurrence},
                    {"savedId", entity.savedId}, {"parentSavedId", entity.parentId},
                    {"primitive", entity.primitive}, {"meshPath", entity.meshPath},
                    {"position", VectorJson(entity.position)}, {"rotationQuaternion", VectorJson(entity.rotation)},
                    {"scale", VectorJson(entity.scale)}, {"color", VectorJson(entity.color)},
                    {"components", entity.components}});
            }
            return Json{{"scenePath", PathUtf8(fs::relative(path, root))}, {"entities", output}};
        }

        Json CreateEntity(const Json& args) const
        {
            const fs::path path = ScenePath(args);
            std::string name = args.value("name", std::string{});
            if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || name[0] == '@')
                throw std::runtime_error("O nome precisa ter entre 1 e 128 caracteres e não pode começar com @.");
            const std::string primitive = args.value("primitive", std::string("Cube"));
            static const std::set<std::string> primitives{
                "Cube", "Sphere", "Cylinder", "Cone", "Capsule", "Plane", "Wedge", "Torus", "Triangle", "Quad"};
            if (!primitives.contains(primitive))
                throw std::runtime_error("Forma geométrica não reconhecida.");
            const std::vector<float> position = args.contains("position") ? RequireVector(args, "position", 3) : std::vector<float>{0,0,0};
            const std::vector<float> scale = args.contains("scale") ? RequireVector(args, "scale", 3) : std::vector<float>{1,1,1};
            const std::vector<float> color = args.contains("color") ? RequireVector(args, "color", 3) : std::vector<float>{1,1,1};
            auto lines = ReadSceneLines(path);
            const auto existing = ParseScene(lines);
            if (std::any_of(existing.begin(), existing.end(), [&](const auto& item) { return item.name == name; }))
                throw std::runtime_error("Já existe uma entidade com esse nome nesta cena; escolha um nome único.");

            std::ostringstream block;
            block << name << '\n' << FormatVector(position) << "\n0 0 0 1\n"
                  << FormatVector(scale) << '\n' << FormatVector(color)
                  << "\n\n@primitive \"" << primitive << "\""
                  << "\n@physics 1 1 0 1 0 1 0.05 0.65 1 0 0 0 1 0 0"
                  << "\n@entity 1 1 1 0 \"\" 1 0\n";
            if (!lines.empty() && !lines.back().empty())
                lines.emplace_back();
            std::istringstream blockStream(block.str());
            std::string line;
            while (std::getline(blockStream, line)) lines.push_back(std::move(line));
            AtomicWrite(path, JoinLines(lines), true);
            return Json{{"created", true}, {"name", name}, {"scenePath", PathUtf8(fs::relative(path, root))},
                        {"primitive", primitive}, {"position", position}, {"scale", scale}};
        }

        Json SetTransform(const Json& args) const
        {
            const fs::path path = ScenePath(args);
            const std::string name = args.value("name", std::string{});
            const size_t occurrence = args.value("occurrence", static_cast<size_t>(0));
            if (name.empty()) throw std::runtime_error("Informe o nome da entidade.");
            auto lines = ReadSceneLines(path);
            auto entities = ParseScene(lines);
            const auto found = std::find_if(entities.begin(), entities.end(), [&](const auto& entity) {
                return entity.name == name && entity.occurrence == occurrence;
            });
            if (found == entities.end()) throw std::runtime_error("A entidade não foi encontrada nessa cena.");
            Json updated;
            if (args.contains("position"))
            {
                auto value = RequireVector(args, "position", 3);
                lines[found->firstLine + 1] = FormatVector(value);
                updated["position"] = value;
            }
            if (args.contains("rotationQuaternion"))
            {
                auto value = RequireVector(args, "rotationQuaternion", 4);
                const float length = std::sqrt(value[0]*value[0] + value[1]*value[1] + value[2]*value[2] + value[3]*value[3]);
                if (length < 1e-6f) throw std::runtime_error("O quaternion de rotação não pode ter comprimento zero.");
                for (float& component : value) component /= length;
                lines[found->firstLine + 2] = FormatVector(value);
                updated["rotationQuaternion"] = value;
            }
            if (args.contains("scale"))
            {
                auto value = RequireVector(args, "scale", 3);
                if (std::any_of(value.begin(), value.end(), [](float part) { return std::abs(part) < 1e-6f; }))
                    throw std::runtime_error("A escala não pode conter zero.");
                lines[found->firstLine + 3] = FormatVector(value);
                updated["scale"] = value;
            }
            if (updated.empty()) throw std::runtime_error("Informe ao menos position, rotationQuaternion ou scale.");
            AtomicWrite(path, JoinLines(lines), true);
            return Json{{"updated", true}, {"name", name}, {"occurrence", occurrence}, {"transform", updated}};
        }

        Json DeleteEntity(const Json& args) const
        {
            const fs::path path = ScenePath(args);
            const std::string name = args.value("name", std::string{});
            const size_t occurrence = args.value("occurrence", static_cast<size_t>(0));
            auto lines = ReadSceneLines(path);
            auto entities = ParseScene(lines);
            const auto found = std::find_if(entities.begin(), entities.end(), [&](const auto& entity) {
                return entity.name == name && entity.occurrence == occurrence;
            });
            if (found == entities.end()) throw std::runtime_error("A entidade não foi encontrada nessa cena.");
            if (found->savedId != 0 && std::any_of(entities.begin(), entities.end(), [&](const auto& entity) {
                return entity.parentId == found->savedId;
            }))
                throw std::runtime_error("A entidade ainda tem filhos; remova ou reorganize os filhos antes.");
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(found->firstLine),
                        lines.begin() + static_cast<std::ptrdiff_t>(found->endLine));
            AtomicWrite(path, JoinLines(lines), true);
            return Json{{"deleted", true}, {"name", name}, {"occurrence", occurrence}};
        }

        Json ReadScript(const Json& args) const
        {
            const std::string relative = args.value("path", std::string{});
            fs::path path;
            try { path = ResolveProjectPath(root, relative, "Assets/Scripts"); }
            catch (const std::exception&)
            {
                path = ResolveProjectPath(root, relative, "Scripts");
            }
            if (path.extension() != ".cs" || !fs::is_regular_file(path))
                throw std::runtime_error("O script não existe ou não tem extensão .cs.");
            return Json{{"path", relative}, {"content", ReadFile(path, MaxTextFileBytes)}};
        }

        Json CreateScript(const Json& args) const
        {
            const std::string relative = args.value("path", std::string{});
            const fs::path path = ResolveProjectPath(root, relative, "Assets/Scripts");
            if (path.extension() != ".cs") throw std::runtime_error("O arquivo precisa ter extensão .cs.");
            if (fs::exists(path)) throw std::runtime_error("Esse script já existe; use update_script para substituí-lo com cópia de segurança.");
            std::string content = args.value("content", std::string{});
            if (content.empty())
            {
                std::string className = args.value("className", path.stem().string());
                if (className.empty() || !(std::isalpha(static_cast<unsigned char>(className[0])) || className[0] == '_') ||
                    !std::all_of(className.begin() + 1, className.end(), [](unsigned char ch) { return std::isalnum(ch) || ch == '_'; }))
                    throw std::runtime_error("className não é um identificador C# válido.");
                content = "using MaraeEngine;\n\npublic class " + className + " : ScriptBehaviour\n{\n";
                content += "    public override void OnStart()\n    {\n    }\n\n";
                content += "    public override void OnUpdate(float dt)\n    {\n    }\n}\n";
            }
            if (content.size() > MaxTextFileBytes) throw std::runtime_error("O script excede o limite de 256 KiB.");
            AtomicWrite(path, content, false);
            return Json{{"created", true}, {"path", relative}, {"bytes", content.size()}};
        }

        Json UpdateScript(const Json& args) const
        {
            const std::string relative = args.value("path", std::string{});
            const fs::path path = ResolveProjectPath(root, relative, "Assets/Scripts");
            if (path.extension() != ".cs" || !fs::is_regular_file(path)) throw std::runtime_error("O script existente não foi encontrado.");
            const std::string content = args.value("content", std::string{});
            if (content.empty()) throw std::runtime_error("O conteúdo não pode ficar vazio.");
            if (content.size() > MaxTextFileBytes) throw std::runtime_error("O script excede o limite de 256 KiB.");
            AtomicWrite(path, content, true);
            return Json{{"updated", true}, {"path", relative}, {"bytes", content.size()},
                        {"backup", relative + ".mcp.bak"}};
        }

        Json ToolCall(const std::string& name, const Json& args) const
        {
            if (name == "project_info") return ProjectInfo();
            if (name == "list_scenes") return ListScenes();
            if (name == "list_assets") return ListAssets(args);
            if (name == "inspect_scene") return InspectScene(args);
            if (name == "scene_create_entity") return CreateEntity(args);
            if (name == "scene_set_transform") return SetTransform(args);
            if (name == "scene_delete_entity") return DeleteEntity(args);
            if (name == "read_script") return ReadScript(args);
            if (name == "create_script") return CreateScript(args);
            if (name == "update_script") return UpdateScript(args);
            throw std::runtime_error("Ferramenta MCP desconhecida: " + name);
        }

        Json ReadResource(const std::string& uri) const
        {
            if (uri == "marae://project")
                return Json{{"uri", uri}, {"mimeType", "application/json"}, {"text", ProjectInfo().dump(2)}};
            if (uri == "marae://scenes")
                return Json{{"uri", uri}, {"mimeType", "application/json"}, {"text", ListScenes().dump(2)}};
            if (uri == "marae://assets")
                return Json{{"uri", uri}, {"mimeType", "application/json"}, {"text", ListAssets(Json{{"limit", MaxListedFiles}}).dump(2)}};
            constexpr std::string_view prefix = "marae://scene/";
            if (uri.rfind(prefix, 0) == 0)
            {
                const std::string relative = uri.substr(prefix.size());
                const fs::path path = ResolveProjectPath(root, relative, "Assets/Scenes");
                if (path.extension() != ".scene" || !fs::is_regular_file(path))
                    throw std::runtime_error("A cena solicitada não existe.");
                return Json{{"uri", uri}, {"mimeType", "text/plain"}, {"text", ReadFile(path, MaxSceneBytes)}};
            }
            throw std::runtime_error("Recurso Marae não encontrado.");
        }
    };

    Json TextToolResult(const Json& value)
    {
        return Json{{"content", Json::array({Json{{"type", "text"}, {"text", value.dump(2)}}})},
                    {"structuredContent", value}};
    }

    Json ToolDefinition(std::string name, std::string description, Json properties,
                        Json required = Json::array())
    {
        return Json{{"name", std::move(name)}, {"description", std::move(description)},
            {"inputSchema", Json{{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}}}};
    }

    Json ToolDefinitions()
    {
        const Json string = Json{{"type", "string"}};
        const Json number = Json{{"type", "number"}};
        const Json vec3 = Json{{"type", "array"}, {"items", number}, {"minItems", 3}, {"maxItems", 3}};
        const Json vec4 = Json{{"type", "array"}, {"items", number}, {"minItems", 4}, {"maxItems", 4}};
        Json tools = Json::array();
        tools.push_back(ToolDefinition("project_info", "Lê informações básicas do projeto Marae configurado.", Json::object()));
        tools.push_back(ToolDefinition("list_scenes", "Lista arquivos de cena .scene em Assets/Scenes.", Json::object()));
        tools.push_back(ToolDefinition("list_assets", "Lista recursos dentro de Assets, com subpasta e limite opcionais.",
            Json{{"subdirectory", string}, {"limit", Json{{"type", "integer"}, {"minimum", 1}, {"maximum", static_cast<int>(MaxListedFiles)}}}}));
        tools.push_back(ToolDefinition("inspect_scene", "Lê nomes, malhas, transformações e componentes serializados de uma cena.",
            Json{{"scenePath", string}}, Json::array({"scenePath"})));
        tools.push_back(ToolDefinition("scene_create_entity", "Cria primitivo geométrico em uma cena e salva cópia .mcp.bak.",
            Json{{"scenePath", string}, {"name", string}, {"primitive", Json{{"type", "string"}, {"enum", {"Cube","Sphere","Cylinder","Cone","Capsule","Plane","Wedge","Torus","Triangle","Quad"}}}},
                 {"position", vec3}, {"scale", vec3}, {"color", vec3}}, Json::array({"scenePath", "name"})));
        tools.push_back(ToolDefinition("scene_set_transform", "Altera posição, quaternion e/ou escala da entidade identificada por nome e ocorrência (base 0). Salva cópia .mcp.bak.",
            Json{{"scenePath", string}, {"name", string}, {"occurrence", Json{{"type", "integer"}, {"minimum", 0}}},
                 {"position", vec3}, {"rotationQuaternion", vec4}, {"scale", vec3}}, Json::array({"scenePath", "name"})));
        tools.push_back(ToolDefinition("scene_delete_entity", "Remove entidade de uma cena; bloqueia remoção de pai que ainda tenha filhos. Salva cópia .mcp.bak.",
            Json{{"scenePath", string}, {"name", string}, {"occurrence", Json{{"type", "integer"}, {"minimum", 0}}}}, Json::array({"scenePath", "name"})));
        tools.push_back(ToolDefinition("read_script", "Lê um script C# do projeto, limitado a Assets/Scripts ou Scripts legado.",
            Json{{"path", string}}, Json::array({"path"})));
        tools.push_back(ToolDefinition("create_script", "Cria um script C# em Assets/Scripts. Se content for omitido, gera uma classe ScriptBehaviour mínima.",
            Json{{"path", string}, {"className", string}, {"content", string}}, Json::array({"path"})));
        tools.push_back(ToolDefinition("update_script", "Substitui um script existente em Assets/Scripts e mantém a versão anterior em .mcp.bak.",
            Json{{"path", string}, {"content", string}}, Json::array({"path", "content"})));
        return tools;
    }

    Json ResourceDefinitions(const McpContext& context)
    {
        Json resources = Json::array({
            Json{{"uri", "marae://project"}, {"name", "Projeto Marae"}, {"description", "Informações do projeto configurado."}, {"mimeType", "application/json"}},
            Json{{"uri", "marae://scenes"}, {"name", "Cenas"}, {"description", "Lista de cenas dentro de Assets/Scenes."}, {"mimeType", "application/json"}},
            Json{{"uri", "marae://assets"}, {"name", "Recursos"}, {"description", "Lista limitada de arquivos da pasta Assets."}, {"mimeType", "application/json"}}
        });
        const Json scenes = context.ListScenes();
        for (const Json& scene : scenes)
        {
            const std::string path = scene.value("path", std::string{});
            resources.push_back(Json{{"uri", "marae://scene/" + path}, {"name", scene.value("name", path)},
                                     {"description", "Conteúdo serializado da cena " + path}, {"mimeType", "text/plain"}});
        }
        return resources;
    }

    Json PromptDefinitions()
    {
        return Json::array({
            Json{{"name", "create_marae_script"}, {"description", "Orienta a criação de um script C# compatível com a API da Marae."},
                 {"arguments", Json::array({Json{{"name", "goal"}, {"description", "O comportamento desejado para o script."}, {"required", true}}})}},
            Json{{"name", "review_marae_scene"}, {"description", "Orienta uma revisão de cena usando os recursos e ferramentas do servidor Marae."},
                 {"arguments", Json::array({Json{{"name", "scenePath"}, {"description", "Caminho relativo da cena em Assets/Scenes."}, {"required", true}}})}}
        });
    }

    Json GetPrompt(const McpContext& context, const Json& params)
    {
        const std::string name = params.value("name", std::string{});
        const Json arguments = params.value("arguments", Json::object());
        if (name == "create_marae_script")
        {
            const std::string goal = arguments.value("goal", std::string{});
            if (goal.empty()) throw std::runtime_error("O prompt create_marae_script exige o argumento goal.");
            const std::string text = "Crie um script C# para a Marae Engine com este objetivo: " + goal +
                "\nConsulte a ferramenta read_script quando precisar examinar scripts existentes. "
                "Use a API pública MaraeEngine e ScriptBehaviour, preserve nomes e arquivos existentes, "
                "e grave o resultado somente em Assets/Scripts com create_script ou update_script. "
                "Não invente métodos de API; explique dependências de componentes que o usuário precisa adicionar.";
            return Json{{"description", "Orientação para criar um script Marae."},
                {"messages", Json::array({Json{{"role", "user"}, {"content", Json{{"type", "text"}, {"text", text}}}}})}};
        }
        if (name == "review_marae_scene")
        {
            const std::string scenePath = arguments.value("scenePath", std::string{});
            const fs::path path = ResolveProjectPath(context.root, scenePath, "Assets/Scenes");
            if (path.extension() != ".scene" || !fs::is_regular_file(path))
                throw std::runtime_error("Informe uma cena existente dentro de Assets/Scenes.");
            const std::string text = "Revise a cena Marae " + scenePath +
                ". Primeiro use inspect_scene para entender entidades e componentes. "
                "Antes de alterar, descreva mudanças que possam remover conteúdo. Use apenas ferramentas scene_* "
                "para mudanças estruturais e observe que elas mantêm .mcp.bak.";
            return Json{{"description", "Revisão segura da cena."},
                {"messages", Json::array({Json{{"role", "user"}, {"content", Json{{"type", "text"}, {"text", text}}}}})}};
        }
        throw std::runtime_error("Prompt MCP não encontrado: " + name);
    }

    Json RpcError(const Json& id, int code, const std::string& message, Json data = Json())
    {
        Json error{{"code", code}, {"message", message}};
        if (!data.is_null()) error["data"] = std::move(data);
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}};
    }

    void WriteMessage(std::ostream& output, const Json& message)
    {
        output << message.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
        output.flush();
    }

    Json HandleRequest(McpContext& context, const Json& request)
    {
        if (!request.is_object())
            return RpcError(nullptr, -32600, "Requisição JSON-RPC inválida.");
        const Json id = request.value("id", Json());
        const std::string method = request.value("method", std::string{});
        const Json params = request.value("params", Json::object());
        const bool hasId = request.contains("id");
        if (request.value("jsonrpc", std::string{}) != "2.0" || method.empty() || !params.is_object())
            return hasId ? RpcError(id, -32600, "Requisição JSON-RPC inválida.") : Json();

        const Json requestMeta = params.value("_meta", Json::object());
        const bool isDiscover = method == "server/discover";
        const bool hasVersion = requestMeta.is_object() && requestMeta.contains("io.modelcontextprotocol/protocolVersion");
        const std::string requestVersion = hasVersion
            ? requestMeta.value("io.modelcontextprotocol/protocolVersion", std::string{}) : std::string{};
        const bool isModern = isDiscover || requestVersion == ModernProtocolVersion;
        auto responseFor = [&](Json result)
        {
            if (isModern)
            {
                result["resultType"] = "complete";
                result["_meta"] = Json{{"io.modelcontextprotocol/serverInfo", Json{
                    {"name", "marae-project-mcp"}, {"version", "1.0.0"}}}};
            }
            return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
        };

        try
        {
            if (!hasId)
            {
                if (method == "notifications/initialized") context.initialized = true;
                return Json();
            }
            if (isModern && (!hasId || (!id.is_string() && !id.is_number_integer())))
                return RpcError(id, -32602, "Requisições MCP modernas precisam de ID string ou inteiro.");
            if (hasVersion && requestVersion != ModernProtocolVersion)
                return RpcError(id, -32022, "Versão de protocolo não suportada.",
                    Json{{"supported", Json::array({ModernProtocolVersion})}, {"requested", requestVersion}});
            if (isDiscover)
            {
                if (!requestMeta.is_object() || !requestMeta.contains("io.modelcontextprotocol/clientCapabilities") ||
                    !requestMeta["io.modelcontextprotocol/clientCapabilities"].is_object())
                    return RpcError(id, -32602, "server/discover exige clientCapabilities no envelope _meta.");
                Json result{{"supportedVersions", Json::array({ModernProtocolVersion})},
                    {"capabilities", Json{{"tools", Json{{"listChanged", false}}},
                        {"resources", Json{{"listChanged", false}, {"subscribe", false}}},
                        {"prompts", Json{{"listChanged", false}}}}},
                    {"instructions", "Acesso local restrito ao projeto configurado. Escritas em cenas e scripts criam cópia .mcp.bak."},
                    {"ttlMs", 0}, {"cacheScope", "private"}};
                return responseFor(std::move(result));
            }
            if (isModern)
            {
                if (!requestMeta.is_object() || !requestMeta.contains("io.modelcontextprotocol/clientCapabilities") ||
                    !requestMeta["io.modelcontextprotocol/clientCapabilities"].is_object())
                    return RpcError(id, -32602, "A mensagem moderna precisa informar clientCapabilities em _meta.");
                if (method == "initialize")
                    return RpcError(id, -32601, "A revisão MCP 2026 usa server/discover no lugar de initialize.");
            }

            if (method == "initialize")
            {
                if (context.initialized)
                    return RpcError(id, -32600, "initialize já foi concluído nesta conexão.");
                const std::string requested = params.value("protocolVersion", std::string(LatestHandshakeVersion));
                static const std::set<std::string> supported{
                    "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
                if (!supported.contains(requested))
                    return RpcError(id, -32602, "Versão MCP de handshake não suportada: " + requested);
                context.negotiatedVersion = requested;
                return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", Json{
                    {"protocolVersion", requested},
                    {"capabilities", Json{{"tools", Json{{"listChanged", false}}},
                                           {"resources", Json{{"subscribe", false}, {"listChanged", false}}},
                                           {"prompts", Json{{"listChanged", false}}}}},
                    {"serverInfo", Json{{"name", "marae-project-mcp"}, {"title", "Marae Project MCP"}, {"version", "1.0.0"}}},
                    {"instructions", "Acesso local restrito ao projeto configurado. Escritas em cenas e scripts criam cópia .mcp.bak."}
                }}};
            }

            if (!isModern && !context.initialized && method != "notifications/initialized" && method != "notifications/cancelled" && method != "ping")
                return RpcError(id, -32002, "Envie initialize antes de usar recursos e ferramentas.");
            if (method == "notifications/initialized" || method == "notifications/cancelled" ||
                method == "notifications/progress" || method == "notifications/roots/list_changed")
            {
                if (method == "notifications/initialized") context.initialized = true;
                return Json();
            }
            if (method == "ping")
                return responseFor(Json::object());

            Json result;
            if (method == "tools/list") result = Json{{"tools", ToolDefinitions()}};
            else if (method == "tools/call")
            {
                const Json args = params.value("arguments", Json::object());
                try { result = TextToolResult(context.ToolCall(params.value("name", std::string{}), args)); }
                catch (const std::exception& error)
                {
                    result = Json{{"isError", true}, {"content", Json::array({Json{{"type", "text"}, {"text", error.what()}}})}};
                }
            }
            else if (method == "resources/list") result = Json{{"resources", ResourceDefinitions(context)}};
            else if (method == "resources/templates/list")
                result = Json{{"resourceTemplates", Json::array({Json{
                    {"uriTemplate", "marae://scene/{path}"}, {"name", "Cena Marae"},
                    {"description", "Conteúdo de uma cena .scene dentro de Assets/Scenes."},
                    {"mimeType", "text/plain"}}})}};
            else if (method == "resources/read")
            {
                const std::string uri = params.value("uri", std::string{});
                const Json resource = context.ReadResource(uri);
                result = Json{{"contents", Json::array({Json{{"uri", uri}, {"mimeType", resource["mimeType"]}, {"text", resource["text"]}}})}};
            }
            else if (method == "prompts/list") result = Json{{"prompts", PromptDefinitions()}};
            else if (method == "prompts/get") result = GetPrompt(context, params);
            else return RpcError(id, -32601, "Método MCP não implementado: " + method);

            if (!hasId) return Json();
            if (isModern && (method == "tools/list" || method == "resources/list" || method == "resources/templates/list" || method == "prompts/list" || method == "resources/read"))
            {
                result["ttlMs"] = 0;
                result["cacheScope"] = "private";
            }
            return responseFor(std::move(result));
        }
        catch (const std::exception& error)
        {
            return RpcError(id, -32602, error.what());
        }
    }
}

int ProjectMcpServer::Run(const std::filesystem::path& projectRoot,
                          std::istream& input,
                          std::ostream& output,
                          std::ostream& diagnostics)
{
    try
    {
        std::error_code error;
        const fs::path root = fs::canonical(projectRoot, error);
        if (error || !fs::is_directory(root) || !fs::is_directory(root / "Assets"))
        {
            diagnostics << "[MCP] A raiz informada não é um projeto Marae válido com pasta Assets.\n";
            return 2;
        }
        McpContext context;
        context.root = root;
        std::string line;
        while (std::getline(input, line))
        {
            if (line.size() > MaxMessageBytes)
            {
                WriteMessage(output, RpcError(nullptr, -32600, "Mensagem excede o limite de 1 MiB."));
                continue;
            }
            Json request;
            try { request = Json::parse(line); }
            catch (const Json::parse_error& parseError)
            {
                WriteMessage(output, RpcError(nullptr, -32700, std::string("JSON inválido: ") + parseError.what()));
                continue;
            }
            Json response = HandleRequest(context, request);
            if (!response.is_null())
                WriteMessage(output, response);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        diagnostics << "[MCP] Inicialização do servidor falhou: " << error.what() << '\n';
        return 2;
    }
}

