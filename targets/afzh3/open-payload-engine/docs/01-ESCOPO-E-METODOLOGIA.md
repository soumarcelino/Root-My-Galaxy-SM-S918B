# Escopo e metodologia

## Objetivo

O projeto reconstrói em C o comportamento do payload fechado
`cve-2026-43499-app-afzh3.so`/`ksu-payload` para o alvo
`dm3q-S918BXXSAFZH3`. “Funcionar” exige a cadeia completa:

1. executar como ADB shell sem root prévio;
2. localizar a base do kernel;
3. preparar o allocator e reclamar o objeto esperado;
4. disparar o caminho futex/scheduler;
5. provar acesso de leitura e escrita ao kernel;
6. restaurar ponteiros globais temporariamente alterados;
7. instalar uma primitiva adequada para memória SLUB dinâmica;
8. executar o helper de root via workqueue;
9. late-load do KernelSU;
10. confirmar `su -c id` com UID 0.

Build, carregamento da biblioteca, logs de estágio ou existência de processos
holder não são prova isolada de root.

## Natureza clean-room

A implementação usa observações do binário fechado como especificação de
comportamento. As principais fontes são:

- decompilação Ghidra;
- disassembly AArch64 bruto para pontos em que tipos ou fluxo de alto nível
  ficaram ambíguos;
- bytes de `.rodata` e `.data` para tabelas e constantes;
- offsets e layouts comparados com BTF, kallsyms e fonte Samsung disponível;
- comportamento real do payload fechado no mesmo aparelho;
- logs do clone, tracefs, `/proc/last_kmsg` e provas de root.

O código registra a origem com comentários `source: FUN_...`. Esses comentários
fazem parte da rastreabilidade: uma alteração em lógica derivada do fechado
deve manter a referência ou explicar por que a referência deixou de valer.

## Hierarquia de evidência

Quando fontes divergem, usar a seguinte prioridade:

1. **Instruções e bytes do binário fechado.** Definem ordem, constantes e
   branches efetivamente distribuídos.
2. **Execução observada do fechado no alvo exato.** Resolve efeitos de runtime,
   allocator e sincronização que o decompiler não mostra bem.
3. **BTF/kallsyms do kernel em execução.** Definem símbolos e layouts reais do
   boot testado.
4. **Fonte Samsung compatível.** Explica semântica interna, mas não prova que o
   pacote local é byte a byte a fonte do kernel instalado.
5. **Hipóteses do clone.** Só podem virar default depois de evidência e teste.

## Classes de afirmação

Cada conclusão deve ser interpretada por sua força:

| Classe | Significado | Exemplo |
|---|---|---|
| Confirmado estaticamente | Presente em bytes/disassembly do fechado. | `0x8e80` no envio skb. |
| Confirmado no device | Observado diretamente no alvo. | `uid=0(root)` em dois boots. |
| Confirmado por fonte | Semântica sustentada pelo kernel compatível. | offsets de `pool_workqueue`. |
| Inferência forte | Várias evidências convergem, sem observação direta completa. | efeito allocator de uma ordem de socketpairs. |
| Hipótese | Explicação ainda não provada. | causa de uma nova falha sem last_kmsg. |

Documentação e logs não devem promover hipótese a fato.

## Critérios de fidelidade

Fidelidade relevante não significa apenas copiar chamadas. Inclui:

- ordem de criação de socketpairs;
- CPU onde alocação e liberação ocorrem;
- quantidade de objetos por slab e ondas pre/post;
- tamanho do skb;
- número de pipes e slots;
- lifetime de processos, memfds, sockets e pipes;
- momento exato da restauração de ponteiros;
- backend usado para cada classe de memória;
- ausência de logs/syscalls em janelas críticas;
- condição que torna uma mutação irreversível;
- critérios de completion e root real.

Uma implementação pode ter os mesmos offsets e ainda falhar por executar a
mesma operação em outra CPU, em outra ordem ou com um objeto já liberado.

## Identidade do alvo

Validação final registrada:

```text
Produto:       Samsung Galaxy S23 Ultra
Modelo:        SM-S918B
Codinome:      dm3q
Firmware:      S918BXXSAFZH3
Kernel:        5.15.189-android13-8-33413713-abS918BXXSAFZH3
Serial ADB:    RXCX602E20X
Contexto root: u:r:ksu:s0
```

Os offsets deste repositório são específicos desse alvo. Compatibilidade de
versão principal, SoC ou família não autoriza reutilização dos offsets em
outro firmware.

## Critério de aceitação final

Um resultado só é final quando passa em dois reboots limpos independentes.
Cada execução deve registrar:

- `boot_id` diferente;
- `sys.boot_completed=1`;
- build incremental `S918BXXSAFZH3`;
- ausência de `su` antes do payload;
- log completo do payload;
- `temporary-root-ready`;
- confirmação do controle KernelSU;
- `/system/bin/su -c id` com `uid=0(root)`;
- mesmo `boot_id` antes e depois, provando ausência de reboot oculto.

Essa regra reduz falso positivo por estado residual e falso negativo causado
pelo atraso entre late-load e publicação de `/system/bin/su`.

## O que a validação não prova

Dois boots provam funcionamento no aparelho, firmware, artefato e condições
registradas. Não provam:

- universalidade em todos os SM-S918B;
- compatibilidade com outro patch level;
- ausência absoluta de race sob toda carga possível;
- identidade byte a byte entre fonte Samsung local e kernel instalado;
- segurança para uso fora do laboratório autorizado.

## Processo para nova mudança

1. Identificar função/offset fechado associado.
2. Registrar evidência estática ou runtime.
3. Alterar a menor camada responsável.
4. Compilar PIE e shared object sem warnings novos.
5. Fazer checagens não destrutivas primeiro.
6. Executar apenas uma vez naquele boot.
7. Coletar log e forense antes de repetir.
8. Para aceitação, repetir em dois reboots limpos.
9. Atualizar estes documentos e `STATUS.md`.

Desvios experimentais devem ficar opt-in, por variável de ambiente ou harness,
até serem comparados com o fechado e validados no device.
