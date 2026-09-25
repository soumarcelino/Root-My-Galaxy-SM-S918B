# `root_umh`: usermode helper pela workqueue

## Objetivo

O estágio constrói um `subprocess_info` e dados auxiliares em memória controlada,
insere o `work_struct` em `system_unbound_wq` e aguarda o kernel executar
`call_usermodehelper_exec_work`. O helper sobe o socket temporário que permite o
late-load do KernelSU.

## Endereços e layouts

Offsets do alvo:

```text
selinux_enforcing                 K + 0x02d8e5c0
system_unbound_wq slot            K + 0x02a90800
call_usermodehelper_exec_work     K + 0x001045d0
fake work                         A + 0x6000
UMH data                          A + 0x6200
```

Campos dinâmicos usados:

```text
wq->dfl_pwq             +0xb0
pwq->pool               +0x00
pwq->wq                 +0x08
pwq->work_color         +0x10
pwq->refcnt             +0x18
pwq->nr_in_flight       +0x1c + color*4
pwq->nr_active          +0x5c
pwq->max_active         +0x60
pool->worklist          +0x20
pool->nr_idle           +0x34
work->data              +0x00
work->entry             +0x08
work->func              +0x18
```

`_Static_assert` verifica `subprocess_info` com 112 bytes e completion com 32
bytes. Uma mudança de compilador que alterasse padding falharia no build.

## Separação de backends

Configfs permanece somente nos acessos que o fechado faz por esse caminho:

- byte `selinux_enforcing`;
- slot estático que contém o ponteiro `system_unbound_wq`.

Pipe R/W é obrigatório para:

- `wq->dfl_pwq`;
- `pwq->pool` e `pwq->wq`;
- estado, contadores e worklist;
- blobs fake work/UMH;
- completion.

Essa distinção é estrutural, não otimização.

## Montagem dos dados UMH

`umh_kernel_data` contém:

- completion inicializada com wait-list auto-referente;
- path absoluto do helper;
- argumento `--umh`;
- UID do processo chamador em texto;
- `argv = {path, arg, uid, NULL}`;
- `envp = {NULL}`.

O path é rejeitado se não couber no buffer de 256 bytes. Todos os ponteiros do
blob são endereços kernel dentro de `A+0x6200`.

O `subprocess_info` fake contém:

- `work.data = pwq | (color << 4) | 5`;
- `work.entry` inicialmente auto-referente à worklist;
- `work.func = call_usermodehelper_exec_work`;
- ponteiros para completion, path, argv e envp.

## Validação de ponteiros

Antes de dereferenciar:

- `wq`, `pwq` e `pool` devem estar no direct-map;
- `pwq->wq` deve ser exatamente o `wq` lido do slot estático.

Isso impede que uma leitura transitória ou offset incorreto seja usado para
escrever em endereço arbitrário.

## Espera por pool utilizável

Até 200 iterações de 1 ms verificam:

```text
worklist.next == &worklist
worklist.prev == &worklist
pool->nr_idle > 0
```

Se a lista continuar ocupada, a tentativa falha antes de qualquer publicação.

## Estado do `pool_workqueue`

O código exige:

- `color < 16`;
- `refcnt != 0`;
- `nr_active < max_active`;
- leitura válida de `nr_in_flight[color]`.

Esses valores determinam `work.data` e os contadores que precisam representar
o item recém-publicado.

## Escrita dos blobs e revalidação

Primeiro são escritos dados UMH e fake work. Como essa preparação pode levar
tempo suficiente para outro producer/worker mudar o pool, a lista e `nr_idle`
são relidos imediatamente antes da mutação dos contadores.

Se o estado mudou, a função retorna sem publicar o item.

## Ordem de publicação

Ordem final:

1. `nr_in_flight[color]++`;
2. `nr_active++`;
3. `refcnt++`;
4. `worklist.prev = fake_entry`;
5. `worklist.next = fake_entry`;
6. wake imediato.

As três primeiras escritas ainda permitem rollback limitado se uma delas
falhar. Depois da primeira escrita da lista, a operação é tratada como
irreversível.

### Por que não há rollback da lista

Uma função pipe pode ter efetuado a escrita alvo e falhado apenas ao restaurar
o `pipe_buffer`. Além disso, um worker concorrente pode observar e remover o
item entre a falha e o rollback. Escrever “estado anterior” cegamente poderia
corromper uma lista que já mudou.

## Wake da workqueue

`wake_system_unbound()` abre um PTY master, concede/desbloqueia, abre o slave e
fecha ambos. Esse trabalho provoca atividade no `system_unbound_wq` sem depender
de outro offset específico do kernel.

Não há log entre ligação final da lista e o primeiro wake. Isso reduz a janela
em que o item fica publicado sem worker acordado.

## Completion

Após o primeiro wake:

- até 8 ciclos;
- cada ciclo faz até 250 leituras de 1 ms;
- ciclos posteriores podem repetir o wake.

O campo completion é lido pelo pipe backend. Falha de leitura encerra a
tentativa: não se assume que o helper executou.

## Prova por socket

Completion prova que o work item terminou, mas não que o helper estabeleceu o
canal de root esperado. Depois de completion, o código tenta conectar a
`/data/local/tmp/temp_su.sock` até 200 vezes com intervalo de 10 ms.

Sucesso de `root_umh_install_fd()` exige socket conectado.

Log final esperado:

```text
[root_umh] queued ... writes=1/1/1/1/1
[root_umh] result wake=1 complete=1 socket=1
```

## Falhas e interpretação

| Log | Significado |
|---|---|
| `bad workqueue wq` | slot/offset/base incorreto ou ponteiro transitório |
| `bad workqueue pwq/pool` | backend pipe ou layout dinâmico inválido |
| `pwq_wq` diferente | cadeia de ponteiros inconsistente |
| `pool busy` | estado concorrente; não publicar |
| `bad pwq state` | color/refcount/capacidade incompatível |
| `pool changed before publish` | TOCTOU detectado a tempo |
| `worklist ... write failed` | estado potencialmente irreversível; não repetir no boot |
| `complete=0` | worker não consumiu ou fake work inválido |
| `socket=0` | helper terminou sem canal de root utilizável |

## Relação com KernelSU

`root_umh` cria o root temporário necessário ao helper. KernelSU é carregado
depois, fora deste módulo, pelo `simple-root`. Portanto:

- `root_umh socket=1` é prova do estágio temporário;
- `KernelSU control verified` prova o late-load;
- `su -c id` prova a interface final usada pelo operador.

As três provas devem permanecer separadas nos logs.
