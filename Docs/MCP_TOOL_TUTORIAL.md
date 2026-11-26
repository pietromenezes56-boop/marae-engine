# Tutorial completo: ferramentas MCP da Marae Engine

Este guia ensina a conectar um cliente de IA ao servidor MCP local da Marae e a usar cada ferramenta para consultar ou editar um projeto. O servidor trabalha com os arquivos do projeto no computador; ele não abre a janela da Marae, não executa o jogo e não sincroniza alterações com uma cena que já esteja carregada no editor.

## 1. O que você precisa

- Uma compilação x64 da Marae Engine que contenha o modo `--mcp-server`.
- Um projeto Marae cuja raiz contenha a pasta `Assets`.
- Um cliente de IA que aceite servidores MCP por `stdio`.
- O caminho absoluto do executável e do projeto.

Use o caminho do executável que você compilou ou recebeu. A compilação isolada desta implementação gerou `MaraeEngine.exe` em uma pasta temporária de verificação, que não faz parte do repositório. Não aponte o servidor para `Assets`: informe a pasta raiz do projeto, que contém `Assets`.

## 2. Configurar o cliente MCP

Adicione uma entrada de servidor MCP à configuração do seu cliente. Este exemplo usa o formato comum de servidores `stdio`; alguns clientes colocam o mesmo bloco em uma tela de configurações em vez de um arquivo JSON:

```json
{
  "mcpServers": {
    "marae-projeto": {
      "command": "C:/caminho/para/MaraeEngine.exe",
      "args": [
        "--mcp-server",
        "C:/caminho/para/MeuProjeto"
      ]
    }
  }
}
```

No Windows, prefira barras `/` ou escape as barras invertidas (`\\`) dentro de JSON. Para cada projeto, crie uma entrada separada com seu próprio caminho. Reinicie ou recarregue os servidores MCP no cliente.

O processo precisa permanecer aberto enquanto a conversa usar o servidor. O protocolo passa por `stdin` e `stdout`; diagnósticos são enviados a `stderr`. Não configure o cliente para iniciar a Marae sem `--mcp-server`.

## 3. Primeiro fluxo recomendado

1. Peça à IA para chamar `project_info` e confirme o nome e o caminho do projeto.
2. Chame `list_scenes` para ver as cenas disponíveis.
3. Chame `inspect_scene` na cena desejada antes de pedir mudanças.
4. Faça uma mudança pequena por vez com uma ferramenta `scene_*`.
5. Leia a cena novamente com `inspect_scene` e confira o resultado.
6. Abra o projeto na Marae e confirme visualmente.

Antes de alterar arquivos, salve e feche o projeto no editor. O MCP escreve no disco; se o editor ainda mantiver a cena aberta, um salvamento posterior pode sobrescrever o que o servidor acabou de gravar.

## 4. Ferramentas disponíveis

Todos os nomes abaixo são os nomes exatos que o cliente MCP deve chamar. Argumentos não mencionados são opcionais.

### `project_info`

Retorna nome e raiz configurada do projeto, se existe `Project.marae` e uma lista resumida de cenas.

Argumentos: nenhum.

Use primeiro para confirmar que o cliente apontou para o projeto certo.

### `list_scenes`

Lista os arquivos `.scene` dentro de `Assets/Scenes`, incluindo caminho relativo e nome.

Argumentos: nenhum.

### `list_assets`

Lista arquivos de recursos dentro de `Assets` ou de uma subpasta dele. Cada item contém caminho relativo, extensão e tamanho em bytes.

| Argumento | Tipo | Padrão | Descrição |
|---|---|---:|---|
| `subdirectory` | texto | `Assets` | Pasta dentro de `Assets`, por exemplo `Assets/Textures`. |
| `limit` | inteiro | `500` | Máximo de itens; o servidor limita o valor a 2.000. |

Exemplo de argumentos:

```json
{"subdirectory":"Assets/Textures","limit":100}
```

### `inspect_scene`

Lê a estrutura serializada de uma cena: nomes, IDs persistidos, relação pai/filho, primitiva, caminho da malha, componentes, posição, quaternion, escala e cor.

| Argumento | Tipo | Obrigatório |
|---|---|---|
| `scenePath` | texto | sim |

O caminho deve ser relativo à raiz e apontar para um `.scene` em `Assets/Scenes`.

```json
{"scenePath":"Assets/Scenes/Main.scene"}
```

Entidades com nomes repetidos recebem `occurrence` começando em zero, na ordem da hierarquia serializada. Use `savedId` e `parentSavedId` para entender a organização quando disponíveis.

