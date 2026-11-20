# Exemplos de gameplay C#

Estes exemplos usam somente a API pública `MaraeEngine`. Eles são scripts de
referência que compilam com `ApiExamples.csproj`; cada cena ainda precisa ser
montada no editor com os objetos e componentes listados abaixo.

## Pong 2D

Anexe `PongPaddle2D.cs` a cada raquete. Configure um mundo 2D, limites da quadra
e um objeto de bola com collider. W/S ou as setas verticais movem a raquete.

## Plataforma 2D

Anexe `PlatformerMotor2D.cs` ao personagem; use Rigidbody dinâmico, gravidade e
collider. Crie chão estático com collider. A/D ou setas movem; Espaço salta após
o callback de contato identificar o chão.

## Top-down 2D

Anexe `TopDownController2D.cs` ao personagem em um projeto 2D. WASD/setas movem
em oito direções; diagonais são normalizadas.

## Corrida 3D arcade

Anexe `RacingCarController3D.cs` ao chassi com Rigidbody dinâmico e collider.
W/S aceleram/freiam e A/D esterçam. Configure atrito e uma pista com colliders.
Este exemplo não simula suspensão nem rodas.

## Coleta de sobrevivência

Anexe `SurvivalPickup.cs` ao jogador com collider e ative `isTrigger`. Os itens
precisam ter collider, tag `Collectible` e não podem estar estáticos sem corpo
compatível com callbacks de trigger. A contagem local fica em `Collected`.

## Câmera orbital 3D

Adicione uma entidade chamada `Target`, coloque a câmera na cena e anexe
`OrbitCamera3D.cs` à câmera. O script posiciona e aponta a câmera ao redor do alvo.

## Verificação

Compile os exemplos com:

```powershell
dotnet build MaraeEngine\Tests\Scripting\ApiExamples\ApiExamples.csproj --no-restore -t:Rebuild
```

Esses scripts compilam como biblioteca de exemplos. A compilação não cria as
cenas, não adiciona componentes automaticamente e não substitui um teste de
Play nas cenas montadas.
