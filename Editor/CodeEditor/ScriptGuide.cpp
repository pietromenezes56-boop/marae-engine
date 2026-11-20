#include "ScriptGuide.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace
{
    struct GuidePage
    {
        const char* title;
        const char* intro;
        const char* details;
        const char* code;
    };

    const std::vector<GuidePage>& Pages()
    {
        static const std::vector<GuidePage> pages = {
            {"Comece aqui", "Crie um script C# que roda como componente de uma entidade Marae.",
             "1. No projeto, crie ou abra Assets/Scripts. Projetos antigos podem usar Scripts na raiz.\n"
              "2. Crie um arquivo .cs com uma classe que herda de ScriptBehaviour. A Marae encontra automaticamente a classe no arquivo; não precisa renomear classe para combinar com o arquivo.\n"
             "3. Herde de MaraeEngine.ScriptBehaviour. Não use MonoBehaviour como base.\n"
             "4. Salve e use Compilar scripts C# no editor. Corrija os diagnósticos antes de anexar.\n"
              "5. Arraste o arquivo .cs para o objeto no Hierarchy ou adicione Script C# pelo menu de filho.\n"
             "6. Pressione Play. O código não deve executar no modo Edit.\n\n"
             "O runtime usa Assets/Scripts quando essa pasta existe; Scripts na raiz é fallback para projetos antigos. Mantenha os scripts em uma única pasta ativa: se dividir arquivos entre as duas pastas, os arquivos da pasta ignorada não entrarão na compilação. O SDK MaraeEngine já é fornecido pelo runtime; não instale pacote externo para acessar componentes nativos.",
             "using MaraeEngine;\n\npublic class MeuPrimeiroScript : ScriptBehaviour\n{\n    public float velocidade = 3f;\n\n    public override void OnStart()\n    {\n        if (gameObject != null)\n            gameObject.name = \"Meu objeto em jogo\";\n    }\n\n    public override void OnUpdate(float dt)\n    {\n        if (transform == null) return;\n        transform.Translate(Vector3.forward * (velocidade * dt), Space.World);\n    }\n}"},
             {"Arquivo, compilação e anexação", "O editor compila os arquivos .cs do projeto e associa automaticamente o script ao objeto onde você o colocou.",
             "• Salve o arquivo antes de compilar. O botão de compilação chama a compilação Roslyn real do projeto; o diagnóstico estático do editor não substitui essa compilação.\n"
             "• Mensagens CSxxxx apontam erro de C#. Clique duas vezes em um diagnóstico para navegar à linha quando a mensagem veio do painel local.\n"
              "• A classe do script precisa ser pública, concreta e herdar ScriptBehaviour; mantenha um construtor padrão sem parâmetros. O nome do arquivo e o nome da classe podem ser diferentes.\n"
              "• Se o arquivo tiver várias classes ScriptBehaviour, a Marae escolhe automaticamente a classe cujo nome corresponde ao arquivo. Se nenhuma corresponder, deixe uma classe de script por arquivo.\n"
             "• Se aparecer “tipo não encontrado”, resolva primeiro todos os erros de compilação. Uma compilação com erros pode impedir que os scripts do projeto sejam carregados.\n"
             "• O runtime compila os .cs da pasta de scripts escolhida pelo projeto; Assets/Scripts tem prioridade, Scripts na raiz só é fallback. Evite espalhar fontes nas duas.\n"
             "• O arquivo pode declarar outras classes auxiliares, mas evite duplicar nomes públicos entre arquivos.\n"
             "• Campos públicos são a forma mais simples de expor valores editáveis/serializáveis no Inspector; confira o estado salvo do componente após alterar nomes ou tipos.",
             "using MaraeEngine;\n\npublic class Porta : ScriptBehaviour\n{\n    public float tempoAberta = 2f;\n    private float decorrido;\n\n    public override void OnStart()\n    {\n        decorrido = 0f;\n    }\n\n    public override void OnUpdate(float dt)\n    {\n        decorrido += dt;\n        if (decorrido >= tempoAberta)\n            transform!.position = new Vector3(0f, 3f, 0f);\n    }\n}"},
            {"Ciclo de vida e tempo", "Use callbacks de frame para lógica geral e o passo fixo para lógica física.",
             "Callbacks públicos de ScriptBehaviour: OnEnable(), OnDisable(), OnStart(), Start(), OnUpdate(float dt), Update(), OnFixedUpdate(float fixedDt), FixedUpdate(), OnDestroy(), eventos de colisão e trigger. O invoker chama os pares Marae e aliases (OnStart/Start, OnUpdate/Update, OnFixedUpdate/FixedUpdate); implemente apenas o método desejado de cada par para não duplicar a ação.\n\n"
             "OnStart/Start ocorrem uma vez no início da instância; OnUpdate/Update rodam a cada frame em Play; OnFixedUpdate/FixedUpdate são para o passo de física fixo. OnEnable/OnDisable acompanham habilitação/ativação; OnDestroy é limpeza. Objetos desativados ou scripts desabilitados não recebem atualizações normais.\n\n"
             "Multiplique deslocamentos por dt para que a velocidade não dependa do FPS. Use Time.timeScale para escala/pausa global e Time.unscaledDeltaTime para UI ou temporizadores que devam ignorar essa escala.",
             "using MaraeEngine;\n\npublic class Rotacionar : ScriptBehaviour\n{\n    public float grausPorSegundo = 90f;\n\n    public override void OnUpdate(float dt)\n    {\n        // Rotate recebe radianos; converta graus para radianos.\n        float rad = grausPorSegundo * (MathF.PI / 180f) * dt;\n        transform?.Rotate(0f, rad, 0f, Space.World);\n    }\n}"},
            {"Entidade e hierarquia", "Encontre objetos por nome/tag, clone-os e consulte os componentes conhecidos pelo SDK.",
             "A entidade associada está em gameObject (alias GameObject); transform e rigidbody são atalhos. Entity.Find procura o primeiro nome (case-sensitive), FindWithTag procura uma tag, FindGameObjectsWithTag retorna todas; FindEntityByID localiza pelo ID numérico enquanto a entidade existir.\n\n"
             "GetComponent<T>() e HasComponent<T>() suportam as fachadas conhecidas: MeshComponent, AudioComponent, LightComponent, MaterialComponent, ScriptComponent, Rigidbody, UIElement, AnimationComponent, CameraComponent e GlobalLightComponent. Componentes ausentes retornam null. Nem todo tipo interno da engine é acessível pelo SDK público.\n\n"
             "Entity.Instantiate clona uma entidade e os componentes. Entity.Destroy destrói no fim do frame; DestroyHierarchy inclui descendentes. SetParent/GetChildEntity permitem operar na hierarquia. Guarde Entity.id como identificador da instância, não o nome visual.",
             "Entity? alvo = Entity.FindWithTag(\"Inimigo\");\nif (alvo != null)\n{\n    alvo.transform.position = transform!.position + Vector3.forward * 2f;\n}\n\nEntity? copia = gameObject == null ? null : Entity.Instantiate(gameObject);\nif (copia != null)\n{\n    copia.name = \"Inimigo cópia\";\n    copia.transform.position += Vector3.right * 2f;\n}\n\n// Destruição segura quando necessário:\n// copia?.Destroy();"},
            {"Transformação e matemática", "Transform usa posição/rotação/escala; os vetores e quatérnios pertencem ao namespace MaraeEngine.",
             "position/localPosition são posições mundial/local; localScale é escala local. forward, right e up são direções mundiais rotacionadas. Translate e Rotate recebem Space.Self por padrão; use Space.World para eixos globais.\n\n"
             "Euler, Rotate, RotateAround, Quaternion.Euler e Quaternion.AngleAxis usam radianos na implementação atual. Não passe graus diretamente. Vector3 oferece zero/one/up/forward/right, magnitude, normalized, Normalize, Dot, Cross, Lerp e operações vetoriais. Mathf oferece Abs, Min/Max, Clamp, Clamp01, Lerp, MoveTowards, SmoothStep, seno/cosseno, raiz e funções correlatas. Random está no SDK; confirme membros pela assinatura no autocomplete/arquivos públicos do projeto.\n\n"
             "Use Quaternion para compor rotações 3D sem acumular ângulos de Euler manualmente. Transform.LookAt aponta o eixo forward para uma posição alvo.",
             "Vector3 alvo = new Vector3(5f, 0f, 2f);\ntransform?.LookAt(alvo);\n\nVector3 direcao = (alvo - transform!.position).normalized;\ntransform.position += direcao * (2f * Time.deltaTime);\n\n// Rotação em radianos: meia volta no eixo Y.\ntransform.rotation = Quaternion.Euler(0f, MathF.PI, 0f);"},
            {"Entrada e ações", "Leia teclado, mouse e ações mapeadas durante Play.",
             "Input.GetKey permanece true enquanto a tecla está pressionada; GetKeyDown e GetKeyUp são pulsos de um frame. KeyCode inclui letras, setas, espaço, Escape, teclas de função e códigos do mouse.\n\n"
             "Input.GetMouseButton usa 0 esquerdo, 1 direito, 2 meio; há variantes Down/Up. GetAxis/GetAxisRaw aceitam Horizontal, Vertical, Mouse X e Mouse Y. GetAxis aplica deadzone a eixos de teclado; GetAxisRaw retorna o valor nativo.\n\n"
             "GetMappedAction/Down/Up consultam o mapa do projeto. BindKey, BindMouseButton e ClearBindings alteram bindings. GetActionDown tem fallback para Jump/Flap/Submit e Cancel se o mapa nativo não estiver disponível. Captura de entrada depende de foco do viewport/jogo.",
             "if (Input.GetKeyDown(KeyCode.Space))\n    Pular();\n\nif (Input.GetMouseButtonDown(0))\n    Audio.Play(\"audio_23_3\");\n\nfloat horizontal = Input.GetAxisRaw(\"Horizontal\");\ntransform?.Translate(Vector3.right * horizontal * 4f * Time.deltaTime, Space.World);\n\nif (Input.GetActionDown(\"Jump\"))\n    Pular();"},
            {"Física e colisão", "Configure o Rigidbody no Inspector e altere sua velocidade/impulso pelo script.",
             "Rigidbody expõe enabled, useGravity, isStatic, colliderEnabled, continuousCollision, freezeRotation, collider (ColliderShape), mass, restitution, friction, velocity, angularVelocity e AddImpulse. O wrapper não cria automaticamente um collider: a entidade precisa do componente físico/configuração no Inspector.\n\n"
             "Para controlar um corpo dinâmico, prefira OnFixedUpdate e velocity/impulso. Não sobrescreva position todo frame enquanto o solver está simulando o mesmo corpo, pois isso pode brigar com a física. Static indica corpo imóvel; não espere gravidade nele.\n\n"
             "OnCollisionEnter/Stay/Exit recebem Collision com other, contactPoint, normal e impulse. OnTriggerEnter/Stay/Exit recebem Collider; triggers detectam sobreposição e não são colisão sólida. Verifique que os dois objetos têm colisores habilitados e que a configuração trigger está correta. O runtime nativo encaminha os callbacks quando a simulação os emite; valide colisões na cena concreta.",
             "using MaraeEngine;\n\npublic class Pulo : ScriptBehaviour\n{\n    private bool noChao;\n\n    public override void OnUpdate(float dt)\n    {\n        if (noChao && Input.GetKeyDown(KeyCode.Space) && rigidbody != null)\n        {\n            rigidbody.velocity = new Vector3(rigidbody.velocity.X, 0f, rigidbody.velocity.Z);\n            rigidbody.AddImpulse(Vector3.up * 5f);\n            noChao = false;\n        }\n    }\n\n    public override void OnCollisionEnter(MaraeEngine.Collision.Stubs.Collision c)\n    {\n        if (c.normal.Y > 0.5f) noChao = true;\n    }\n    public override void OnCollisionStay(MaraeEngine.Collision.Stubs.Collision c)\n    {\n        if (c.normal.Y > 0.5f) noChao = true;\n    }\n    public override void OnCollisionExit(MaraeEngine.Collision.Stubs.Collision c)\n    {\n        noChao = false;\n    }\n}"},
            {"Áudio", "Use um ID de fonte para playback global ou controle o AudioComponent anexado.",
             "Audio.Play(sourceId) e PlayById retornam bool indicando se a fonte identificada foi encontrada/iniciada. Copie o ID do asset/fonte no Inspector; não use o nome do arquivo como se fosse o ID.\n\n"
             "AudioComponent.Play/Stop, isPlaying, volume, loop e pitch controlam a fonte da entidade. GetComponent<AudioComponent>() pode retornar null se ela não estiver anexada. Importar um áudio é um passo separado de adicioná-lo à cena como fonte.\n\n"
             "A API pública aqui não expõe ainda mixagem detalhada, efeitos de áudio ou callback de clique de botão. Para botão UI, use o estado de ponteiro exposto e entrada do mouse, ou a integração de eventos implementada pelo projeto.",
             "using MaraeEngine;\n\npublic class SomAoClicar : ScriptBehaviour\n{\n    public string audioId = \"audio_23_3\";\n\n    public override void OnUpdate(float dt)\n    {\n        if (Input.GetMouseButtonDown(0))\n        {\n            bool iniciou = Audio.Play(audioId);\n            if (!iniciou)\n                UnityEngine.Debug.LogWarning(\"ID de áudio não encontrado: \" + audioId);\n        }\n    }\n}"},
            {"UI 2D", "Acesse elementos UI por gameObject.ui e responda a eventos de interface.",
             "UIElement oferece type, enabled, rect, âncoras, rotation, transparency, cornerRadius, borderThickness, fontSize, zIndex, textAlignment, clipsContent, backgroundColor, hoverColor, pressedColor, textColor, borderColor, text, font, imagePath e os controles Slider/Checkbox/Dropdown. Filhos UI usam coordenadas relativas ao retângulo do pai; um pai desabilitado oculta toda a subárvore.\n\n"
             "OnUIClicked é chamado no script anexado ao TextButton/ImageButton. OnUIValueChanged atende Slider, Checkbox e Dropdown; OnUITextChanged atende TextBox. Um script pode alterar outro elemento por Entity.Find(\"Nome\").ui, inclusive mostrar/ocultar com enabled e mudar backgroundColor durante Play. Entidades sem UI retornam null em gameObject.ui.",
             "using MaraeEngine;\n\npublic class AbrirPainel : ScriptBehaviour\n{\n    public string painelName = \"Painel\";\n\n    public override void OnUIClicked(UIElement botao)\n    {\n        Entity? painel = Entity.Find(painelName);\n        UIElement? uiPainel = painel?.ui;\n        if (uiPainel != null)\n            uiPainel.enabled = !uiPainel.enabled;\n\n        botao.backgroundColor = new Color(\n            Random.Range(0f, 1f), Random.Range(0f, 1f), Random.Range(0f, 1f), 1f);\n    }\n}"},
            {"Materiais, luz e câmera", "As fachadas mudam propriedades em componentes que já existem na entidade.",
             "MaterialComponent: albedo, metallic e smoothness. As propriedades metálico e suavidade são limitadas a [0,1]. MeshComponent: meshName, visible e color. LightComponent: enabled, castShadows, useTemperature, distanceSoftening, indirectContribution, type, intensity, color, range, spotAngle, sourceRadius, shadowSoftness, shadowStrength, contactShadowStrength e temperatureKelvin.\n\n"
             "CameraComponent: enabled, previewEnabled, orthographic, fieldOfView, nearClip, farClip e orthographicSize. GlobalLightComponent controla enabled, ambientIntensity/color, exposure, shadowStrength, fogEnabled/color/density/start/end e cloudsEnabled/movement/amount/speed. Sempre verifique null antes de usar GetComponent<T>().\n\n"
             "Essas interfaces editam componentes existentes; elas não criam mesh, câmera ou luz do nada. Propriedades sem binding nativo ativo podem não surtir efeito; use o componente correspondente no Inspector e compile o projeto para detectar incompatibilidade.",
             "MaterialComponent? mat = gameObject?.GetComponent<MaterialComponent>();\nif (mat != null)\n{\n    mat.albedo = new Color(0.1f, 0.5f, 1f, 1f);\n    mat.metallic = 0.2f;\n    mat.smoothness = 0.75f;\n}\n\nLightComponent? luz = gameObject?.GetComponent<LightComponent>();\nif (luz != null)\n{\n    luz.enabled = true;\n    luz.castShadows = true;\n    luz.intensity = 4f;\n}"},
            {"Animação e IDs", "Use o identificador estável do clip para controlar a animação anexada.",
             "AnimationComponent fornece enabled, isPlaying, playOnStart, time, speed, weight, clipName, clipId, currentState, Play(), Play(animationId), Pause(), Stop() e SetBool(parameter, value). O overload Play(id) retorna false se o ID não corresponder ao clip associado.\n\n"
             "Gere/copiei o ID no sistema de animação da Marae e mantenha esse ID no campo do script. O nome visual pode mudar; IDs servem para referência estável. A instância/cena precisa ter AnimationComponent e clip associado.\n\n"
             "Se a animação deve iniciar automaticamente, confira playOnStart no componente e que a entidade está ativa na cena carregada. A chamada de script pressupõe Play; ela não corrige um clip ausente ou não associado.",
             "using MaraeEngine;\n\npublic class AnimarPorta : ScriptBehaviour\n{\n    public string idDoClip = \"animation_01\";\n    private bool iniciou;\n\n    public override void OnStart()\n    {\n        AnimationComponent? anim = gameObject?.GetComponent<AnimationComponent>();\n        if (anim == null) return;\n        iniciou = anim.Play(idDoClip);\n        if (!iniciou)\n            UnityEngine.Debug.LogError(\"Clip não associado ou ID incorreto: \" + idDoClip);\n    }\n}"},
            {"Eventos, temporizadores e depuração", "Use eventos tipados e registre mensagens úteis no painel Debug.",
             "ScriptBehaviour fornece Invoke(method, seconds), InvokeRepeating(method, delay, interval), CancelInvoke(), CancelInvoke(method), IsInvoking() e IsInvoking(method). Os nomes precisam apontar para métodos do próprio script que o invoker consiga chamar.\n\n"
             "MaraeEngine.Events.EventSystem aceita Subscribe<T>, Unsubscribe<T> e Trigger<T>, para tipos que implementam IEventData. Eventos incluídos: SceneLoadedEvent, PlayStateChangedEvent, EntityCreatedEvent e EntityDestroyedEvent. Sempre remova inscrições em OnDisable/OnDestroy para não deixar handlers antigos.\n\n"
             "UnityEngine.Debug.Log, LogWarning e LogError são aliases de compatibilidade que escrevem no log nativo. Isso não significa compatibilidade integral com Unity. Use o painel Debug e a saída da compilação; exceções são registradas pelo runtime, mas erros lógicos precisam de logs/inspeção do estado.",
             "using MaraeEngine;\nusing MaraeEngine.Events;\n\npublic class ObservarPlay : ScriptBehaviour\n{\n    public override void OnEnable()\n    {\n        EventSystem.Subscribe<PlayStateChangedEvent>(AoMudarPlay);\n    }\n\n    public override void OnDisable()\n    {\n        EventSystem.Unsubscribe<PlayStateChangedEvent>(AoMudarPlay);\n    }\n\n    private void AoMudarPlay(PlayStateChangedEvent e)\n    {\n        UnityEngine.Debug.Log(\"Play ativo: \" + e.IsPlaying);\n    }\n}"},
            {"Erros comuns e limites da API", "Diagnostique do mais básico ao mais específico antes de alterar o runtime.",
             "CS0101/CS0111: há tipos/métodos duplicados; procure cópias em todas as pastas compiladas. CS0246: namespace/tipo não existe ou falta using MaraeEngine. CS1061: o método/propriedade não pertence à API pública atual. CS8602: trate referência possivelmente nula. Tipo não resolvido no Inspector: compile sem erros e confira nome público do tipo/componente.\n\n"
             "Se um comando não funciona em Play: confirme que o script está anexado à entidade certa, habilitado, que a entidade está ativa, que a cena está em execução, que o foco/input chega ao viewport e que o componente/ID existe. Imprima valores e use IDs exatos.\n\n"
             "Compatibilidade Unity é parcial. UnityEngineCompat oferece apenas wrappers que aparecem no SDK; a presença de um namespace não implementa toda a Unity API. Scene loading arbitrário, criação genérica de componentes, callback onClick real para UIElement, raycast genérico, networking e coroutine padrão não devem ser presumidos. Consulte MaraeApi.Version e a referência desta janela antes de usar uma chamada.",
             "// Exemplo defensivo: validar dependências em OnStart.\npublic override void OnStart()\n{\n    if (gameObject == null)\n    {\n        UnityEngine.Debug.LogError(\"Script sem entidade associada.\");\n        return;\n    }\n\n    if (!gameObject.HasComponent<MaraeEngine.Rigidbody>())\n        UnityEngine.Debug.LogWarning(\"Adicione/configure Física no Inspector.\");\n\n    UnityEngine.Debug.Log(\"Script iniciado em: \" + gameObject.name +\n                         \" | Entity ID: \" + gameObject.id);\n}"}
            ,
            {"Referência de tipos e propriedades", "Resumo navegável do SDK MaraeEngine 1.0.",
             "Base: ScriptBehaviour; entidades: Entity e GameObject; transformação: Transform; matemática: Vector3, Quaternion, Color, Space, Mathf e Random; enumerações: KeyCode, ColliderShape, LightType, UIElementType e UITextAlignment.\n\n"
             "Tempo: Time.deltaTime, unscaledDeltaTime, time, fixedDeltaTime, timeScale, isPaused. Mathf: PI, Deg2Rad, Rad2Deg, Abs, Sign, Min, Max, Clamp, Clamp01, Lerp, InverseLerp, MoveTowards, Repeat, PingPong, SmoothStep, Sqrt, Pow, Sin, Cos, Tan, Atan2, Floor, Ceil, Round, Approximately. Random: value, Range(int/float), InsideUnitSphere, OnUnitSphere. Navigation.FindPath(start, destination) retorna pontos ou array vazio se não houver caminho/NavMesh válido.\n\n"
             "Entity: id/name/activeSelf/tag/layer/transform/ui/rigidbody/camera/animation/globalLight/HasUI/HasComponent<T>/GetComponent<T>/Find/FindWithTag/FindGameObjectsWithTag/FindEntityByID/Instantiate/Destroy/DestroyHierarchy. Transform: localPosition/position/localRotation/rotation/localScale/localEulerAngles/eulerAngles/forward/right/up/Translate/Rotate/RotateAround/LookAt/SetParent/parent/childCount/GetChild/GetChildEntity/layer.\n\n"
             "Input: GetKey/Down/Up, IsKeyDown/Pressed/Released, GetMouseButton/Down/Up, mousePosition, GetAxis/GetAxisRaw, GetMappedAction/Down/Up, GetAction/Down, BindKey, BindMouseButton, ClearBindings, SetAxisDeadZone, leftMouse/rightMouse/middleMouse. Rigidbody: enabled/useGravity/isStatic/colliderEnabled/continuousCollision/freezeRotation/collider/mass/restitution/friction/velocity/angularVelocity/AddImpulse.\n\n"
             "Audio: Audio.Play/PlayById; AudioComponent Play/Stop/isPlaying/volume/loop/pitch. Animação: enabled/isPlaying/playOnStart/time/speed/weight/clipName/clipId/currentState/Play/Pause/Stop/SetBool. Camera: enabled/previewEnabled/orthographic/fieldOfView/nearClip/farClip/orthographicSize. Mesh: meshName/visible/color. Material: albedo/metallic/smoothness.\n\n"
             "Light: enabled/castShadows/useTemperature/distanceSoftening/indirectContribution/type/intensity/color/range/spotAngle/sourceRadius/shadowSoftness/shadowStrength/contactShadowStrength/temperatureKelvin. GlobalLight: enabled/ambientIntensity/ambientColor/exposure/shadowStrength/fogEnabled/fogColor/fogDensity/fogStart/fogEnd/cloudsEnabled/cloudMovementEnabled/cloudAmount/cloudSpeed. UIElement: veja a seção UI 2D. ScriptBehaviour: callbacks, Invoke/InvokeRepeating/CancelInvoke/IsInvoking/Call. EventSystem: Subscribe/Unsubscribe/Trigger; eventos padrão SceneLoadedEvent, PlayStateChangedEvent, EntityCreatedEvent, EntityDestroyedEvent. Veja os arquivos públicos do SDK para assinaturas completas; a referência acompanha MaraeApi.Version.",
             "Entity? alvo = Entity.FindWithTag(\"Player\");\nif (alvo != null)\n{\n    alvo.transform.localScale = Vector3.one * 1.25f;\n    Rigidbody? corpo = alvo.GetComponent<Rigidbody>();\n    if (corpo != null)\n    {\n        corpo.useGravity = true;\n        corpo.continuousCollision = true;\n    }\n}\n\nColor ciano = Color.Cyan;\nColor personalizado = Color.FromByte(20, 120, 255);"}
        };
        return pages;
    }

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
}

