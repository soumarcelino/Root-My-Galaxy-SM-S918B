# Próximos passos: confiabilidade, velocidade, estabilidade e evolução

## Objetivo deste roadmap

O projeto já atingiu o marco principal: root end-to-end do código aberto em dois
reboots limpos, com landing, backend físico, usermode helper, KernelSU e
`su -c id` comprovados. O próximo estágio deixa de ser “fazer funcionar” e
passa a ser:

- medir a confiabilidade real;
- reduzir risco de regressão;
- tornar falhas diagnosticáveis sem alterar timing crítico;
- reduzir tempo operacional fora do exploit;
- aumentar estabilidade sob variações reais de boot/carga;
- tornar offsets, layouts e artefatos reproduzíveis;
- facilitar manutenção e port para novos firmwares;
- preservar fidelidade ao fechado.

O princípio principal é: otimização não pode destruir a geometria que tornou o
payload funcional. Mudanças em allocator, scheduling, ordem de syscalls,
lifetimes ou workqueue precisam ser experimentais até passarem por comparação
com o fechado e validação multi-boot.

## Baseline atual

### Comprovado

- 4 reboots limpos, 4 sucessos completos, em duas campanhas 2/2;
- landing da fake FOPS na primeira tentativa em ambos;
- pipe backend instalado na primeira tentativa em ambos;
- `root_umh` com `wake=1 complete=1 socket=1`;
- KernelSU `version=33214`, UAPI 2;
- `uid=0(root)` no contexto `u:r:ksu:s0`;
- nenhuma reinicialização durante os dois testes finais;
- build compartilhado estável com SHA-256 conhecido;
- runner corrigido contra falso negativo de `su` tardio.

### Ainda não medido

- taxa de sucesso em 10, 20 ou 50 boots;
- distribuição de tentativas para groom e pipe;
- latência p50/p95 de cada estágio;
- comportamento com device quente/frio, carga de I/O ou memória;
- incidência real da race de workqueue em uma amostra maior;
- estabilidade após atualização de firmware;
- reprodutibilidade binária entre hosts/toolchains;
- qualidade de recuperação automática de evidência após panic.

Quatro sucessos provam repetibilidade inicial, mas não sustentam afirmação estatística de
“99% confiável”. Mesmo 20 sucessos em 20 boots dão limite inferior aproximado
de 84% para a taxa real num intervalo Wilson de 95%. Para afirmações fortes,
será necessária amostra maior.

## Metas mensuráveis

| Área | Métrica | Meta inicial | Meta madura |
|---|---|---:|---:|
| Confiabilidade | boots com UID 0 / boots válidos | 20/20 | 50/50 |
| Segurança | panics/watchdogs | 0/20 | 0/50 |
| Landing | sucesso na tentativa 1 | ≥90% | ≥95% |
| Pipe backend | `ready` na tentativa 1 | ≥90% | ≥95% |
| Root stage | completion+socket após publish | 100% | 100% |
| Diagnóstico | falhas com código/estágio inequívoco | 100% | 100% |
| Falso negativo | KernelSU passou, runner reportou falha | 0 | 0 |
| Evidência | boots com manifesto completo | 100% | 100% |
| Tempo operacional | preparação host/ADB | <15 s | <8 s |
| Tempo pós-quiet-window | p95 até UID 0 | medir primeiro | reduzir ≥20% sem perder sucesso |

O tempo de espera pela janela quieta do allocator deve ser medido separadamente
do tempo ativo do exploit. Removê-lo para “ficar mais rápido” sem dados pode
reduzir drasticamente a confiabilidade.

## Priorização

