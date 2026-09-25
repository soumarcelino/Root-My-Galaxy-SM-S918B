# Documentação técnica do `targets/afzh3/open-payload-engine`

Este diretório documenta a reconstrução clean-room do payload fechado usado no
Galaxy S23 Ultra `SM-S918B`, codinome `dm3q`, firmware
`S918BXXSAFZH3`. O objetivo do projeto é reproduzir o comportamento observável
do binário fechado com código-fonte auditável, preservando ordem, geometria,
lifetimes, offsets e critérios de sucesso relevantes para o kernel.

## Estado comprovado

O código aberto alcançou root end-to-end no aparelho de laboratório em quatro
reboots limpos, distribuídos em duas campanhas independentes de 2 reboots. Em
todos os casos:

- o aparelho iniciou sem `/system/bin/su`;
- o payload foi executado pelo fluxo normal `simple-root` via ADB shell;
- KASLR foi localizado por tracefs;
- o reclaim do objeto fake FOPS caiu na região prevista;
- AAR/AAW foi confirmado e `ashmem_misc.fops` foi restaurado;
- o backend físico por `pipe_buffer` foi instalado e provado nos dois sentidos;
- `root_umh` publicou o item de workqueue e recebeu completion/socket;
- KernelSU late-load confirmou `version=33214`;
- `/system/bin/su -c id` retornou `uid=0(root)` e contexto `u:r:ksu:s0`.

Artefato validado:

```text
SHA-256 a22ff696a2c096a45c62fbc0bd9c4bf8918d9783886d7227637a5ca980f8a53c
Arquivo  build/payload.so
Tamanho  101120 bytes
```

## Índice

1. [Escopo e metodologia](01-ESCOPO-E-METODOLOGIA.md): objetivo, fontes,
   hierarquia de evidência e critérios de fidelidade.
2. [Arquitetura e fluxo](02-ARQUITETURA-E-FLUXO.md): visão completa desde o
   constructor até KernelSU e `su`.
3. [Causas raiz e correções](03-CAUSAS-RAIZ-E-CORRECOES.md): quatro bloqueios
   principais, falhas menores e hipóteses descartadas.
4. [Backend físico por pipes](04-PIPE-PHYSRW.md): geometria, reclaim,
   descoberta do victim, leitura/escrita e lifetimes.
5. [Root UMH e workqueue](05-ROOT-UMH-WORKQUEUE.md): construção do
   `subprocess_info`, validações, publicação e confirmação.
6. [Mapa de fidelidade ao binário](06-MAPA-FIDELIDADE.md): correspondência
   entre funções fechadas, código aberto, constantes e desvios conscientes.
7. [Validação no device](07-VALIDACAO-DEVICE.md): protocolo 2 reboots,
   evidências e critérios de aceitação.
8. [Runbook operacional](08-RUNBOOK.md): build, staging, execução, coleta e
   encerramento de um teste.
9. [Troubleshooting e forense](09-TROUBLESHOOTING-E-FORENSE.md): diagnóstico
   por estágio, panic, last_kmsg, falsos negativos e recuperação.
10. [Limitações e manutenção](10-LIMITACOES-E-MANUTENCAO.md): escopo de
    compatibilidade, invariantes que não devem ser alteradas e processo de
    mudança.
11. [Ferramentas e evidências](11-FERRAMENTAS-E-EVIDENCIAS.md): Ghidra,
    radare2, kernel source, BTF/kallsyms, ADB, ftrace, last_kmsg, runner e as
    evidências que concretizaram as correções.
12. [Próximos passos e roadmap](12-PROXIMOS-PASSOS-E-ROADMAP.md): plano
    priorizado para confiabilidade, velocidade, estabilidade, testes,
    observabilidade, manutenção, CI e novos firmwares.
13. [Ferramentas de porting e debug](../tools/README.md): scripts reutilizáveis
    para preflight, forense, comparação ELF, manifests, perfis, análise de logs,
    checks locais e validação em dois reboots.

## Código principal

| Arquivo | Responsabilidade |
|---|---|
| `src/main.c` | Supervisor, limites, tentativa completa e callback imediato. |
| `src/kaslr.c` | Descoberta da base KASLR por tracefs. |
| `src/groom.c` | Reclaim order-3 e instalação do objeto fake FOPS. |
| `src/fops_install.c` | Layout byte a byte do buffer reclamado. |
| `src/futex_trigger.c` | Trigger futex/FPSIMD, variante v14 e serviço assíncrono. |
| `src/aar_aaw.c` | Alias ashmem, AAR/AAW inicial e restauração. |
| `src/pipe_physrw.c` | Backend físico por pipes equivalente ao estágio fechado. |
| `src/root_umh.c` | Publicação da workqueue e execução do helper de root. |
| `Makefile` | Build PIE e `LD_PRELOAD` com NDK Android. |

## Convenções desta documentação

- `A`: base order-3 alinhada, `candidate & ~0x7fff`.
- `D`: início dos dados do skb, `A - 0xe80`.
- `page_base` ou `payload_base`: endereço `A` usado pelos layouts fake.
- `fd ashmem`: descritor já aberto que conserva o fake `f_op` mesmo após a
  restauração do ponteiro global.
- “configfs AAR/AAW”: primitiva inicial, adequada aos acessos estáticos que o
  binário fechado também realiza por esse caminho.
- “pipe R/W”: primitiva física instalada depois, usada para objetos SLUB
  dinâmicos e dados de workqueue.
- “reboot limpo”: novo `boot_id`, boot concluído, firmware conferido e `su`
  ausente antes da execução.

`STATUS.md` permanece como diário cronológico. Estes documentos descrevem o
estado final consolidado e devem ser atualizados quando uma invariante ou
prova mudar.
