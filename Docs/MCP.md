# Servidor MCP da Marae

Para o passo a passo completo, exemplos de todos os argumentos, recursos e prompts, consulte [MCP_TOOL_TUTORIAL.md](MCP_TOOL_TUTORIAL.md).

O servidor local MCP permite que um cliente compatível com Model Context Protocol consulte e altere um projeto Marae. Ele usa `stdio` com JSON-RPC, não abre janela gráfica, não inicia o renderer e não conecta à internet. `stdout` é reservado às mensagens MCP; diagnósticos vão para `stderr`.

## Configuração

Compile a configuração Debug ou Release x64 da Marae. Configure o cliente MCP para iniciar o executável com `--mcp-server` e o caminho absoluto do projeto como argumento. Exemplo genérico:

```json
{
  "mcpServers": {
    "marae": {
      "command": "C:/caminho/para/MaraeEngine.exe",
      "args": [
        "--mcp-server",
        "C:/caminho/para/o/projeto"
      ]
    }
  }
}
```

O caminho deve apontar para a raiz do projeto, que contém `Assets`. Reinicie o cliente MCP depois de alterar a configuração. O processo do servidor permanece aberto enquanto o cliente mantiver a sessão `stdio`.

## O que o servidor expõe

### Ferramentas

- `project_info`: metadados básicos e lista resumida de cenas.
- `list_scenes`: arquivos `.scene` dentro de `Assets/Scenes`.
- `list_assets`: arquivos em `Assets`, com limite configurável de até 2.000 entradas.
- `inspect_scene`: nomes, primitivas, transformações e componentes serializados.
- `scene_create_entity`: cria uma forma geométrica e grava a cena.
- `scene_set_transform`: atualiza posição, quaternion e/ou escala por nome e ocorrência.
- `scene_delete_entity`: remove uma entidade; bloqueia remoção de pai que ainda tenha filhos.
- `read_script`: lê C# em `Assets/Scripts` ou na pasta legada `Scripts`.
- `create_script`: cria scripts apenas em `Assets/Scripts`; se não receber conteúdo, cria uma classe `ScriptBehaviour` mínima.
- `update_script`: atualiza script existente em `Assets/Scripts`.

Para nomes repetidos em uma cena, `occurrence` é baseado em zero e segue a ordem da hierarquia no arquivo. `rotationQuaternion` usa `[x, y, z, w]`.

### Recursos MCP

- `marae://project`
- `marae://scenes`
- `marae://assets`
- `marae://scene/<caminho-relativo>`

### Prompts MCP

- `create_marae_script`: orienta a criação de script usando a API da Marae.
- `review_marae_scene`: conduz uma revisão não destrutiva da cena.

## Proteções e limites

- Todos os caminhos são relativos à raiz configurada e são verificados depois de resolver links simbólicos. O servidor não oferece terminal, execução de comandos, acesso à rede nem leitura arbitrária fora do projeto.
- Alterações em cenas e atualizações de scripts mantêm uma cópia anterior ao lado do arquivo, com sufixo `.mcp.bak`; a gravação usa arquivo temporário e substituição atômica.
- Scripts novos e atualizados ficam restritos a `Assets/Scripts`. O servidor não sobrescreve scripts existentes com `create_script`.
- Os limites atuais são 1 MiB por mensagem JSON-RPC, 8 MiB por cena e 256 KiB por script lido ou gravado.
- O servidor edita arquivos no disco, não uma cena aberta na memória do editor. Salve e feche o projeto antes de pedir alterações em arquivos que o editor mantém abertos; reabra ou recarregue a cena depois. Caso contrário, uma gravação posterior do editor pode substituir a edição feita pelo MCP.
- O cliente de IA decide quando pedir autorização ao usuário para chamar ferramentas. Revise operações de escrita antes de autorizá-las.

## Protocolo

O servidor responde à revisão moderna `2026-07-28` por `server/discover` e metadados por requisição; também aceita a negociação clássica `initialize` até `2025-11-25` e revisões anteriores. Anuncia capacidades `tools`, `resources` e `prompts`, implementa `ping`, listagem/leitura, templates e chamadas. Mensagens de protocolo usam uma linha JSON por mensagem em `stdin`/`stdout`.