| Prioridade | Trabalho | Impacto | Risco no exploit | Esforço |
|---|---|---|---|---|
| P0 | Arquivo de evidência e manifesto por boot | alto | nenhum | baixo |
| P0 | Orquestrador seguro de validação 2-reboots | alto | baixo | médio |
| P0 | Códigos estruturados de estágio/falha | alto | baixo se buffered | médio |
| P0 | Gate forte de firmware/kernel/artefato | alto | nenhum antes do trigger | médio |
| P0 | Testes host de layouts e transformações | alto | nenhum | médio |
| P1 | Centralizar perfil AFZH3 e gerar offsets | alto | baixo | médio |
| P1 | Telemetria temporal fora de janelas críticas | médio | baixo | médio |
| P1 | Endurecer snapshot/revalidação da workqueue | alto | médio | alto |
| P1 | Relatório automático de last_kmsg/pstore | alto | nenhum no exploit | médio |
| P1 | Reprodutibilidade de build/proveniência | médio | nenhum | médio |
| P2 | Campanha 20/50 boots | muito alto | operacional | alto |
| P2 | Matriz de carga/temperatura/uptime | alto | operacional | alto |
| P2 | Otimizações de latência | médio | alto | médio |
| P2 | Perfis gerados para novos firmwares | alto | alto por alvo | alto |
| P3 | CI privada com build e análise estática | médio | nenhum | médio |
| P3 | Refatoração das variantes/harnesses antigas | médio | médio | alto |

## Fase 0 — preservar o estado funcional

Antes de mudar código crítico, criar uma referência imutável.

### 0.1 Tag e release interna

Criar tag para o commit/artefato validado, com:

- commit exato;
- SHA-256 do `.so`;
- tamanho;
- NDK/API/clang;
- hashes de helper e ksud;
- dois boot IDs;
- logs completos;
- identidade do firmware/kernel;
- data e operador.

Critério de saída: qualquer pessoa autorizada consegue identificar exatamente
qual fonte produziu o artefato 2/2.

### 0.2 Arquivar evidências fora de `/tmp`

Criar estrutura sugerida:

```text
evidence/
  manifests/
  successful-boots/
  panics/
  kallsyms/
  btf/
  tool-versions/
```

Logs do device podem conter endereços específicos. Como o repositório é
privado, ainda assim convém separar evidência bruta de documentação pública e
definir política de retenção.

### 0.3 Branch experimental

Manter `main` como baseline funcional. Mudanças de allocator/timing devem entrar
em branch experimental e ser promovidas somente após:

1. build/checks host;
2. comparação de disassembly/ordem;
3. 2 reboots mínimos da mudança;
4. soak maior quando afetar scheduler/reclaim/workqueue.

## Fase 1 — validação automatizada e segura

### 1.1 Orquestrador de dois reboots

Criar ferramenta host, por exemplo `tools/validate_two_boots.sh`, que:

- exige serial explícito;
- lê hash do payload antes de qualquer reboot;
- confirma modelo, codinome, build e kernel;
- registra boot ID e uptime;
- exige `SU_ABSENT` no início;
- mantém state file para impedir segunda execução no mesmo boot;
- chama o `simple-root` existente, sem duplicar sua lógica;
- impõe timeout global;
- captura stdout/stderr sem renderização intermediária destrutiva;
- consulta `su -c id` e boot ID final;
- reinicia apenas entre Boot 1 e Boot 2;
- produz relatório final PASS 2/2, parcial ou falha terminal.

O orquestrador não deve decidir sozinho repetir após mutação terminal. Se o log
chegou a FOPS/list publication e falhou, encerrar aquele boot e coletar forense.

### 1.2 Manifesto por boot

Formato JSON recomendado:

```json
{
  "serial": "RXCX602E20X",
  "boot_id": "...",
  "firmware": "S918BXXSAFZH3",
  "kernel": "...",
  "payload_sha256": "...",
  "commit": "...",
  "started_without_su": true,
  "stages": {},
  "final_uid": 0,
  "boot_id_unchanged": true,
  "result": "PASS"
}
```

O manifesto deve apontar para o log raw, não copiar apenas linhas escolhidas.

### 1.3 Máquina de estados do runner

Em vez de depender apenas de regex livre, padronizar estados:

```text
PRECHECK
KASLR_READY
FOPS_INSTALLED
AAR_AAW_VERIFIED
GLOBAL_RESTORED
PIPE_READY
UMH_PUBLISHED
TEMP_ROOT_READY
KSU_READY
SU_READY
```

Cada falha recebe código estável, por exemplo `E_PIPE_VICTIM_CONFIRM` ou
`E_UMH_POOL_CHANGED`. Mensagens humanas podem evoluir sem quebrar automação.

