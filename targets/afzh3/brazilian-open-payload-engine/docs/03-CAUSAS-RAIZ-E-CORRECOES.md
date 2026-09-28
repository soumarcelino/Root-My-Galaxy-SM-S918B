# Causas raiz, correções e hipóteses descartadas

## Resumo

O clone não falhava por um único offset. Diferenças independentes
impediam a cadeia completa ou causavam panic depois que o estágio anterior
passava.

| Bloqueio | Sintoma | Evidência decisiva | Correção |
|---|---|---|---|
| Afinidade dividida entre CPUs | reclaim não caía ou fake FOPS ficava inconsistente | comportamento do SLUB e falha `factory-affinity` | uma CPU dinâmica para create/free/drain/reclaim |
| Comprimento AAR ampliado pelo deslocamento do ponteiro | panic HARDENED_USERCOPY em `kmalloc-128`, offset 8, size 221 | registrador `x19=0xdd`, alvo e geometria reproduzidos | ponteiro exato, encoder de NULs e invariante `kernel_check_len == len` |
| Backend errado para SLUB dinâmico | panic HARDENED_USERCOPY em `pool_workqueue` | last_kmsg com `configfs_read_iter` lendo `pwq+0` | pipe physical R/W para campos dinâmicos |
| Geometria/ordem do pipe divergente | victim instável ou backend não instalava | comparação de decompile/disassembly e allocator real | 2×240 pipes, 32 slots, ordem reclaim→PCP, pre31/post32 |
| Reclaim não provado | `try_module_get` recebeu owner residual `0x1270` | last_kmsg e página alvo sem payload completo | `close_range` imediato e delta exato do slab |
| Owner residual na posição antiga | panic em `try_module_get -> misc_open` | BTF mostrou `A+0x1180` sobre `saved_auxv[3]` | FOPS em `A+0x1260`, recuperação por segundo write e quarentena nula |
| Publicação manual da workqueue | `list_del corruption` em `cancel_work_sync` | last_kmsg mostrou lista global corrompida | PTY privado e `schedule_work()` nativo |

## 1. Afinidade dividida no primeiro groom

### Sintoma

O trigger podia executar sem que `ashmem_misc.fops` apontasse para o objeto
esperado. Em outras execuções, um objeto parcialmente correto chegava ao
callback, mas campos residuais causavam falha ao abrir ashmem.

### Causa

O fluxo selecionava uma CPU rápida para o bulk e CPU0 para a fase crítica.
Além disso, uma fábrica podia receber um cpuset que não aceitava a CPU pedida.
Assim, a liberação das páginas `mm_struct` e a alocação do skb de reclaim
podiam acontecer em PCPs diferentes.

Como as page lists são per-CPU, mesma geometria e mesmos tamanhos não bastam se
free e allocate acontecem em CPUs distintas.

### Correção

- a CPU mais rápida é escolhida dentro do cpuset permitido;
- fábrica, alvo, liberações, drains e reclaim usam essa mesma CPU;
- o PID fábrica confirma que a CPU continua permitida após cada `execve`;
- deriva de afinidade aborta a tentativa antes da mutação do kernel.

### Prova

Nos dois boots da versão então validada:

```text
[aar_aaw] ashmem_misc_fops readback got=<A+0x1180> want=<A+0x1180>
[aar_aaw] verify ok: corruption landed, R/W primitive live
```

A fonte atual move o valor esperado para `A+0x1260`; essa mudança ainda exige
uma campanha de boots limpa.

## 2. Uso de configfs em objetos SLUB dinâmicos

### Sintoma

Depois que o landing passou, o aparelho reiniciava durante `root_umh`.
Inicialmente isso parecia uma falha genérica da workqueue.

### Evidência decisiva

O last_kmsg do clone mostrou HARDENED_USERCOPY envolvendo cache
`pool_workqueue`, com stack no caminho `configfs_read_iter`. A leitura fatal era
de um campo dinâmico, equivalente a `pwq+0`.

O fechado não usa sua primitiva configfs para toda a workqueue. Ele instala
primeiro o backend físico por `pipe_buffer` e usa esse backend para estruturas
dinâmicas.

### Causa

Configfs impõe regras de usercopy sobre objetos SLUB. Um endereço de
`pool_workqueue` pode ser kernel-readable, mas não autorizado como origem de
cópia para userspace naquele cache/layout. A falha era do caminho de cópia,
não do valor do ponteiro.

### Correção

`root_umh` agora separa acessos:

