# API C# da Marae Engine

Versão do contrato: **1.0** (`MaraeEngine.MaraeApi.Version`).

No editor integrado, abra **Script Editor → Guia C#** para consultar tópicos
por busca, ler exemplos e copiar trechos sem sair da Marae. Esta página é a
referência textual complementar; confira as assinaturas no SDK
`MaraeEngine.Scripting` e compile o projeto pelo editor.

O script é um componente da entidade da cena. A engine cria a instância quando o
Play começa, injeta `gameObject` e chama o ciclo de vida. O fluxo normal é:

`OnEnable` → `OnStart`/`Start` → `OnFixedUpdate`/`FixedUpdate` →
`OnUpdate`/`Update` → eventos de colisão → `OnDisable` → `OnDestroy`.

O runtime chama os callbacks `On*` e os aliases `Start`, `Update` e
`FixedUpdate`. Implemente somente um membro de cada par para evitar executar a
mesma lógica duas vezes. `OnUpdate(float dt)` é por frame;
`OnFixedUpdate(float fixedDt)` é apropriado para controlar Rigidbody. Use dt
para movimento independente da taxa de quadros. O gameplay executa em Play.

## Entidade e transform

Use `gameObject`, `transform`, `Entity.Find`, `Entity.FindWithTag` e
`Entity.Instantiate`. O `transform` permite posição, rotação, escala, movimento,
parenting e `LookAt`. O ID `gameObject.id` é estável enquanto a entidade existe.
`Entity.GetComponent<T>()` e `HasComponent<T>()` funcionam para as fachadas do
SDK (mesh, áudio, luz, material, script, física, UI, animação, câmera e luz
global); componente ausente retorna `null`. `Entity.Instantiate` clona entidade
e componentes; `Destroy` e `DestroyHierarchy` agendam remoção. `Find`,
`FindWithTag`, `FindGameObjectsWithTag` e `FindEntityByID` localizam entidades.

## Entrada e tempo

`Input.GetKey`, `GetKeyDown`, `GetKeyUp`, `GetMouseButton`, `GetAxisRaw` e
`GetAction`/`GetActionDown` consultam o mapa nativo. `Time.deltaTime`,
`Time.fixedDeltaTime`, `Time.time` e `Time.timeScale` são a base para movimento
independente do FPS. Use `Input.BindKey` e `BindMouseButton` para ações próprias.

## Física

`gameObject.rigidbody` expõe gravidade, modo estático, colisão contínua,
congelamento de rotação, massa, restituição, atrito, velocidade e impulso.
`OnCollisionEnter/Stay/Exit` recebem contato, normal e impulso; triggers usam
`OnTriggerEnter/Stay/Exit`.
Os callbacks são encaminhados pelo runtime nativo quando emitidos pela simulação;
configure os colisores e valide na cena concreta. Trigger detecta sobreposição e
não aplica resposta sólida. Para corpos dinâmicos, prefira alterar velocidade ou
impulso no passo fixo em vez de sobrescrever a posição a cada frame.

## Áudio, luz e materiais

`Audio.Play("id_do_audio")` toca uma fonte importada pelo ID do asset.
`GetComponent<AudioComponent>()` controla a fonte anexada. `LightComponent`
permite ativação, cor, intensidade, alcance, sombras, temperatura e suavidade.
`MaterialComponent`/a ponte de componentes permite ajustar albedo, metálico e
suavidade quando o componente existir na entidade.

## UI 2D

Uma entidade UI expõe `gameObject.ui`. A API cobre `rect`, âncora, rotação,
transparência, raio da borda, fonte, tamanho da fonte, alinhamento, z-index,
texto, cores, imagem e teste de ponteiro. UI não precisa de mesh 3D.
Também expõe `enabled`, `type`, `clipsContent`, `isButton` e `IsPointerOver`. A
API atual não oferece evento `onClick` público no wrapper; combine
`IsPointerOver` com `Input.GetMouseButtonDown(0)` se precisar de interação por
polling.

## Animação