Para não alterar timing, eventos críticos podem ser gravados em struct/buffer
pré-alocado e emitidos depois da janela sensível.

### 1.4 Captura automática após reboot inesperado

Quando ADB voltar com novo boot ID:

- classificar como reboot confirmado;
- salvar bootreason e uptime;
- obter last_kmsg/pstore usando root diagnóstico quando disponível;
- extrair call trace e bloco ftrace completo;
- associar os arquivos ao manifesto da execução anterior;
- impedir novo payload até a coleta terminar.

## Fase 2 — gates de identidade e artefato

### 2.1 Fingerprint forte do alvo

Antes do trigger, comparar:

- modelo/codinome;
- build incremental;
- `/proc/version`;
- hash ou conjunto de símbolos/offsets sentinela;
- BTF build ID quando disponível;
- acessibilidade e `st_rdev` do alias ashmem.

Falhar fechado em mismatch. Não usar “perfil mais próximo”.

### 2.2 Perfil de alvo centralizado

Hoje offsets aparecem em diferentes módulos. Criar um `target_profile` único:

```c
struct target_profile {
  const char *build;
  uint64_t ashmem_misc_fops;
  uint64_t real_ashmem_fops;
  uint64_t init_task;
  uint64_t selinux_enforcing;
  uint64_t system_unbound_wq;
  uint64_t call_usermodehelper_exec_work;
  uint64_t kmalloc_caches;
  uint64_t anon_pipe_buf_ops;
  /* layouts e fingerprints */
};
```

Benefícios:

- revisão de offsets em um local;
- comparação automática com manifestos;
- menor risco de atualizar um módulo e esquecer outro;
- port explícito para novo firmware.

Risco: refatoração do caminho funcional. Fazer primeiro como tabela gerada e
comparar byte a byte os valores usados, sem mudar ordem/runtime.

### 2.3 Gerador a partir de BTF/kallsyms

Criar ferramenta offline que gere um relatório, não um perfil automaticamente
aceito. Ela deve:

- ler BTF/kallsyms coletados;
- calcular offsets relativos à base;
- emitir diff contra o perfil versionado;
- marcar campos não deriváveis;
- exigir revisão humana para atualizar fonte.

Automação deve detectar divergência, não mascará-la.

### 2.4 Proveniência de build

Registrar no artefato ou arquivo adjacente:

- commit;
- árvore limpa/suja;
- clang/NDK/API;
- flags;
- timestamp reprodutível;
- SHA-256 das entradas críticas.

Meta: dois builds com mesmo ambiente produzirem artefatos comparáveis ou, no
mínimo, manifesto que explique qualquer diferença.

## Fase 3 — testes host e análise estática

### 3.1 Testes de layout

Cobrir sem device:

- tamanhos e offsets de `pipe_buffer`, completion e `subprocess_info`;
- cálculo `A`, `D`, `A+0x1180`, `A+0x6000`, `A+0x6200`, `A+0x7100`;
- conversão direct-map→vmemmap;
- split de leitura/escrita cruzando páginas;
- construção fake FOPS e work item;
- limites de path/argv;
- validação/clamp de variáveis de ambiente.

Esses testes devem compartilhar helpers puros, sem copiar a implementação para
o teste e validar apenas uma duplicata.

### 3.2 Testes de máquina de estados

Simular logs/resultados:

- sucesso completo;
- `su` tardio;
- reboot no meio;
- boot ID trocado;
- mismatch de firmware;
- failure depois de list write;
- timeout antes de mutação;
- pipe miss 12/12.

### 3.3 Análise estática

Adicionar checks privados:

- `clang --analyze` ou clang-tidy focado em lifetime/FDs;
- warnings tratados como erro em módulos produtivos;
- shellcheck para runner/orquestrador;
- verificação de links Markdown;
- busca de segredos antes do push;
- limite de tamanho/arquivos binários inesperados.

Sanitizers são úteis em harnesses host, mas não devem ser ativados no payload
final sem medir como mudam layout e timing.

### 3.4 Testes de fault injection fora do kernel real

Abstrair operações de read/write para um backend simulado e injetar:

- short read/write;
- EINTR;
- falha ao restaurar `pipe_buffer`;
- mudança de worklist entre snapshots;
- ponteiros fora do direct-map;
- counters no limite;
- holder morrendo cedo.

