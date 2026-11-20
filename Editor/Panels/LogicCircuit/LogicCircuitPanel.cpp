#include "LogicCircuitPanel.h"

#include "../../../Core/Application.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define GUID Windows_SDK_GUID
#include <windows.h>
#undef GUID
#endif

namespace
{
    enum class GateType : int
    {
        Input, Constant, Not, And, Or, Nand, Nor, Xor, Xnor, Add, Subtract, Output
    };

    struct GateNode
    {
        uint64_t id = 0;
        GateType type = GateType::And;
        ImVec2 position{ 80.0f, 80.0f };
        bool bit = false;
        float number = 0.0f;
        float result = 0.0f;
        bool valid = true;
    };

    struct GateWire { uint64_t source = 0, target = 0; int input = 0; };
    std::vector<GateNode> s_Nodes;
    std::vector<GateWire> s_Wires;
    std::filesystem::path s_ProjectPath, s_GraphPath;
    uint64_t s_NextID = 1;
    bool s_Loaded = false, s_Dirty = false, s_ShowHelp = false;
    int s_DragNode = -1;
    ImVec2 s_DragOffset{ 0.0f, 0.0f };
    uint64_t s_LinkSource = 0;

    constexpr float NodeW = 164.0f;
    constexpr float NodeH = 96.0f;

    const char* GateName(GateType type)
    {
        switch (type)
        {
        case GateType::Input: return "ENTRADA"; case GateType::Constant: return "VALOR";
        case GateType::Not: return "NOT"; case GateType::And: return "AND";
        case GateType::Or: return "OR"; case GateType::Nand: return "NAND";
        case GateType::Nor: return "NOR"; case GateType::Xor: return "XOR";
        case GateType::Xnor: return "XNOR"; case GateType::Add: return "SOMAR";
        case GateType::Subtract: return "SUBTRAIR"; case GateType::Output: return "SAÍDA";
        }
        return "PORTA";
    }

    int InputCount(GateType type)
    {
        if (type == GateType::Not || type == GateType::Output) return 1;
        if (type == GateType::And || type == GateType::Or || type == GateType::Nand ||
            type == GateType::Nor || type == GateType::Xor || type == GateType::Xnor ||
            type == GateType::Add || type == GateType::Subtract) return 2;
        return 0;
    }

    GateNode* FindNode(uint64_t id)
    {
        auto it = std::find_if(s_Nodes.begin(), s_Nodes.end(), [id](const GateNode& n) { return n.id == id; });
        return it == s_Nodes.end() ? nullptr : &*it;
    }

