# Arquitetura e fluxo completo

> **Escopo histórico:** este documento veio do engine AFZH3 usado como base.
> Valores e resultados ZZHL estão no relatório `17-PORT-ZZHL-FRESH-E-CAMPANHA.md`.

## Visão geral

O shared object roda pelo constructor quando `LD_PRELOAD` carrega
`/system/bin/true`. Um supervisor cria tentativas limitadas. Dentro da tentativa
bem-sucedida, o processo prepara KASLR e allocator, dispara o trigger futex,
executa o callback de verificação no thread correto, instala o backend físico,
publica o usermode helper e preserva as alocações necessárias num holder.

```mermaid
flowchart TD
    A[LD_PRELOAD constructor] --> B[Supervisor de tentativas]
    B --> C[Preflight ashmem e limites]
    C --> D[KASLR via tracefs]
    D --> E[Groom mm_struct order-3]
    E --> F[skb 0x8e80 com fake FOPS]
    F --> G[Trigger futex/FPSIMD v14]
    G --> H[Callback imediato no waiter]
    H --> I[Prova AAR/AAW]
    I --> J[Restaura ashmem_misc.fops]
    J --> K[Instala pipe physical R/W]
    K --> L[Constrói subprocess_info]
    L --> M[PTY privado chama schedule_work]
    M --> N[Completion + temp_su.sock]
    N --> O[temporary-root-ready]
    O --> P[Holder preserva alocações]
    P --> Q[ksu-helper --late-load]
    Q --> R[su -c id == uid 0]
```

## 1. Entrada e supervisor

`src/00_orchestrator.c` possui dois modos de build:

- executável PIE `build/app_main`, útil para diagnóstico;
- shared object `build/payload.so`, carregado por `LD_PRELOAD`.

No início, o processo:

- eleva os soft limits de `RLIMIT_NOFILE` e `RLIMIT_NPROC` ao hard limit;
- seleciona e fixa a CPU mais rápida permitida pelo cpuset;
- valida `CVE43499_ROOT_HELPER`;
- prepara estado compartilhado de 0x20 bytes;
- limita quantidade e duração das tentativas.

O estado compartilhado contém status terminal e campos do handoff P0. Uma
tentativa que já realizou mutação potencialmente terminal não deve ser tratada
como uma tentativa comum e repetida cegamente.

## 2. Preflight da primitiva ashmem

Antes de tocar o reclaim, `oss_prepare_kernel_rw_path()` procura um nó ashmem
que o contexto shell consiga abrir. No alvo, `/dev/ashmem` canônico é negado,
mas o alias associado ao boot pode ser aberto.

Esse preflight existe para evitar disparar a corrupção e descobrir depois que
o descritor necessário para verificar/restaurar a estrutura global não pode ser
obtido.

## 3. Localização KASLR

`src/01_kernel_base_tracefs.c` usa tracefs e `sched_blocked_reason` para derivar a base. O fluxo
preferido continua sendo tracefs mesmo quando `SLIDE_P0_OFFSET` existe; o offset
forçado é fallback validado por faixa e alinhamento de 64 KiB.

Marcas esperadas:

```text
stage=locating-kernel
[kaslr] source=tracefs base=... slide=... p0_offset=...
stage=kernel-location-ready
```

Sem base confiável, nenhum offset do alvo é seguro.

## 4. Groom e fake FOPS

`src/05_mm_slab_grooming.c` prepara slabs order-3 de `mm_struct`, usa kernelsnitch para obter
uma candidata e calcula:

```text
A = candidate & ~0x7fff
D = A - 0xe80
```

O skb enviado tem `0x8e80` bytes. Seus dados começam em `D`. Logo, o conteúdo
no offset `+0x20e0` do buffer do usuário cai em `A+0x1260`, onde a tabela fake
FOPS primária é construída. A tabela de recuperação em `+0x24e0` cai em
`A+0x1660`. Confundir `D` com `A` desloca todos os ponteiros internos.

O reclaim depende de uma única CPU porque as listas de páginas são per-CPU.
O código seleciona dinamicamente a CPU mais rápida e estável permitida. A
política considera capacidade, frequência e `core_ctl`, e usa a mesma CPU na
fábrica exec, criação do alvo, liberações, drains e envio de reclaim.

As gerações pre31/post32 usam o mesmo PID fábrica. O código fecha 62 vizinhos,
usa 38 slabs auxiliares para expulsar o alvo de `cpu_partial` ainda com o leak
vivo e libera essa referência conhecida por último. O primeiro `sendmsg()` vem
imediatamente depois. O fluxo aborta antes do futex se os contadores não
provarem a devolução de um slab e 32 objetos.

## 5. Trigger futex/FPSIMD