Objetivo: provar que o código aborta no estágio correto e nunca promove falha
parcial a sucesso.

## Fase 4 — confiabilidade do caminho crítico

### 4.1 Telemetria sem perturbação

Pré-alocar uma estrutura pequena por tentativa com timestamps monotônicos e
códigos numéricos. Em janelas críticas, apenas stores atômicos simples. Emitir
texto após o estágio.

Medir:

- KASLR;
- primeiro groom;
- futex route;
- verify/restore;
- pipe prepare/scan/proof;
- UMH wait/publish/completion/socket;
- KernelSU late-load;
- disponibilidade de `su`.

Sem baseline temporal, qualquer otimização de velocidade é especulação.

### 4.2 Razões internas do pipe miss

Hoje `setup miss` agrega várias causas. Criar enum interno:

- `PIPE_E_PREPARE`;
- `PIPE_E_CACHE_SELECT`;
- `PIPE_E_MARKER_WRITE`;
- `PIPE_E_VICTIM_SCAN`;
- `PIPE_E_VICTIM_CONFIRM`;
- `PIPE_E_READ_PROOF`;
- `PIPE_E_WRITE_PROOF`;
- `PIPE_E_RESTORE`.

Emitir depois do cleanup. Isso mostra onde investir sem adicionar prints no
allocator crítico.

### 4.3 Snapshot consistente da workqueue

Antes de publicar, coletar duas leituras consecutivas de:

- `worklist.next/prev`;
- `nr_idle`;
- `work_color`;
- `refcnt`;
- `nr_in_flight[color]`;
- `nr_active/max_active`;
- `pwq->wq` e `pwq->pool`.

Aceitar apenas snapshots idênticos e válidos. Avaliar custo temporal: mais
leituras podem ampliar a janela total. A proposta deve ser comparada com a
sequência exata do fechado e testada como variante opt-in antes de virar
default.

### 4.4 Minimizar janela publish→wake

Auditar assembly gerado para confirmar que entre:

```text
worklist.prev
worklist.next
wake_system_unbound
```

não existem logs, malloc, lazy binding ou inicializações inesperadas. Opções a
avaliar:

- preparar todos os dados/FDs antes;
- resolver símbolos/libc antes da janela;
- pré-abrir recursos necessários ao wake, se isso continuar fiel e não mudar a
  workqueue acionada;
- colocar rotina crítica em função pequena e `noinline` para auditoria.

Não aplicar `mlockall`, realtime priority ou outras mudanças globais sem prova;
elas podem mudar allocator/scheduler mais do que ajudam.

### 4.5 Holder e cleanup explícitos

Documentar e testar:

- quais FDs cada holder precisa herdar;
- como detectar morte prematura;
- quando é seguro encerrar;
- comportamento se late-load falha após root temporário;
- cleanup apenas após reboot ou prova de ausência de referências.

Adicionar heartbeat fora do caminho crítico pode melhorar diagnóstico, mas não
deve fechar recursos automaticamente por timeout.

### 4.6 Revisão dos acessos irreversíveis

Classificar cada write como:

- reversível antes de publicação;
- reversível apenas se estado não mudou;
- irreversível/ambíguo.

Codificar isso na máquina de estados para impedir retry automático depois de:

- global FOPS potencialmente alterada sem restauração confirmada;
- primeira list write;
- completion ambígua;
- falha de restauração do victim `pipe_buffer`.

## Fase 5 — velocidade sem sacrificar sucesso

### 5.1 Primeiro medir

Coletar pelo menos 10 boots do baseline antes de mudar waits. Separar:

- boot→quiet-window;
- quiet-window→KASLR;
- KASLR→landing;
- landing→pipe ready;
- pipe ready→temporary root;
- late-load→`su`.

### 5.2 Otimizações seguras fora do exploit

Priorizar:

- não reenviar assets se hash remoto já for idêntico;
- build incremental quando fonte não mudou;
- coletar propriedades ADB independentes em uma única shell;
- gerar manifesto e relatório em paralelo após sucesso;
- detectar boot completion por property em vez de sleep fixo;
- parar poll de `su` imediatamente no primeiro UID 0;
- comprimir/arquivar logs depois do teste;
- reutilizar dados estáticos do host, nunca endereços runtime de outro boot.