    void LoadGraph(const std::filesystem::path& projectPath)
    {
        s_ProjectPath = projectPath;
        s_GraphPath.clear(); s_Nodes.clear(); s_Wires.clear(); s_NextID = 1;
        s_DragNode = -1; s_LinkSource = 0; s_Dirty = false; s_Loaded = true;
        if (s_ProjectPath.empty()) return;
        s_GraphPath = s_ProjectPath / "Assets" / "Logic" / "CircuitGraph.mlogic";
        std::ifstream file(s_GraphPath);
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream row(line); char kind = 0; row >> kind;
            if (kind == 'N')
            {
                GateNode node; int type = 0, bit = 0;
                if (row >> node.id >> type >> node.position.x >> node.position.y >> bit >> node.number &&
                    type >= 0 && type <= static_cast<int>(GateType::Output))
                {
                    node.type = static_cast<GateType>(type); node.bit = bit != 0;
                    s_Nodes.push_back(node); s_NextID = std::max(s_NextID, node.id + 1);
                }
            }
            else if (kind == 'C')
            {
                GateWire wire;
                if (row >> wire.source >> wire.target >> wire.input && wire.input >= 0 && wire.input < 2)
                    s_Wires.push_back(wire);
            }
        }
        // Descarta conexões quebradas sem modificar arquivos até o usuário editar.
        s_Wires.erase(std::remove_if(s_Wires.begin(), s_Wires.end(), [](const GateWire& w)
        {
            const GateNode* src = FindNode(w.source); const GateNode* dst = FindNode(w.target);
            return !src || !dst || w.input >= InputCount(dst->type) || src->type == GateType::Output;
        }), s_Wires.end());
    }

    void SaveGraph()
    {
        if (!s_Dirty || s_GraphPath.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(s_GraphPath.parent_path(), ec);
        if (ec) return;
        const std::filesystem::path temp = s_GraphPath.string() + ".tmp";
        std::ofstream file(temp, std::ios::trunc);
        if (!file) return;
        file << "MARAE_LOGIC_V1\n";
        for (const auto& n : s_Nodes)
            file << "N " << n.id << ' ' << static_cast<int>(n.type) << ' ' << n.position.x << ' '
                 << n.position.y << ' ' << (n.bit ? 1 : 0) << ' ' << n.number << '\n';
        for (const auto& w : s_Wires) file << "C " << w.source << ' ' << w.target << ' ' << w.input << '\n';
        file.close(); if (!file) return;
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), s_GraphPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return;
#else
        std::filesystem::rename(temp, s_GraphPath, ec);
        if (ec) return;
#endif
        s_Dirty = false;
    }

    bool EvaluateNode(uint64_t id, std::unordered_map<uint64_t, int>& state)
    {
        GateNode* node = FindNode(id);
        if (!node) return false;
        int& mark = state[id];
        if (mark == 2) return node->valid;
        if (mark == 1) { node->valid = false; return false; }
        mark = 1; node->valid = true;
        if (node->type == GateType::Input) node->result = node->bit ? 1.0f : 0.0f;
        else if (node->type == GateType::Constant) node->result = node->number;
        else
        {
            float input[2] = { 0.0f, 0.0f };
            for (int pin = 0; pin < InputCount(node->type); ++pin)
            {
                auto wire = std::find_if(s_Wires.begin(), s_Wires.end(), [&](const GateWire& w)
                    { return w.target == id && w.input == pin; });
                if (wire == s_Wires.end()) { node->valid = false; continue; }
                if (!EvaluateNode(wire->source, state)) node->valid = false;
                if (GateNode* src = FindNode(wire->source)) input[pin] = src->result;
            }
            const bool a = input[0] >= 0.5f, b = input[1] >= 0.5f;
            switch (node->type)
            {
            case GateType::Not: node->result = !a ? 1.0f : 0.0f; break;
            case GateType::And: node->result = a && b ? 1.0f : 0.0f; break;
            case GateType::Or: node->result = a || b ? 1.0f : 0.0f; break;
            case GateType::Nand: node->result = !(a && b) ? 1.0f : 0.0f; break;
            case GateType::Nor: node->result = !(a || b) ? 1.0f : 0.0f; break;
            case GateType::Xor: node->result = a != b ? 1.0f : 0.0f; break;
            case GateType::Xnor: node->result = a == b ? 1.0f : 0.0f; break;
            case GateType::Add: node->result = input[0] + input[1]; break;
            case GateType::Subtract: node->result = input[0] - input[1]; break;
            case GateType::Output: node->result = input[0]; break;
            default: break;
            }
        }
        mark = 2;
        return node->valid;
    }

    void EvaluateGraph()
    {
        std::unordered_map<uint64_t, int> state; state.reserve(s_Nodes.size());
        for (auto& n : s_Nodes) { n.valid = true; n.result = 0.0f; }
        for (const auto& n : s_Nodes) EvaluateNode(n.id, state);
    }

    void AddNode(GateType type, const ImVec2& position)
    {
        GateNode node; node.id = s_NextID++; node.type = type; node.position = position;
        s_Nodes.push_back(node); s_Dirty = true;
    }
}