| Dado | Backend |
|---|---|
| `selinux_enforcing` | configfs AAW, igual ao fechado |
| início de `init_task.tasks` e ops estáticas | configfs AAR |
| task, files, fdtable e PTY | pipe R/W |
| fake tty ops e dados UMH | pipe R/W |
| completion | pipe R/W |

Essa separação removeu o panic HARDENED_USERCOPY.

### 2.1. Comprimento ampliado em uma leitura válida

Outro panic no mesmo `configfs_read_iter` tinha causa diferente. A leitura de
oito bytes do slot de fd em `ffffff8923636588` usava um ponteiro deslocado 213
bytes para eliminar NULs da representação. O offset do arquivo era reduzido
em 213, preservando o endereço final, mas o kernel validava
`buffer->count - ki_pos = 8 + 213 = 221` antes de o iterador limitar a cópia a
oito bytes. Isso produziu exatamente:

```text
usercopy: Kernel memory exposure attempt detected from SLUB object
'kmalloc-128' (offset 8, size 221)!
```

O encoder já existente consegue publicar ponteiros com NULs por escritas de
prefixos em ordem decrescente. A AAR passou a usar o ponteiro exato e só chama
`pread64()` se provar `page + offset == target` e
`OSS_CONFIGFS_COUNT - offset == len`. Assim, o comprimento visto pelo Hardened
Usercopy é matematicamente idêntico ao comprimento pedido. O teste de host
reproduz os 221 bytes antigos e exige oito bytes no plano novo. Três boots
distintos atravessaram AAR, restauração da FOPS e resolução do pipe sem o
marcador de usercopy ou panic. Detalhes e evidências estão em
[AAR ConfigFS com comprimento exato](16-CONFIGFS-AAR-EXACT-LENGTH.md).

## 3. Backend pipe incompleto ou geometricamente diferente

### Sintoma

Uma implementação “equivalente” do pipe spray não encontrava victim estável,
travava aguardando serviço ou produzia reclaim diferente do fechado.

### Diferenças encontradas

- ordem dos socketpairs invertida;
- pre/post representados por filhos vivos em vez de clone/open/kill com memfd;
- ausência de segunda fixação CPU0;
- lifetime insuficiente dos bancos;
- falta de confirmação ativa do victim;
- espera infinita em harnesses sem o loop v14;
- provas de R/W incompletas.

### Correção final

- dois bancos de 240 pipes;
- pipes criados com 2 slots e redimensionados para 32;
- `mm_struct` com tamanho `0x400`, order 3, 32 objetos por slab;
- preparação 1024, spray 192, onda pre31 e post32;
- skb `0x8e80`;
- socketpair reclaim criado antes do PCP;
- um PID fábrica gera pre31/post32 por `execve`, com os mm mortos presos por `/proc/<pid>/mem`;
- scan do slab selecionado contra caches kmalloc-2k normal/cgroup;
- marker por comprimento 1..240;
- confirmação de um byte `0x6c` e incremento esperado de `len`;
- provas ASCII de 0x17 bytes e provas `uint64_t` em `A+0x7100`;
- até 12 tentativas, com cleanup integral entre misses;
- fallback de serviço após 5 s somente para harness standalone.

### Prova

Boots finais:

```text
[pipe_rw] ready attempt=1/12 page=<direct-map> victim=<direct-map> pipe=107
[pipe_rw] ready attempt=1/12 page=<direct-map> victim=<direct-map> pipe=81
```

Victims diferentes são normais entre boots; o importante é a descoberta e a
prova do backend, não um índice fixo.

## 4. Reclaim do `mm_struct` sem prova

### Sintoma

O panic em `misc_open -> try_module_get` recebeu `fops->owner = 0x1270`.
O endereço residual demonstra que o slab alvo não foi substituído
completamente pelo skb esperado.

### Causa

Depois de liberar os siblings, o fluxo executava `sched_yield()` antes de
fechar o alvo e novamente antes do primeiro spray. No SLUB, uma página vazia
pode permanecer congelada na lista parcial da CPU. Outro `mm_struct` podia
ocupar a página durante essa janela.

A antiga janela quieta provava apenas ausência de mudanças depois do spray;
ela não provava que o slab específico tinha voltado ao allocator.

### Correção

- pre31 e post32 são produzidos pelo mesmo PID fábrica;
- 62 referências vizinhas são fechadas mantendo o descritor vazado vivo;
- 38 referências pertencentes a slabs distintos forçam o flush de
  `cpu_partial` enquanto o alvo ainda contém o objeto conhecido;
