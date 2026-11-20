# Sistema de iluminação avançado — Marae Engine

## Resumo

A iluminação avançada foi integrada ao renderer de cena existente e é **opt-in**: `LightingSettings::Enabled` começa em `false`, e `Material::PBRParametersEnabled` também começa desligado. Cenas antigas continuam válidas. Quando a opção é ativada, os parâmetros de luz e material são lidos no frame de renderização; alterações feitas no Inspector aparecem no próximo frame.

## Organização

| Camada | Responsabilidade |
|---|---|
| `LightComponent` | Tipo, ativação, cor/intensidade, alcance, temperatura, direção, cone, sombra e contribuição indireta por entidade. |
| `LightingManager` | Configuração da cena, luz solar e coleções legadas de Point/Spot. |
| `LightingSystem` | Conversão de temperatura, amostra solar e avanço do ciclo dia/noite. |
| `ShadowSystem` | Política de sombras, distância e resolução efetiva conforme qualidade. |
| `SceneRenderer` | Culling de esfera pelo frustum da câmera, seleção limitada de luzes e atualização seletiva de mapas de sombra. |
| `Shader` | BRDF GGX/Smith/Schlick, luz direta, respostas PBR, normal maps e irradiância aproximada. |
| `SkyRenderer` | Céu procedural existente, sincronizado com a direção/intensidade solar quando o ciclo está habilitado. |

## Iluminação e limites atuais

O shader usa uma BRDF microfacet GGX, geometria Smith e Fresnel Schlick para luz direcional, pontual e spot. Há suporte a parâmetros personalizados por material — rugosidade, metalness, specular, emissão e alpha —, além de normal map e emission map opcionais. Quando não há parâmetros PBR personalizados, os presets de material existentes continuam sendo usados.

Point e Spot recebem atenuação inversa ao quadrado, suavizada pelo raio do emissor e limitada pelo alcance. As fontes podem usar temperatura em Kelvin. Luzes do `LightingManager` e componentes ECS seguem o mesmo shader; entidades-luz podem ser ativadas, movidas ou removidas durante a execução.

As sombras direcionais usam cascatas e as locais usam cubemaps, com limite configurável de fontes locais sombreadas. Mapas só são recriados/renderizados quando tamanho, cena, câmera, fonte ou geometria relevante invalidam o cache. A qualidade limita as cascatas e resoluções. A força de contato por luz combina com o controle global e altera o PCF/radius em shadow maps existentes; **não é ray tracing nem uma passagem SSAO de espaço de tela**. O alpha PBR é aplicado ao blend e à opacidade armazenada nos mapas de sombra.

A iluminação indireta não implementa path tracing, radiosidade multi-bounce nem probes dinâmicos: isso exigiria outra arquitetura de buffers/probes e custo adicional. O caminho atual combina irradiância ambiental hemisférica e um termo local de baixa energia das fontes Point/Spot que permitem contribuição indireta. O reflexo especular de ambiente também é uma aproximação hemisférica, não um environment cubemap com convolution. Portanto, o resultado é uma aproximação econômica, não GI fisicamente completa.

`Area` é compatível como **aproximação**: a escala da entidade aumenta o raio da fonte Point correspondente, suavizando a penumbra. Não é uma superfície emissora integrada analiticamente. O culling atual reduz luzes locais ao frustum e aos limites da cena, mas ainda não usa spatial partition, tiled/clustered shading ou batching de luzes. As capacidades atuais são até oito Point, oito Spot e quatro luzes locais com shadow maps, limitadas por configuração.

## Inspector e persistência

No componente Light do Inspector é possível alterar tipo, ativação, cast shadows, cor, intensidade, temperatura, alcance, direção, raio/suavidade e, para Spot, os ângulos; também há contribuição indireta por fonte. `Global Light` reúne qualidade, máximos, sombras, irradiância, distância/resolução e o ciclo solar. O ciclo é editável em horas, avança apenas em Play e sincroniza o sol e o céu.

`Application::SaveScene/LoadScene` estende o formato de texto existente com um bloco opcional `@lighting`. Campos avançados de `@material` são opcionais depois do tipo antigo; o leitor continua aceitando linhas antigas que continham somente o tipo. As configurações em `@lighting` explícitas prevalecem sobre preferências globais antigas do editor. Materiais referenciados por GUID e mapas locais são persistidos para a entidade.

## Cena de verificação

Abra `LightingAdvancedTest.scene` no editor. A cena usa esferas com combinações distintas de metalness/rugosidade, um emissor visível, painel acrílico, piso e oclusor para observar sombras, transparência, Point/Spot, sol direcional e uma Area aproximada. O bloco `@lighting` habilita explicitamente o sistema **somente nessa cena de teste**; habilite Play para observar a mudança temporal entre dia e noite. A malha OBJ pequena e UV-mapeada usada pelas esferas está em `Assets/Meshes/LightingTestSphere.obj`.

## Validação

O fixture inclui parâmetros de materiais locais PBR e um bloco `@lighting` retrocompatível. Antes de distribuir a engine, compile `MaraeEngine.vcxproj` no Visual Studio com os workloads Windows/C++ e execute a cena em hardware com suporte a OpenGL 3.3; a validação visual do shader e das sombras precisa ocorrer com contexto OpenGL ativo.