### `scene_create_entity`

Adiciona uma primitiva geométrica à cena e cria um backup da cena original.

| Argumento | Tipo | Obrigatório | Padrão |
|---|---|---|---|
| `scenePath` | texto | sim | — |
| `name` | texto | sim | — |
| `primitive` | texto | não | `Cube` |
| `position` | vetor de 3 números | não | `[0,0,0]` |
| `scale` | vetor de 3 números | não | `[1,1,1]` |
| `color` | vetor de 3 números | não | `[1,1,1]` |

Primitivas aceitas: `Cube`, `Sphere`, `Cylinder`, `Cone`, `Capsule`, `Plane`, `Wedge`, `Torus`, `Triangle` e `Quad`. O nome precisa ser único na cena, ter até 128 caracteres e não pode começar com `@`.

```json
{
  "scenePath":"Assets/Scenes/Main.scene",
  "name":"Plataforma",
  "primitive":"Cube",
  "position":[0,1,0],
  "scale":[4,0.25,4],
  "color":[0.2,0.7,0.35]
}
```

### `scene_set_transform`

Altera um ou mais valores de transformação de uma entidade existente. A ferramenta preserva os demais dados da cena e cria um backup antes de gravar.

| Argumento | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `scenePath` | texto | sim | Cena em `Assets/Scenes`. |
| `name` | texto | sim | Nome exato da entidade. |
| `occurrence` | inteiro | não | Ocorrência do nome repetido, começando em zero; padrão `0`. |
| `position` | vetor de 3 números | não | Nova posição XYZ. |
| `rotationQuaternion` | vetor de 4 números | não | Quaternion `[x,y,z,w]`; o servidor normaliza o valor. |
| `scale` | vetor de 3 números | não | Nova escala XYZ; nenhum eixo pode ser zero. |

Envie pelo menos um entre `position`, `rotationQuaternion` ou `scale`.

```json
{
  "scenePath":"Assets/Scenes/Main.scene",
  "name":"Plataforma",
  "occurrence":0,
  "position":[2,1,0],
  "scale":[5,0.25,4]
}
```

O MCP recebe e grava quaternion, não ângulos Euler. Se precisar converter rotação, peça à IA para calcular e normalizar o quaternion e confira a orientação na Marae depois.

### `scene_delete_entity`

Remove uma entidade da cena e cria um backup. Para evitar órfãos, o servidor recusa remover um pai que ainda tenha filhos.

| Argumento | Tipo | Obrigatório |
|---|---|---|
| `scenePath` | texto | sim |
| `name` | texto | sim |
| `occurrence` | inteiro | não, padrão `0` |

```json
{"scenePath":"Assets/Scenes/Main.scene","name":"Plataforma","occurrence":0}
```

Essa ação é destrutiva para a entidade escolhida. Revise o nome e a ocorrência antes de autorizar a chamada. Se houver filhos, remova-os primeiro ou reorganize a hierarquia no editor.

### `read_script`

Lê um arquivo C# `.cs` em `Assets/Scripts` ou na pasta legada `Scripts`.

| Argumento | Tipo | Obrigatório |
|---|---|---|
| `path` | texto | sim |

```json
{"path":"Assets/Scripts/CameraWASD.cs"}
```

### `create_script`

Cria um novo script em `Assets/Scripts`. Não sobrescreve um arquivo existente.

| Argumento | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `path` | texto | sim | Caminho relativo, com extensão `.cs`, dentro de `Assets/Scripts`. |
| `className` | texto | não | Nome C# da classe, usado quando `content` não foi fornecido. |
| `content` | texto | não | Arquivo C# completo. Se omitido, gera uma classe mínima `ScriptBehaviour`. |

```json
{
  "path":"Assets/Scripts/RotacionarObjeto.cs",
  "className":"RotacionarObjeto"
}
```

Se informar `content`, forneça o script inteiro, incluindo `using`, declaração da classe e chaves. A criação não compila nem anexa o script a uma entidade; faça isso no editor e compile os scripts depois.

### `update_script`

Substitui um `.cs` já existente em `Assets/Scripts`, preservando a versão anterior como `.mcp.bak`.

| Argumento | Tipo | Obrigatório |
|---|---|---|
| `path` | texto | sim |
| `content` | texto | sim |

```json
{
  "path":"Assets/Scripts/RotacionarObjeto.cs",
  "content":"using MaraeEngine;\n\npublic class RotacionarObjeto : ScriptBehaviour\n{\n    public override void OnUpdate(float dt)\n    {\n    }\n}\n"
}
```

