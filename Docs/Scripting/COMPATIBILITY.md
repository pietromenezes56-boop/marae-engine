# Compatibilidade do scripting

## Política

- A API pública atual é `MaraeApi 1.0`.
- Adições compatíveis incrementam a versão minor e preservam scripts 1.x.
- Mudanças que alterem assinatura, significado ou ciclo de vida exigem uma nova
  major, aviso no editor e guia de migração.
- O runtime mantém o isolamento de exceções: script com erro não deve derrubar o
  editor nem os demais scripts.
- IDs de assets, animações e entidades devem ser tratados como identificadores,
  não como texto visual mutável.

## Migração segura

1. Compile o projeto pelo botão de diagnóstico antes de trocar a versão.
2. Corrija erros de API usando os exemplos em `Docs/Scripting/Examples`.
3. Não duplique uma classe com o mesmo nome em duas pastas de scripts: todos os
   arquivos do projeto entram na mesma compilação Roslyn.
4. Ao renomear um script, mantenha o nome do tipo anexado no componente ou
   reanexe o novo tipo pelo Inspector.

## Escopo atual

Input, tempo, transform, física, colisões, áudio, luz, materiais, UI, animação,
eventos e navegação têm fachadas no SDK. Scene loading arbitrário, debugger por
linha e avaliação de expressões ainda não fazem parte da API 1.0.
