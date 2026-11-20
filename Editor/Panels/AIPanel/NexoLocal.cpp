#include "NexoLocal.h"

#include "../../../ThirdParty/json/json.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>

using json = nlohmann::json;

namespace
{
std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool Has(const std::string& text, const char* word)
{
    return text.find(word) != std::string::npos;
}

void Add(json& actions, const char* op, std::initializer_list<std::pair<const char*, json>> fields = {})
{
    json action = { {"op", op} };
    for (const auto& field : fields)
        action[field.first] = field.second;
    actions.push_back(action);
}
}

bool NexoLocalPlan(const std::string& request,
                   std::string& reply,
                   std::string& actionsJson,
                   std::string& searchQuery)
{
    const std::string text = Lower(request);
    if (text.size() < 3)
        return false;

    const bool asksToBuild = Has(text, "crie") || Has(text, "cria") || Has(text, "criar") ||
                             Has(text, "construa") || Has(text, "constrói") || Has(text, "construir") ||
                             Has(text, "faça") || Has(text, "faca") || Has(text, "faz") ||
                             Has(text, "fazer") || Has(text, "monte") || Has(text, "montar");
    if (!asksToBuild)
        return false;

    json actions = json::array();

    if (Has(text, "lanterna"))
    {
        const bool wantsSpot = Has(text, "spot") || Has(text, "foco") || Has(text, "holofote");
        Add(actions, "create_model", {{"name", "Lanterna"}});
        Add(actions, "create_cylinder", {{"name", "Corpo_Lanterna"}, {"x", 0.0f}, {"y", 1.0f}, {"z", 0.0f}, {"sx", 0.55f}, {"sy", 1.15f}, {"sz", 0.55f}, {"r", 0.08f}, {"g", 0.09f}, {"b", 0.11f}});
        Add(actions, "set_material", {{"name", "Corpo_Lanterna"}, {"type", "metal"}, {"metallic", 0.82f}, {"roughness", 0.24f}, {"r", 0.08f}, {"g", 0.09f}, {"b", 0.11f}});
        Add(actions, "create_torus", {{"name", "Aro_Lanterna"}, {"x", 0.0f}, {"y", 2.12f}, {"z", 0.0f}, {"sx", 0.72f}, {"sy", 0.72f}, {"sz", 0.72f}, {"r", 0.18f}, {"g", 0.19f}, {"b", 0.22f}});
        Add(actions, "set_material", {{"name", "Aro_Lanterna"}, {"type", "metal"}, {"metallic", 0.9f}, {"roughness", 0.18f}, {"r", 0.18f}, {"g", 0.19f}, {"b", 0.22f}});
        Add(actions, "create_sphere", {{"name", "Bulbo_Lanterna"}, {"x", 0.0f}, {"y", 1.92f}, {"z", 0.0f}, {"sx", 0.36f}, {"sy", 0.22f}, {"sz", 0.36f}, {"r", 1.0f}, {"g", 0.78f}, {"b", 0.32f}});
        Add(actions, "set_material", {{"name", "Bulbo_Lanterna"}, {"type", "neon"}, {"emission", 8.0f}, {"roughness", 0.2f}, {"r", 1.0f}, {"g", 0.65f}, {"b", 0.18f}});
        if (wantsSpot)
        {
            Add(actions, "create_spot_light", {{"name", "Luz_Spot_Lanterna"}, {"x", 0.0f}, {"y", 1.78f}, {"z", -0.22f}, {"intensity", 150.0f}, {"range", 14.0f}, {"r", 1.0f}, {"g", 0.78f}, {"b", 0.42f}, {"shadows", true}, {"innerAngle", 16.0f}, {"outerAngle", 32.0f}});
            Add(actions, "set_parent", {{"name", "Luz_Spot_Lanterna"}, {"parent", "Lanterna"}});
        }
        else
        {
            Add(actions, "create_point_light", {{"name", "Luz_Lanterna"}, {"x", 0.0f}, {"y", 1.78f}, {"z", 0.0f}, {"intensity", 110.0f}, {"range", 10.0f}, {"r", 1.0f}, {"g", 0.78f}, {"b", 0.42f}, {"shadows", true}});
            Add(actions, "set_parent", {{"name", "Luz_Lanterna"}, {"parent", "Lanterna"}});
        }
        Add(actions, "set_parent", {{"name", "Corpo_Lanterna"}, {"parent", "Lanterna"}});
        Add(actions, "set_parent", {{"name", "Aro_Lanterna"}, {"parent", "Lanterna"}});
        Add(actions, "set_parent", {{"name", "Bulbo_Lanterna"}, {"parent", "Lanterna"}});
        Add(actions, "select", {{"name", "Lanterna"}});
        searchQuery = wantsSpot ? "lanterna 3D realista com spot light" : "lanterna 3D realista modelagem iluminação";
        reply = "Sou o Nexo local: selecionei uma referência visual essencial, executei uma receita limitada de lanterna com materiais, corpo, aro, bulbo e luz. "
                + std::string(wantsSpot ? "Usei Spot Light porque você pediu. " : "Usei Point Light por padrão; Spot Light é opcional. ")
                + "Etapa atual: analisar a referência, construir e deixar a imagem aberta para a próxima comparação. Minha pesquisa sugerida é: " + searchQuery + ".";
        actionsJson = actions.dump();
        return true;
    }

    if (Has(text, "cubo") || Has(text, "cube"))
    {
        Add(actions, "create_cube", {{"name", "Cubo_Nexo"}, {"x", 0.0f}, {"y", 0.5f}, {"z", 0.0f}, {"r", 0.25f}, {"g", 0.55f}, {"b", 0.95f}});
        Add(actions, "set_material", {{"name", "Cubo_Nexo"}, {"type", "plastic"}, {"roughness", 0.35f}, {"r", 0.25f}, {"g", 0.55f}, {"b", 0.95f}});
        Add(actions, "select", {{"name", "Cubo_Nexo"}});
        searchQuery = "cubo 3D realista iluminação Marae Engine";
        reply = "Sou o Nexo local e só sei executar receitas pequenas. Selecionei uma referência visual, criei um cubo azul com material simples e deixei a busca pronta para comparação. Pesquisa sugerida: " + searchQuery + ".";
        actionsJson = actions.dump();
        return true;
    }

    // Fallback de execução: todo pedido de criação deve produzir uma tentativa
    // concreta. A receita genérica é intencionalmente conservadora; uma etapa
    // visual posterior poderá refiná-la quando houver referência disponível.
    Add(actions, "create_model", {{"name", "Objeto_Nexo"}});
    Add(actions, "create_cube", {{"name", "Base_Objeto_Nexo"}, {"x", 0.0f}, {"y", 0.5f}, {"z", 0.0f}, {"sx", 1.0f}, {"sy", 1.0f}, {"sz", 1.0f}, {"r", 0.32f}, {"g", 0.48f}, {"b", 0.78f}});
    Add(actions, "set_material", {{"name", "Base_Objeto_Nexo"}, {"type", "plastic"}, {"roughness", 0.42f}, {"r", 0.32f}, {"g", 0.48f}, {"b", 0.78f}});
    Add(actions, "set_parent", {{"name", "Base_Objeto_Nexo"}, {"parent", "Objeto_Nexo"}});
    Add(actions, "select", {{"name", "Objeto_Nexo"}});
    searchQuery = "modelo 3D realista " + request;
    if (searchQuery.size() > 180)
        searchQuery.resize(180);
    reply = "Sou o Nexo local. Não conheço uma receita específica para esse pedido, mas tentei construir uma base segura chamada Objeto_Nexo. "
            "Deixei a pesquisa visual essencial pronta para a próxima etapa de análise e refinamento: " + searchQuery + ".";
    actionsJson = actions.dump();
    return true;
}
