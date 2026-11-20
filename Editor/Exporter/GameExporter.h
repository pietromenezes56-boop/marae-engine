#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>

// ---------------------------------------------------------------------------
// GameExporter
//
// Empacota os assets de um jogo Windows Desktop em staging limpo, valida os
// arquivos obrigatórios e só então publica a pasta final. Android e HTML só
// são aceitos quando houver runtimes reais para essas plataformas.
//
// Estrutura comum:
//   <OutputDir>/
//     MaraeGame.exe          ← somente no alvo Desktop
//     glfw3.dll              ← DLL GLFW
//     *.dll                  ← qualquer outra DLL ao lado do editor
//     Assets/
//       ...                  ← árvore Assets completa, sem filtro de extensão
//     Scripts/               ← scripts fora de Assets, quando houver
//     MaraeExport.manifest   ← manifest gerado com cena inicial
//
// O export é síncrono mas chamado via std::thread para não travar o editor.
// ---------------------------------------------------------------------------

enum class ExportPlatform
{
    Desktop,
    Android,
    HTML,
    MaraePlatform
};

struct ExportConfig
{
    std::string ProjectPath;       // raiz do projeto (ex. C:/MyGame)
    std::string OutputPath;        // pasta de destino do build
    std::string GameName;          // nome do jogo (usado no manifest)
    std::string Version = "1.0.0";
    std::string OfficialName;      // nome público exibido na Marae Platform
    std::string IconPath;           // ícone da janela, absoluto ou relativo ao projeto
    std::string Description;       // descrição pública do jogo
    std::string CoverImage;        // capa principal, absoluta ou relativa ao projeto
    std::vector<std::string> PresentationImages;
    std::string StartScene;        // caminho relativo da cena inicial (ex. Assets/Scenes/Main.scene)
    int         WindowWidth = 1280;
    int         WindowHeight = 720;
    int         WindowMode = 0; // 0 windowed, 1 borderless, 2 fullscreen
    bool        World2D = false;
    bool        OverwriteOutput = true;
    ExportPlatform Platform = ExportPlatform::Desktop;
};

struct ExportResult
{
    bool        Success = false;
    int         FilesCopied = 0;
    int         FilesSkipped = 0;
    std::string ErrorMessage;
    std::vector<std::string> Log;   // entradas de progresso
};

class GameExporter
{
public:
    // Callback chamado a cada step: (message, progress 0..1)
    using ProgressCallback = std::function<void(const std::string&, float)>;

    // Exportação síncrona — chame em thread separada para não travar ImGui.
    static ExportResult Export(const ExportConfig& config,
                               ProgressCallback    onProgress = nullptr);

    // Retorna o diretório padrão de output sugerido para o projeto.
    static std::string DefaultOutputPath(const std::string& projectPath,
                                         const std::string& gameName);

    // Retorna a pasta do executável da Marae em execução, usada como base do runtime.
    static std::string RuntimeSourceDir();

    // Testes determinísticos do pré-voo de exportação (sem gerar builds reais).
    static int RunRegressionTests();

    static const char* PlatformName(ExportPlatform platform);
};
