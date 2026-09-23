# Causas raiz, correções e hipóteses descartadas

## Resumo

O clone não falhava por um único offset. Quatro diferenças independentes
impediam a cadeia completa ou causavam panic depois que o estágio anterior
passava.

| Bloqueio | Sintoma | Evidência decisiva | Correção |
|---|---|---|---|
| Afinidade CPU0 perdida | reclaim não caía ou fake FOPS ficava inconsistente | comportamento de `kernelsnitch_bruteforce` e comparação com `FUN_00104bf0` | repin imediato e filhos em CPU0 |
| Backend errado para SLUB dinâmico | panic HARDENED_USERCOPY em `pool_workqueue` | last_kmsg com `configfs_read_iter` lendo `pwq+0` | pipe physical R/W para campos dinâmicos |
| Geometria/ordem do pipe divergente | victim instável ou backend não instalava | comparação de decompile/disassembly e allocator real | 2×240 pipes, 32 slots, ordem reclaim→PCP, pre31/post32 |
| Janela de publicação da workqueue | `list_del corruption`/backlink fake | panic do fluxo fechado e lista compartilhada mudando | revalidação imediata, wake sem logs, sem rollback cego |

## 1. Afinidade CPU0 perdida no primeiro groom

### Sintoma

O trigger podia executar sem que `ashmem_misc.fops` apontasse para o objeto
esperado. Em outras execuções, um objeto parcialmente correto chegava ao
callback, mas campos residuais causavam falha ao abrir ashmem.

### Causa

O fluxo começa fixado em CPU0, mas `kernelsnitch_bruteforce()` altera/limpa a
afinidade do chamador. O clone assumia que a afinidade inicial ainda valia.
Assim, a liberação das páginas `mm_struct` e a alocação do skb de reclaim
podiam acontecer em PCPs diferentes.

Como as page lists são per-CPU, mesma geometria e mesmos tamanhos não bastam se
free e allocate acontecem em CPUs distintas.

### Correção

- `clone_child()` fixa cada filho em CPU0.
- após o envio PCP de priming, `pin_reclaim_to_cpu0()` é chamado novamente;
- a checagem é fatal para aquela tentativa se a afinidade não puder ser
  aplicada.

### Prova

Nos dois boots finais:

```text
[aar_aaw] ashmem_misc_fops readback got=<A+0x1180> want=<A+0x1180>
[aar_aaw] verify ok: corruption landed, R/W primitive live
```

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
| slot estático `system_unbound_wq` | configfs AAR, igual ao fechado |
| `wq+0xb0`, `pwq`, pool | pipe R/W |
| contadores e worklist | pipe R/W |
| fake work e dados UMH | pipe R/W |
| completion | pipe R/W |

Essa separação removeu o panic HARDENED_USERCOPY.

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
- filhos pre/post convertidos em mm mortos presos por `/proc/<pid>/mem`;
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

## 4. Race na publicação da workqueue

### Sintoma

Um panic do fluxo fechado mostrou `list_del corruption`; backlink apontava para
a entrada fake `A+0x6008`. Isso demonstrou que até o fechado pode atingir uma
janela TOCTOU na lista compartilhada.

### Causa

O pool pode mudar entre:

1. leitura de lista vazia/worker idle;
2. preparação dos blobs;
3. alteração de contadores;
4. ligação de `worklist.next/prev`;
5. wake do worker.

Não há aquisição do lock interno do pool pela primitiva usada. Portanto, a
implementação só pode minimizar e detectar a janela, não eliminá-la de forma
absoluta.

### Correção

- aguardar lista vazia e `nr_idle>0`;
- validar `color`, `refcnt`, `nr_active<max_active`;
- escrever blobs antes de mutar a lista;
- revalidar lista e idle imediatamente antes dos contadores;
- publicar contadores e links em sequência curta;
- não fazer logging entre publicação da lista e primeiro wake;
- não tentar rollback cego depois da primeira escrita de lista, pois outro
  worker pode já ter observado/mudado o estado;
- confirmar completion e socket em vez de inferir sucesso do wake.

## Correções menores relevantes

### Restauração de `ashmem_misc.fops`

O ponteiro global agora volta para a tabela estática real logo após a prova
AAR/AAW. O descritor aberto continua usando a fake FOPS. Isso reduz dangling
global e evita que novas aberturas usem memória de reclaim.

### Limpeza do owner fake

Depois de `root_umh`, o campo owner em `A+0x1180` é zerado pelo mesmo fd. O
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

A janela da workqueue foi reduzida e passou em dois boots, mas continua
dependente de estado concorrente do pool. Qualquer nova falha após publicação
da lista deve ser tratada como potencial panic/TOCTOU e investigada pelo
last_kmsg antes de alterar offsets ou repetir cegamente.
