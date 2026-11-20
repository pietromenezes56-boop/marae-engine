# Padrão visual da interface da Marae Engine

## Diretriz permanente de identidade

**Não redesenhar, substituir ou mudar o estilo visual estabelecido neste documento sem um pedido explícito do usuário.** Melhorias futuras devem preservar a identidade da Marae e ampliar este padrão de forma compatível, sem trocar o tema, a paleta, a linguagem dos ícones ou a organização dos painéis.

## Ícones

- Usar exclusivamente o catálogo vetorial de `EditorIcons.h` para ícones da interface.
- Desenhar ícones leves, legíveis em tamanhos pequenos e com traço consistente; cubos, esferas, câmeras, luzes, componentes e ferramentas devem ter silhuetas reconhecíveis.
- Usar a paleta e o contraste já definidos pelo tema escuro da Marae; os ícones não devem introduzir uma nova família de cores ou aparência.
- Não usar emojis, glifos de fonte ou outra biblioteca visual como substitutos dos ícones do catálogo.
- Para uma função nova, adicionar um desenho ao catálogo existente e reutilizar os helpers `DrawEditorIcon`, `DrawIconLabel`, `IconMenuItem` e `IconButton` quando apropriado. Não criar um segundo sistema de ícones.
- Manter os rótulos, atalhos, ações e regras de habilitação dos controles. A adoção de um ícone não pode alterar a funcionalidade nem a área clicável do item.

## Layout e interação

- Preservar o tema, a hierarquia visual, o docking e a identidade dos painéis existentes.
- Ajustar espaçamento somente quando necessário para evitar sobreposição ou melhorar legibilidade; não reorganizar a interface inteira como efeito colateral de uma melhoria local.
- Ao alterar viewport, painéis ou renderização, preservar a última imagem válida durante o redimensionamento e evitar exibir texturas de framebuffer ainda não renderizadas.
- Qualquer futura proposta que mude esta identidade visual deve ser apresentada ao usuário antes da implementação.