void ScriptGuide::Render(bool* open)
{
    if (!open || !*open) return;

    ImGui::SetNextWindowSize(ImVec2(980.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Documentação C# Marae##script-guide", open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Referência integrada do SDK de scripting. Os exemplos usam a API MaraeEngine pública; compile no seu projeto para validar o estado exato do runtime.");
    ImGui::Separator();

    static int selected = 0;
    static char search[160] = {};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##script-guide-search", "Buscar tópico, API ou erro...", search, sizeof(search));

    const auto& pages = Pages();
    const std::string query = Lower(search);
    std::vector<int> visiblePages;
    visiblePages.reserve(pages.size());
    for (int i = 0; i < static_cast<int>(pages.size()); ++i)
    {
        const auto& page = pages[static_cast<size_t>(i)];
        const std::string haystack = Lower(std::string(page.title) + " " + page.intro + " " + page.details + " " + page.code);
        if (query.empty() || haystack.find(query) != std::string::npos)
            visiblePages.push_back(i);
    }
    if (!visiblePages.empty() &&
        std::find(visiblePages.begin(), visiblePages.end(), selected) == visiblePages.end())
        selected = visiblePages.front();

    const float navWidth = 245.0f;
    ImGui::BeginChild("##script-guide-nav", ImVec2(navWidth, 0.0f), true);
    for (int i : visiblePages)
    {
        const auto& page = pages[static_cast<size_t>(i)];
        if (ImGui::Selectable(page.title, selected == i)) selected = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##script-guide-content", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (visiblePages.empty())
        ImGui::TextWrapped("Nenhum tópico corresponde à busca.");
    else
    {
        const GuidePage& page = pages[static_cast<size_t>(selected)];
        ImGui::TextColored(ImVec4(0.35f, 0.72f, 1.0f, 1.0f), "%s", page.title);
        ImGui::TextWrapped("%s", page.intro);
        ImGui::Separator();
        ImGui::TextWrapped("%s", page.details);
        ImGui::Spacing();
        ImGui::TextUnformatted("Exemplo");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copiar código")) ImGui::SetClipboardText(page.code);
        ImGui::BeginChild("##script-guide-code", ImVec2(0.0f, 260.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(page.code);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::End();
}