O conteúdo não pode ficar vazio. Depois da edição, peça revisão ou leia o script com `read_script`, compile na Marae e corrija erros de compilação.

## 5. Recursos MCP para leitura

Recursos podem ser abertos pelo seletor de recursos do cliente, sem chamar uma ferramenta:

- `marae://project`: resumo do projeto.
- `marae://scenes`: lista de cenas.
- `marae://assets`: lista resumida de recursos.
- `marae://scene/Assets/Scenes/Main.scene`: conteúdo serializado da cena indicada.
- O template `marae://scene/{path}` permite escolher outra cena relativa dentro de `Assets/Scenes`.

O recurso de cena é somente leitura. Para transformar informações em alteração, use `scene_*`.

## 6. Prompts MCP incluídos

- `create_marae_script(goal)`: fornece à IA orientação para criar um script C# usando a API Marae, consultar scripts existentes e gravar somente em `Assets/Scripts`.
- `review_marae_scene(scenePath)`: orienta uma revisão não destrutiva. A IA deve inspecionar antes e descrever mudanças que removam conteúdo.

Prompts são modelos de instrução para o cliente de IA; eles não executam ações sozinhos.

## 7. Segurança, backups e limites

- O servidor só aceita uma raiz válida de projeto com pasta `Assets`.
- Caminhos de cenas e recursos são limitados às pastas previstas e verificados contra escapes da raiz, inclusive links simbólicos.
- Não há terminal, execução de comandos, acesso à rede ou leitura arbitrária do computador.
- Edições de cena e atualizações de script salvam a versão anterior em um arquivo `.mcp.bak`; a substituição usa um arquivo temporário.
- Scripts novos e atualizados ficam em `Assets/Scripts`; leitura também aceita `Scripts` legado.
- Limites: 1 MiB por mensagem MCP, 8 MiB por cena, 256 KiB por script e até 2.000 recursos em uma listagem.
- O servidor não executa nem compila scripts, não inicia Play, não altera componentes arbitrários e não atualiza a cena que já está aberta no editor.
- O cliente MCP controla a autorização das chamadas. Revise operações de escrita e exclusão antes de aprová-las.

## 8. Problemas comuns

**O cliente não encontra o servidor**

Confira o caminho do executável, se o argumento `--mcp-server` está antes do caminho do projeto e se a raiz informada contém `Assets`. Reinicie o servidor MCP no cliente após mudar a configuração.

**A IA vê o projeto errado**

Chame `project_info`; corrija a raiz na configuração do cliente se o nome/caminho não corresponder ao projeto esperado.

**Uma ferramenta não encontra a cena**

Use `list_scenes` e copie o caminho retornado exatamente. O caminho precisa estar abaixo de `Assets/Scenes` e terminar em `.scene`.

**A entidade não é encontrada ou a errada é alterada**

Execute `inspect_scene`, copie o nome exato e indique `occurrence` para nomes repetidos. O índice começa em zero.

**A alteração MCP desapareceu ao salvar no editor**

Feche o projeto antes de editar pelo MCP. Um editor que mantém a cena em memória pode gravar uma versão antiga por cima do arquivo. Reabra a cena depois da operação.

**Script criado não funciona no Play**

Criar um arquivo não o anexa a um objeto nem prova que o código compila. Abra o script, compile no editor, anexe o componente Script C# à entidade e confira as dependências da cena.

## 9. Exemplos de pedidos à IA

- “Confirme o projeto com `project_info`, liste as cenas e diga qual parece ser a cena principal. Não altere nada.”
- “Inspecione `Assets/Scenes/Main.scene` e resuma as entidades e componentes, sem gravar alterações.”
- “Na cena Main, crie uma plataforma chamada PlataformaTeste nas coordenadas [0, 1, 0], escala [4, 0.25, 4], e depois inspecione a cena para confirmar.”
- “Leia `Assets/Scripts/CameraWASD.cs`, explique o que faz e não atualize o arquivo.”
- “Revise a cena, proponha as alterações primeiro e só depois peça autorização para qualquer mudança destrutiva.”

## 10. Protocolo e compatibilidade

O servidor usa MCP por JSON-RPC sobre `stdio`, anuncia ferramentas, recursos e prompts, e não depende de serviço remoto. A revisão moderna usa `server/discover` e metadados por solicitação; a negociação clássica é aceita para clientes compatíveis com as versões documentadas em [`MCP.md`](MCP.md). A compilação do servidor foi confirmada; a interoperabilidade deve ser validada com o cliente MCP específico usado na máquina.

