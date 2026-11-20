#pragma once

class LegalNotice
{
public:
    // Renderiza a janela modal interativa de Termos de Uso e Direitos Comerciais
    static void DrawModal(bool* p_open);

    // Renderiza o painel de Termos de Uso e Direitos Comerciais embutido (ex: no Project Hub)
    static void DrawPanel();

    // Cria/atualiza os arquivos markdown na raiz do projeto
    static void EnsureLegalFilesExist();

    // Verifica se os arquivos externos correspondem à versão oficial.
    static bool IsIntegrityValid();

    // Retorna true somente depois de o usuário visualizar e aceitar
    // explicitamente os Termos de Uso e a Política de Privacidade.
    static bool DrawFirstRunAcceptance();
};