Essas mudanças reduzem tempo operacional sem tocar allocator.

### 5.3 Otimizações de risco médio

Somente depois de medir:

- reduzir scans redundantes mantendo provas;
- ordenar leitura do slab para priorizar páginas/candidatos mais prováveis;
- substituir sleeps fixos por condições observáveis fora da janela crítica;
- ajustar timeout do socket com base em distribuição p99;
- usar I/O vetorizado onde não altera chamadas esperadas pelo kernel alvo.

Cada uma precisa de A/B multi-boot.

### 5.4 O que não otimizar cedo

- contagens 1024/192/31/32;
- skb `0x8e80`;
- bancos 240+240;
- ordem reclaim→PCP;
- repin CPU0;
- callback no waiter;
- restauração global antes do root stage;
- provas bidirecionais do pipe;
- revalidação da workqueue;
- primeiro wake imediato;
- holder lifetime.

Esses itens são parte da correção, não desperdício evidente.

## Fase 6 — campanha de estabilidade

### 6.1 Soak 20 boots

Executar 20 boots limpos com o mesmo artefato, distribuídos entre:

- device frio e aquecido;
- após boot recém-concluído e após alguns minutos;
- conectado a carregador e bateria;
- baixa carga e carga controlada de I/O/memória;
- diferentes slides KASLR naturais.

Não alterar código durante a campanha. Qualquer alteração reinicia a série.

### 6.2 Soak 50 boots

Depois de 20/20 sem panic, executar 50 boots para uma estimativa mais útil.
Registrar:

- tentativa do primeiro groom;
- tentativa do pipe;
- tempos por estágio;
- victim index;
- slide KASLR;
- temperatura/uptime de forma somente-leitura;
- resultado final.

### 6.3 Matriz de stress

Stress deve ser opt-in e nunca misturado à prova baseline. Exemplos:

- I/O UFS moderado;
- pressão de memória controlada;
- atividade de workqueue;
- device quente dentro de faixa segura;
- ADB reconectado durante fases não críticas.

Objetivo: encontrar limites, não mudar default baseado em um único miss.

### 6.4 Critério para promover mudança

- mudança sem caminho crítico: build/checks + 2 reboots;
- mudança em logging/runner: 2 reboots e zero falso negativo;
- mudança em groom/pipe/futex/workqueue: A/B + mínimo 10 boots por variante;
- mudança de perfil/firmware: validação completa do novo alvo + 20 boots antes
  de chamar estável.

## Fase 7 — arquitetura e manutenção

### 7.1 Separar produção de variantes históricas

`07_futex_pi_trigger.c/.h` acumulam variantes e documentação extensa. Preservar
histórico, mas considerar:

- `futex_trigger_prod.c` com v14;
- `futex_trigger_variants.c` somente para bisseção;
- headers menores com API pública;
- build produtivo sem variantes não usadas;
- build diagnóstico que mantém todas.

Risco: refatoração muda layout/código gerado. Fazer depois do soak baseline e
comparar disassembly do caminho v14.

### 7.2 Ownership explícito de recursos

Criar structs por estágio com init/cleanup claros:

- primeiro groom;
- pipe banks/holder;
- fd ashmem;
- threads futex;
- fake work state.

Evitar cleanup genérico após estado terminal. Cada struct deve conhecer fase e
quais recursos ainda podem ser fechados.

### 7.3 APIs com resultado rico

Trocar gradualmente `int` genérico por:

```c
struct stage_result {
  enum stage stage;
  enum error_code error;
  int sys_errno;
  bool mutation_started;
  bool retry_safe;
};
```

Isso torna a decisão de retry explícita e reduz interpretação por texto.

### 7.4 Documentação ligada ao código

- manter tabela de offsets gerada;
- apontar cada constante para função/endereço fechado;
- registrar divergências conscientes como ADRs;
- atualizar roadmap com métricas reais;
- manter `STATUS.md` cronológico, sem reescrever falhas antigas.

## Fase 8 — CI privada e cadeia de fornecimento