void LogicCircuitPanel::Draw(Application& app)
{
    if (!app.Is2DWorldMode()) return;
    const auto& config = app.GetProjectConfig();
    if (!s_Loaded || s_ProjectPath.string() != config.ProjectPath) LoadGraph(config.ProjectPath);
    if (!ImGui::Begin("Laboratório de Lógica 2D")) { ImGui::End(); return; }

    ImGui::TextDisabled("Circuitos 2D · simulação combinacional em tempo real · salvamento no projeto");
    const struct Tool { GateType type; const char* label; } tools[] = {
        {GateType::Input, "+ Entrada"}, {GateType::Constant, "+ Valor"}, {GateType::Not, "NOT"},
        {GateType::And, "AND"}, {GateType::Or, "OR"}, {GateType::Nand, "NAND"},
        {GateType::Nor, "NOR"}, {GateType::Xor, "XOR"}, {GateType::Xnor, "XNOR"},
        {GateType::Add, "+ Somar"}, {GateType::Subtract, "− Subtrair"}, {GateType::Output, "+ Saída"}
    };
    for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); ++i)
    {
        if (i && i % 6) ImGui::SameLine();
        if (ImGui::SmallButton(tools[i].label))
        {
            const float x = 48.0f + static_cast<float>(s_Nodes.size() % 4) * 194.0f;
            const float y = 48.0f + static_cast<float>(s_Nodes.size() / 4) * 138.0f;
            AddNode(tools[i].type, ImVec2(x, y));
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Limpar circuito"))
    { s_Nodes.clear(); s_Wires.clear(); s_LinkSource = 0; s_Dirty = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Ajuda")) s_ShowHelp = !s_ShowHelp;
    if (s_ShowHelp)
        ImGui::TextWrapped("Conecte o pino verde de saída ao pino azul de entrada. Arraste o cabeçalho para mover blocos. Clique em ENTRADA para alternar 0/1; nos blocos VALOR use + e - para ajustar. Botão direito em um bloco para excluir. SOMAR/SUBTRAIR usam valores numéricos; as portas lógicas tratam 0 como falso e qualquer valor >= 0,5 como verdadeiro.");

    EvaluateGraph();
    const float canvasHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("##LogicCanvas", ImVec2(0.0f, canvasHeight), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 canvasMin = ImGui::GetWindowPos();
    const ImVec2 canvasMax(canvasMin.x + ImGui::GetWindowSize().x, canvasMin.y + ImGui::GetWindowSize().y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvasMin, canvasMax, true);
    constexpr float grid = 24.0f;
    const float startX = std::fmod(canvasMin.x, grid), startY = std::fmod(canvasMin.y, grid);
    for (float x = startX; x < ImGui::GetWindowSize().x; x += grid)
        draw->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), IM_COL32(50, 59, 74, 100));
    for (float y = startY; y < ImGui::GetWindowSize().y; y += grid)
        draw->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), IM_COL32(50, 59, 74, 100));

    auto inputPin = [&](const GateNode& n, int pin) { return ImVec2(canvasMin.x+n.position.x, canvasMin.y+n.position.y+50.0f+pin*25.0f); };
    auto outputPin = [&](const GateNode& n) { return ImVec2(canvasMin.x+n.position.x+NodeW, canvasMin.y+n.position.y+NodeH*0.5f); };
    for (const auto& wire : s_Wires)
    {
        GateNode* src = FindNode(wire.source); GateNode* dst = FindNode(wire.target);
        if (!src || !dst || wire.input >= InputCount(dst->type)) continue;
        const ImVec2 a=outputPin(*src), b=inputPin(*dst,wire.input), t(std::max(36.0f,std::abs(b.x-a.x)*0.45f),0);
        draw->AddBezierCubic(a,ImVec2(a.x+t.x,a.y),ImVec2(b.x-t.x,b.y),b,
            src->result>=0.5f?IM_COL32(73,220,157,255):IM_COL32(105,157,218,220),3.0f);
    }
    if (s_LinkSource)
        if (GateNode* src=FindNode(s_LinkSource))
        {
            const ImVec2 a=outputPin(*src), b=ImGui::GetIO().MousePos, t(std::max(36.0f,std::abs(b.x-a.x)*0.45f),0);
            draw->AddBezierCubic(a,ImVec2(a.x+t.x,a.y),ImVec2(b.x-t.x,b.y),b,IM_COL32(110,200,255,220),2.0f);
        }

    for (size_t i=0; i<s_Nodes.size(); ++i)
    {
        GateNode& node=s_Nodes[i]; const ImVec2 p(canvasMin.x+node.position.x,canvasMin.y+node.position.y), mx(p.x+NodeW,p.y+NodeH);
        const ImU32 accent=!node.valid?IM_COL32(235,93,94,255):(node.result>=0.5f?IM_COL32(74,213,151,255):IM_COL32(75,145,232,255));
        draw->AddRectFilled(p,mx,IM_COL32(29,35,47,248),8.0f); draw->AddRect(p,mx,accent,8.0f,0,2.0f);
        draw->AddRectFilled(p,ImVec2(mx.x,p.y+30),IM_COL32(39,48,64,250),8.0f,ImDrawFlags_RoundCornersTop);
        draw->AddText(ImVec2(p.x+12,p.y+8),IM_COL32(230,238,250,255),GateName(node.type));
        if (node.type==GateType::Input)
            draw->AddText(ImVec2(p.x+12,p.y+57),accent,node.bit?"BIT: 1":"BIT: 0");
        else if (node.type==GateType::Constant)
        {
            char value[48]{}; std::snprintf(value,sizeof(value),"VALOR: %.3g",node.number);
            draw->AddText(ImVec2(p.x+12,p.y+57),IM_COL32(190,203,220,255),value);
        }
        else
        {
            char value[48]{}; std::snprintf(value,sizeof(value),node.type==GateType::Output?"SAÍDA: %.3g":"RESULTADO: %.3g",node.result);
            draw->AddText(ImVec2(p.x+12,p.y+55),accent,value);
            if (!node.valid) draw->AddText(ImVec2(p.x+12,p.y+75),IM_COL32(255,135,135,255),"entrada ausente ou ciclo");
        }

        // Cabeçalho é a alça de movimentação; áreas de pinos/controle ficam separadas,
        // evitando que o hitbox do bloco engula as conexões.
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton(("##header"+std::to_string(node.id)).c_str(),ImVec2(NodeW,30));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            s_DragNode=static_cast<int>(i); s_DragOffset=ImVec2(ImGui::GetIO().MousePos.x-p.x,ImGui::GetIO().MousePos.y-p.y);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(("##menu"+std::to_string(node.id)).c_str());
        if (ImGui::BeginPopup(("##menu"+std::to_string(node.id)).c_str()))
        {
            if (ImGui::MenuItem("Excluir bloco"))
            {
                const auto id=node.id;
                s_Wires.erase(std::remove_if(s_Wires.begin(),s_Wires.end(),[id](const GateWire& w){return w.source==id||w.target==id;}),s_Wires.end());
                s_Nodes.erase(s_Nodes.begin()+static_cast<std::ptrdiff_t>(i)); s_Dirty=true; ImGui::EndPopup(); break;
            }
            ImGui::EndPopup();
        }

        for (int pin=0; pin<InputCount(node.type); ++pin)
        {
            const ImVec2 pos=inputPin(node,pin); draw->AddCircleFilled(pos,6,IM_COL32(103,174,255,255)); draw->AddCircle(pos,8,IM_COL32(218,234,255,220));
            ImGui::SetCursorScreenPos(ImVec2(pos.x-11,pos.y-11));
            ImGui::InvisibleButton(("##in"+std::to_string(node.id)+"_"+std::to_string(pin)).c_str(),ImVec2(22,22));
            if (s_LinkSource && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                const uint64_t source=s_LinkSource, target=node.id;
                s_Wires.erase(std::remove_if(s_Wires.begin(),s_Wires.end(),[&](const GateWire& w){return w.target==target&&w.input==pin;}),s_Wires.end());
                s_Wires.push_back({source,target,pin}); s_LinkSource=0; s_Dirty=true;
            }
        }
        if (node.type!=GateType::Output)
        {
            const ImVec2 pos=outputPin(node); draw->AddCircleFilled(pos,6,IM_COL32(73,220,157,255)); draw->AddCircle(pos,8,IM_COL32(218,255,238,220));
            ImGui::SetCursorScreenPos(ImVec2(pos.x-11,pos.y-11)); ImGui::InvisibleButton(("##out"+std::to_string(node.id)).c_str(),ImVec2(22,22));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) s_LinkSource=node.id;
        }
        if (node.type==GateType::Input)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x+10,p.y+48)); ImGui::InvisibleButton(("##toggle"+std::to_string(node.id)).c_str(),ImVec2(NodeW-24,36));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {node.bit=!node.bit;s_Dirty=true;}
        }
        else if (node.type==GateType::Constant)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x+80,p.y+51));
            if (ImGui::SmallButton(("-##"+std::to_string(node.id)).c_str())) {node.number=std::max(-999999.0f,node.number-1);s_Dirty=true;}
            ImGui::SameLine(); if (ImGui::SmallButton(("+##"+std::to_string(node.id)).c_str())) {node.number=std::min(999999.0f,node.number+1);s_Dirty=true;}
        }
    }

    if (s_DragNode>=0)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const ImVec2 m=ImGui::GetIO().MousePos; auto& pos=s_Nodes[static_cast<size_t>(s_DragNode)].position;
            const ImVec2 size = ImGui::GetWindowSize();
            const float maxX = std::max(12.0f, size.x - NodeW - 12.0f);
            const float maxY = std::max(12.0f, size.y - NodeH - 12.0f);
            pos=ImVec2(std::clamp(m.x-canvasMin.x-s_DragOffset.x,12.0f,maxX),
                       std::clamp(m.y-canvasMin.y-s_DragOffset.y,12.0f,maxY));
            s_Dirty=true;
        }
        else s_DragNode=-1;
    }
    if (s_LinkSource && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) s_LinkSource=0;
    draw->PopClipRect(); ImGui::EndChild(); SaveGraph(); ImGui::End();
}