- o descritor vazado é liberado sozinho por `close_range()`;
- o primeiro `sendmsg()` ocorre imediatamente, sem yield, log ou alocação;
- o gate exige queda exata de 32 objetos totais, um slab ativo e um slab total;
- `active_objs` é somente telemetria porque exclui objetos livres per-CPU;
- qualquer diferença aborta antes do futex e da mutação global.

A prova de trace, os protótipos descartados e a validação 6/6 estão em
[Reclaim determinístico e CPU discovery](15-RECLAIM-DETERMINISTICO-E-CPU-DISCOVERY.md).

## 5. Race na publicação da workqueue

### Sintoma

Um panic mostrou `__list_del_entry_valid -> try_to_grab_pending ->
cancel_work_sync`, enquanto `bwmon_intr_thread` manipulava uma workqueue. O
`bwmon` foi o consumidor que detectou a lista já corrompida.

### Causa

O payload escrevia `nr_in_flight`, `nr_active`, `refcnt` e os dois links de
`pool->worklist` sem `pool->lock`. Três snapshots estáveis não substituem o
lock: uma IRQ ou qualquer produtor pode alterar a lista entre duas escritas.

### Correção

- remover todos os acessos a `system_unbound_wq`, pool, pwq, lista e contadores;
- usar o `SAK_work` de um PTY privado como `subprocess_info` temporário;
- trocar temporariamente `tty_operations.flush_buffer` por `do_SAK`;
- acionar `TCFLSH/TCOFLUSH`, chegando a `schedule_work()` com assinatura CFI
  compatível;
- restaurar ops imediatamente e os 112 bytes do PTY depois da completion;
- manter o PTY aberto se qualquer restauração ficar ambígua.

Agora o próprio kernel executa `insert_work()` segurando `pool->lock`.

## Correções menores relevantes

### Restauração de `ashmem_misc.fops`

O ponteiro global agora volta para a tabela estática real logo após a prova
AAR/AAW. O descritor aberto continua usando a fake FOPS. Isso reduz dangling
global e evita que novas aberturas usem memória de reclaim.

A FOPS primária foi movida de `A+0x1180`, que coincide com
`mm_struct.saved_auxv[3]`, para `A+0x1260`. Nesse offset, owner e os callbacks
críticos caem em zeros confirmados do `saved_auxv` e do `mm_struct` desmontado.
Uma segunda FOPS em `A+0x1660` permite refazer a publicação pelo mesmo waiter
e restaurar o ponteiro real quando a primeira tabela não abre ou não prova
AAR/AAW. A escrita nula final é fail-closed.

### Limpeza do owner fake

Depois de `root_umh`, o campo owner em `A+0x1260` é zerado pelo mesmo fd. O
estágio só reporta sucesso se a limpeza também passa.

### Logs fora das janelas críticas

`fprintf` entre ondas de close/drain/reclaim foi removido ou consolidado.
Logging pode alterar scheduling, alocação libc e timing de syscalls.

### Serviço do backend no loop v14

O callback solicita preparação e o loop principal atende a cada 10 ms. Isso
evita deadlock de tentar executar toda a preparação no contexto errado.

### Falso negativo no runner

O primeiro boot alcançou KernelSU, mas a consulta imediata a `su` ocorreu cedo
demais. Pouco depois, `su -c id` já retornava UID 0. `simple-root` agora espera
até 30 s e exige explicitamente `uid=0(root)`.

## Hipóteses refutadas ou incompletas

| Hipótese | Estado | Motivo |
|---|---|---|
| self-reference de `lock.waiters` era divergência | Refutada | fechado escreve os mesmos valores |
| `panic_on_oops=0` evitaria reboot | Refutada | kernel degradado chegou ao watchdog |
| qualquer AAR/AAW serve para qualquer objeto | Refutada | HARDENED_USERCOPY depende do cache/caminho |
| build/startup prova root | Refutada | exige socket, KernelSU e `su -c id` |
| índice fixo de pipe victim | Refutada | índices 107 e 81 funcionaram em boots distintos |
| falha imediata de `su` prova falha do exploit | Refutada | late-load tem propagação assíncrona |
| repetir no mesmo boot é validação independente | Refutada | payload e fechado têm limite prático de uma vez por boot |

## Estado residual de risco

A classe conhecida de corrupção da lista foi removida do código: o payload não
publica mais links ou contadores de workqueue. O risco residual está nos
offsets específicos do firmware e em falhas ambíguas da primitiva de escrita;
por isso o PTY é validado antes, restaurado byte a byte e mantido aberto quando
não existe prova de restauração. A implementação nova ainda precisa de uma
campanha de boots antes de substituir o último artefato validado.