### 8.1 CI sem device

Pipeline privado:

1. secret scan;
2. link check Markdown;
3. shellcheck;
4. build PIE/shared object;
5. warnings/análise estática;
6. testes host;
7. manifesto e hashes;
8. upload de artefato privado.

Nenhum job deve executar exploração em runner genérico.

### 8.2 Assinatura e integridade

- assinar tags/releases internas;
- armazenar SHA-256 de payload/helper/ksud;
- opcionalmente assinar manifesto;
- verificar hashes antes de push para o device;
- rejeitar asset diferente do manifesto aprovado.

### 8.3 Dependências

Versionar ou registrar:

- NDK;
- Ghidra/radare2 usados na derivação;
- fonte Samsung e hash do snapshot;
- binário fechado e hash, em armazenamento privado apropriado;
- scripts de geração e relatórios.

## Portabilidade para novos firmwares

Criar um processo em cinco gates:

1. **Identidade:** modelo/build/kernel/BTF.
2. **Derivação estática:** símbolos, estruturas, constantes e fechado
   correspondente.
3. **Checks somente-leitura:** KASLR, kallsyms, caches, ranges e ashmem.
4. **Harnesses controlados:** estágios sem root completo, um por vez.
5. **Validação:** dois boots mínimos; 20 boots antes de declarar estável.

Nunca usar fallback automático para o perfil AFZH3 em firmware diferente.

## Backlog recomendado em ordem

1. Criar tag/manifesta do baseline 2/2.
2. Mover logs finais de `/tmp` para arquivo persistente privado.
3. Implementar orquestrador 2-reboots com state file por boot ID.
4. Padronizar códigos de estágio/falha no runner.
5. Adicionar gate de firmware/kernel/hash.
6. Adicionar link check, shellcheck e testes de layout.
7. Criar perfil AFZH3 centralizado sem mudar valores/runtime.
8. Criar relatório offline BTF/kallsyms versus perfil.
9. Adicionar telemetria buffered e baseline de 10 boots.
10. Detalhar razões de pipe miss.
11. Automatizar captura last_kmsg/pstore após reboot inesperado.
12. Fazer soak baseline 20 boots.
13. Experimentar snapshot duplo da workqueue em branch opt-in.
14. Auditar assembly publish→wake.
15. Fazer A/B de qualquer melhoria crítica, 10 boots por variante.
16. Otimizar staging/ADB/build fora do exploit.
17. Fazer soak 50 boots.
18. Separar produção de variantes históricas.
19. Implantar CI privada e assinatura de artefatos.
20. Só então iniciar perfil de outro firmware.

## Definição de pronto por categoria

### Mais confiável

- 20/20 boots, depois 50/50;
- zero panic;
- cada falha tem estágio/código;
- nenhum retry após estado irreversível;
- evidência completa por boot.

### Mais estável

- baseline e stress separados;
- comportamento conhecido em carga/temperatura/uptime variados;
- workqueue sem corrupção observada no soak;
- holders/lifetimes documentados e monitorados;
- recuperação de panic automatizada.

### Mais rápido

- ganho medido em p50/p95;
- mesma ou melhor taxa de sucesso;
- nenhuma alteração não medida em geometria/timing crítico;
- melhoria preferencialmente no staging, build e detecção.

### Mais fácil de manter

- perfil centralizado;
- offsets comparáveis automaticamente;
- testes host úteis;
- APIs com erro rico;
- produção separada de variantes;
- documentação e manifestos atualizados.

### Mais seguro para pesquisa

- alvo/firmware verificados antes do trigger;
- execução única por boot aplicada por ferramenta;
- reboots finais sempre em pares;
- coleta antes de retry;
- repositório, artefatos e evidências privados;
- hashes verificados em todas as etapas.

## Recomendação imediata

Não modificar ainda groom, futex, pipe ou workqueue. O orquestrador já foi
validado numa campanha 2/2; agora preservar o baseline e completar 10 boots
com telemetria externa. Esses dados mostrarão se o próximo investimento deve
ser landing, pipe, workqueue ou apenas tempo operacional. Sem baseline, uma
“melhoria” pode parecer rápida em um boot e reduzir a taxa real de sucesso.