`src/07_futex_pi_trigger.c` implementa waiter, owner e consumer. A variante padrão
v14 preserva o handshake relevante:

```text
-1 -> gate -> SIGUSR1 -> 1 -> sched_setattr
```

O callback que verifica a corrupção roda no waiter, imediatamente após o
`sched_setattr` bem-sucedido. Mover essa verificação para o thread principal,
após joins ou logs, altera a janela observada pelo kernel.

Enquanto o waiter executa o callback e aguarda a preparação física, o loop
principal v14 chama `oss_pipe_rw_service_pending()` a cada 10 ms. Isso permite
que o grooming do segundo backend aconteça no contexto/lifetime correto sem
bloquear indefinidamente o callback.

## 6. Callback imediato e restauração global

`do_one_attempt_post_trigger()`:

1. abre o nó ashmem já resolvido;
2. lê `ashmem_misc.fops` e comprova o landing;
3. conserva o descritor aberto com seu `f_op` já capturado;
4. restaura o ponteiro global para a tabela estática real;
5. instala o backend pipe;
6. chama `root_umh_install_fd()` usando o mesmo descritor;
7. limpa o campo owner da fake FOPS;
8. emite `stage=temporary-root-ready` somente se tudo passou.

A restauração precoce remove o dangling pointer global. O descritor aberto
continua útil porque o VFS já associou seu `f_op` na abertura.

Se a abertura, a prova ou a primeira AAW falha, o callback ainda está dentro
do waiter v14. Ele entrega um segundo frame e dispara outro `sched_setattr`.
Esse write aponta o global para a FOPS de recuperação em `A+0x1660`; o único
efeito lateral cai em `llseek`, que esse caminho não usa. O novo descritor faz
a AAW do endereço real. Se isso não for possível, um terceiro frame vermelho,
sem filhos, escreve zero no global para impedir `misc_open` de chamar
`try_module_get` com um owner residual.

## 7. Segundo reclaim: backend físico por pipes

O backend em `src/09_pipe_buffer_rw.c` cria dois bancos de 240 pipes. Após a segunda
geometria de `mm_struct`, um slab order-3 é reclamado por objetos de pipe. O
código identifica um `pipe_buffer`, confirma o victim por alteração controlada
do comprimento e prova leitura e escrita numa região scratch de `payload_base`.

Esse backend fornece a leitura/escrita física usada nas estruturas dinâmicas e
na resolução do PTY. Ele também evita usar configfs em objetos SLUB, caminho
que anteriormente acionava HARDENED_USERCOPY.

## 8. Publicação do usermode helper

`src/10_workqueue_umh_root.c` abre um PTY privado, resolve seu `tty_struct` pela
tabela de descritores e valida `magic`, ops, port e o `SAK_work` original. Uma
cópia de `tty_operations` troca apenas `flush_buffer` por `do_SAK`.

`TCFLSH/TCOFLUSH` chama `do_SAK(tty)`, que executa
`schedule_work(&tty->SAK_work)`. O kernel segura o lock do pool e atualiza lista
e contadores. Depois da completion, os 112 bytes usados como
`subprocess_info` são restaurados e conferidos antes de fechar o PTY.

## 9. Completion, socket e holder

Root temporário só é aceito quando:

- o campo completion fica não zero;
- o socket `/data/local/tmp/temp_su.sock` aceita conexão.

Após sucesso, `spawn_allocation_keeper()` cria `cve43499-hold`, desacopla o
processo e conserva descritores/alocações herdados. O helper e o payload
principal podem encerrar sem liberar imediatamente memória ainda referenciada.

## 10. Late-load KernelSU

O runner envia helper, payload e ksud. Após `temporary-root-ready`, executa:

```text
ksu-helper --late-load
```

O controle KernelSU é validado antes da prova final. Como `/system/bin/su` pode
aparecer alguns segundos depois, o runner consulta `su -c id` por até 30 s. O
único sucesso final é uma saída contendo `uid=0(root)`.

## Lifetimes importantes

| Recurso | Deve sobreviver até |
|---|---|
| memfds do primeiro groom | Landing, callback e restauração segura. |
| fd ashmem aberto | Fim de `root_umh` e limpeza do owner. |
| reclaim sockets fake FOPS | Holder assumir lifetime. |
| bancos de reclaim pipe | Fim dos acessos dinâmicos/completion. |
| holder do pipe backend | Enquanto a primitiva física estiver em uso. |
| blob fake work/UMH | Workqueue consumir e completar. |
| `cve43499-hold` | Estabilidade do root temporário/late-load. |

Fechar um recurso “não usado pelo userspace” pode liberar exatamente o objeto
que o kernel ainda está prestes a dereferenciar.
