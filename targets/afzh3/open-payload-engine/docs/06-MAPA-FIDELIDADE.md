# Mapa de fidelidade ao binário fechado

## Finalidade

Este mapa liga comportamento observado no payload fechado à implementação
aberta. Ele ajuda revisão, regressão e investigação de novos firmwares.

## Mapa por estágio

| Fechado/origem | Implementação aberta | Fidelidade relevante | Estado |
|---|---|---|---|
| `_INIT_2` / `0x10a440` | constructor em `00_orchestrator.c` | execução automática por `LD_PRELOAD` | implementado |
| `FUN_001044f4` | `do_one_attempt()` | limites, CPU0, ordem da tentativa, holder | implementado |
| `FUN_0010757c` | `kaslr_locate_via_tracefs()` | tracefs preferido e fallback P0 | implementado |
| `FUN_00106288` | `05_mm_slab_grooming.c` | mm order-3, pre/post, skb `0x8e80` | validado no device |
| `FUN_00104bf0` | `pin_reclaim_to_cpu0()` | repin após priming | implementado/necessário |
| fake object fechado | `04_fake_kernel_objects.c` | FOPS em `A+0x1180` | validado por readback |
| waiter/owner/consumer | `07_futex_pi_trigger.c` v14 | handshake e callback no waiter | validado |
| `FUN_001076c0` | callback em `00_orchestrator.c` | verify, restore, root stage, owner clear | validado |
| `FUN_00107dd4` | `09_pipe_buffer_rw.c` prepare | segundo reclaim e bancos 240+240 | validado |
| `FUN_00108604` | pipe R/W interno | forja temporária do `pipe_buffer` | validado por provas |
| `FUN_00108fa4` | `10_workqueue_umh_root.c` | fake work, contadores, list publish | validado |
| helper fechado | `ksu-helper`/`--umh` | socket temporário e late-load | validado |

## Constantes com impacto direto

| Valor | Origem/uso | Consequência se divergir |
|---:|---|---|
| `0x8000` | região order-3 | base e objetos internos errados |
| `0x400` | `mm_struct` alvo | contagem por slab errada |
| `0x8e80` | skb send | dados não cobrem layout/reclaim esperado |
| `0xe80` | diferença `A-D` | fake FOPS deslocada |
| `0x1180` | tabela fake FOPS | landing não verificável |
| `31/32` | ondas pre/post | target deixa de ocupar posição esperada |
| `240/240` | bancos pipe | pressão/cache e identificação divergentes |
| `32` | slots pipe | objetos/páginas insuficientes |
| `0x7100` | scratch proof | prova pisa em outro layout se movida sem revisão |
| `0x6000/0x6200` | work/data UMH | sobreposição/campos inválidos |

## Comportamentos deliberadamente preservados

- O payload fechado só é usado como referência; o clone não chama o fechado.
- A execução normal continua por constructor/`LD_PRELOAD`.
- A primitiva configfs é preservada onde o fechado a usa.
- A primitiva pipe é instalada antes de tocar SLUB dinâmico.
- O descritor ashmem é reutilizado durante o callback.
- O callback ocorre dentro do waiter.
- O holder conserva alocações após o processo principal.
- Tentativas e timeouts são limitados.

## Desvios conscientes

### Variante v14

O v14 mantém o handshake crítico, mas usa reentrada periódica no kernel no
loop de espera para não manter uma janela de `pi_blocked_on` instável por
busy-spin contínuo. A mudança foi adotada após panic reproduzido e forense em
`rt_mutex_adjust_prio_chain`.

### Fallback para harness standalone

Produção atende o pedido de pipe pelo loop v14. Harnesses que chamam
`root_umh_install()` isoladamente não têm esse loop. Após 5 s, o installer
pode atender a solicitação ainda não reivindicada. Esse fallback não altera o
caminho normal quando o serviço de produção responde.

### Poll de `su`

O runner aguarda até 30 s após late-load. Isso não muda exploração nem kernel;
apenas mede corretamente um resultado assíncrono.

## Diagnósticos que não fazem parte do caminho produtivo

- `tests/support/pipe_spray.c` permanece disponível para o harness focado;
- variantes futex supersedidas permanecem documentadas no histórico Git;
- `BISECT_VARIANT` é switch de investigação;
- `SLIDE_ONLY`/`P0_ONLY` param após KASLR por desenho;
- arquivos `tests/test_*.c` não são ligados no payload produtivo pelo Makefile.

Não adicionar um diagnóstico ao caminho produtivo só porque ele melhora um
teste isolado. Primeiro comparar ordem e lifetime com o fechado.

## Checklist de revisão de fidelidade

Ao revisar uma alteração, responder:

1. Qual função/instrução fechada sustenta a mudança?
2. A ordem de syscalls mudou?
3. Alguma alocação libc/log foi adicionada numa janela crítica?
4. CPU affinity continua válida após chamadas que podem alterá-la?
5. Quantidades pre/post, pipes, slots e sizes permanecem iguais?
6. Algum descritor/processo passou a morrer cedo?
7. O backend de memória usado é o mesmo para a classe de objeto?
8. Uma falha depois de mutação irreversível pode causar retry indevido?
9. O critério de sucesso continua exigindo prova real?
10. A alteração passou em dois reboots limpos?

## Port para outro firmware

Não basta trocar a string do build. É necessário revalidar:

- base/offsets de símbolos;
- tamanho e offsets de estruturas BTF;
- endereço de caches e `anon_pipe_buf_ops`;
- layout fake FOPS;
- offsets de workqueue/pwq/pool;
- helper/KernelSU compatível;
- alias ashmem acessível;
- identidade do kernel em execução;
- duas execuções limpas no novo alvo.