`gameObject.animation` expõe `clipId`, `clipName`, estado, tempo, velocidade,
peso e `Play`, `Pause`, `Stop` e `SetBool`. Use o `clipId` gerado pelo editor,
não o nome visual, em scripts que precisam sobreviver a renomeações.

## Eventos e cenas

`EventSystem` permite assinar `SceneLoadedEvent`, `PlayStateChangedEvent`,
`EntityCreatedEvent` e `EntityDestroyedEvent`. A troca de cena é atualmente
gerenciada pelo editor/runtime nativo; não há uma função C# pública de carregar
cena arbitrária neste contrato. Não invente `Scene.Load`: assine os eventos e
use as ferramentas de cena da engine.

## Erros e debugger

Chamadas de ciclo de vida e eventos são protegidas pelo runtime. Uma exceção é
registrada com tipo, método, stack e campos públicos; por padrão ela não fecha a
engine. O painel **Debug** pode ativar pausa em exceções e breakpoints por
`TipoDoScript::Método` (por exemplo `MeuController::OnUpdate`). A primeira
versão do debugger não oferece stepping por linha nem watch de expressão.

## Outros serviços do SDK

- `Audio.Play(id)` e `PlayById(id)` reproduzem fonte pelo ID; `AudioComponent`
  controla Play/Stop/volume/loop/pitch.
- `AnimationComponent` expõe `clipId`, estado e reprodução; `Play(id)` retorna
  false quando o ID não corresponde ao clip associado.
- `MeshComponent`, `MaterialComponent`, `LightComponent`, `CameraComponent` e
  `GlobalLightComponent` editam propriedades de componentes anexados; não criam
  esses componentes.
- `Navigation.FindPath(start, destination)` consulta caminho no NavMesh e retorna
  array vazio se não houver caminho válido.
- `Random`, `Mathf`, `Vector3`, `Quaternion` e `Color` fornecem utilitários de
  matemática. Ângulos em `Transform.Rotate`, `Quaternion.Euler` e propriedades
  angulares de `LightComponent` são em radianos; `Mathf.Deg2Rad`/`Rad2Deg`
  convertem graus.
- `MaraeEngine.Events.EventSystem` permite Subscribe/Unsubscribe/Trigger para
  tipos `IEventData`; eventos incluídos cobrem carga de cena, estado Play e
  criação/destruição de entidade.

## Fluxo de solução de problemas

1. Prefira `Assets/Scripts`. `Scripts` na raiz é usado como fallback quando a
   pasta preferida não existe; mantenha os fontes numa única pasta ativa e evite
   duplicar tipos.
2. Confira `using MaraeEngine;`, classe pública concreta e herança de
   `ScriptBehaviour`. Ao anexar um `.cs`, a Marae localiza automaticamente a
   classe declarada naquele arquivo; os nomes do arquivo e da classe podem diferir.
3. Compile pelo editor e resolva todos os erros antes de iniciar Play.
4. No Hierarchy, arraste o `.cs` para o objeto ou use **Adicionar filho → Scripts e
   Animação → Adicionar Script C# ao objeto**. Confira `Enabled` e o estado ativo.
5. Em Play, confira foco/input do viewport, IDs de áudio/animação e existência
   dos componentes consultados.
6. Registre estado com `UnityEngine.Debug.Log/LogWarning/LogError` e veja o
   painel Debug. CS0101/CS0111 normalmente indicam tipos/métodos duplicados;
   CS0246/CS1061 normalmente indicam tipo/API inexistente ou namespace incorreto.

Compatibilidade Unity é parcial: somente os wrappers presentes no SDK existem.
Não presuma scene loading arbitrário, coroutines padrão, API integral do Unity,
criação genérica de componentes ou callback de clique de UI não listado aqui.

## Regra de compatibilidade

O contrato 1.x é aditivo: tipos e membros existentes não devem ser renomeados ou
removidos sem um período de migração. Consulte `COMPATIBILITY.md` antes de
atualizar scripts de projeto.
