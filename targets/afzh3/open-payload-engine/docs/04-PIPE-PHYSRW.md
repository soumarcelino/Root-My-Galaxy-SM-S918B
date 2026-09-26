# Backend físico por `pipe_buffer`

## Motivação

A primitiva ashmem/configfs inicial é suficiente para verificar o landing e
acessar endereços estáticos específicos. Ela não é adequada para copiar
arbitrariamente objetos SLUB dinâmicos: HARDENED_USERCOPY rejeitou a leitura de
`pool_workqueue`. O binário fechado resolve isso instalando uma segunda
primitiva baseada em `pipe_buffer`.

`src/09_pipe_buffer_rw.c` reconstrói esse estágio. Seu objetivo é obter um objeto
`pipe_buffer` localizado numa página order-3 conhecida e, temporariamente,
alterar `page`, `offset` e `len` para transformar operações normais de pipe em
leitura/escrita de endereços direct-map.

## Constantes do alvo

| Constante | Valor | Papel |
|---|---:|---|
| Página | `0x1000` | unidade de mapeamento |
| Ordem do slab | `3` | região de `0x8000` bytes |
| `mm_struct` | `0x400` | 32 objetos por slab |
| skb | `0x8e80` | reclaim da região order-3 |
| Pipes por banco | `240` | drain e reclaim |
| Slots finais | `32` | `F_SETPIPE_SZ` para `0x20000` bytes |
| Objeto pipe | `0x800` | cache esperado |
| `pipe_buffer` | `0x28` | layout validado por `_Static_assert` |
| Tentativas | `12` | misses com cleanup completo |
| Área de prova | `A+0x7100` | scratch dentro do payload reclaim |

Offsets de kernel usados no alvo:

```text
kmalloc_caches       kernel_base + 0x02064478
anon_pipe_buf_ops    kernel_base + 0x01e7f460
vmemmap              0xfffffffe00000000..0xfffffffe40000000
direct map           0xffffff8000000000..0xffffff9000000000
sizeof(struct page)  0x40
```

## Estados globais

O backend conserva:

- bancos `g_drain_pipes` e `g_reclaim_pipes`;
- PID do holder do segundo reclaim;
- base order-3 encontrada;
- endereço do `pipe_buffer` victim;
- índice do pipe victim;
- base KASLR e base do payload;
- flags atômicas request/done/ok;
- estado instalado.

`pthread_once` inicializa todos os FDs com `-1`, evitando que cleanup feche
descritores válidos por confundir zero com “não inicializado”.

## Preparação dos bancos

São criados dois bancos independentes:

1. **drain bank**: ocupa/libera páginas para moldar o allocator;
2. **reclaim bank**: fornece os objetos que permanecerão vivos e conterão o
   victim.

Cada pipe nasce com 2 slots. Só depois do leak/reclaim os bancos são
redimensionados para 32 slots. Essa ordem importa: redimensionar cedo muda o
allocator antes da página alvo ser liberada.

## Geometria `mm_struct`

A preparação replica o padrão order-3:

| Grupo | Quantidade | Estado final |
|---|---:|---|
| prepare | `32*32 = 1024` | mm mortos presos por memfd |
| spray | `(1+5)*32 = 192` | leaders liberados por slab |
| pre | `31` | mm mortos presos por memfd |
| leak | `1` | kernelsnitch collision child |
| post | `32` | mm mortos presos por memfd |

Para pre/post, `make_pinned_memfd()` executa clone, abre `/proc/<pid>/mem`, mata
o processo e conserva o FD. Isso produz o lifetime observado no fechado: o
processo morre, mas o `mm_struct` permanece preso pelo descritor.

## Ordem dos socketpairs

O fechado cria:

1. socketpair de reclaim;
2. socketpair PCP;
3. envia skb de priming no PCP;
4. fixa CPU0;
5. libera as ondas de memfds;
6. fecha PCP;
7. libera o leak fd;
8. envia skb no reclaim.

Inverter reclaim/PCP altera as alocações de socket/skb que antecedem o alvo.
Essa diferença foi pequena no código, mas material no allocator.

## Repin em CPU0

O código chama `pin_cpu0()` depois do send de priming e novamente antes do
redimensionamento. A razão é a mesma do primeiro groom: free e allocate devem
atingir listas per-CPU compatíveis.

## Descoberta da região e cache

Kernelsnitch retorna uma candidata alinhada para a região order-3. Nem toda
página dentro da região precisa conter os objetos pipe desejados. Por isso
`select_pipe_slab()` percorre cada página de 4 KiB:

1. converte direct-map para `struct page` no vmemmap;
2. resolve `compound_head` quando necessário;
3. lê `slab_cache`;
4. compara com kmalloc-2k normal e kmalloc-cgroup-2k;
5. seleciona a página que pertence ao cache esperado.

Ponteiros fora das faixas direct-map/vmemmap abortam a instalação.

## Markers e descoberta do victim

Cada reclaim pipe recebe uma sequência de bytes `0x61` com comprimento
`índice+1`. O campo `len` do `pipe_buffer` passa a identificar o pipe.

O slab é lido em chunks de `0x400`. Cada candidato precisa satisfazer:

- `page` dentro de vmemmap;
- `offset == 0`;
- `1 <= len <= 240`;
- `ops == anon_pipe_buf_ops`;
- `flags == PIPE_BUF_FLAG_CAN_MERGE` (`0x10`);
- `private == 0`.

Depois, um byte `0x6c` é escrito no pipe identificado. O objeto é relido. Só é
aceito se todos os campos permanecerem iguais e `len` aumentar exatamente em
um. Isso confirma que o objeto visto pela primitiva inicial e o pipe userspace
são o mesmo objeto vivo.

## Conversão de endereço

Para um endereço direct-map:

```text
page = VMEMMAP_START + ((addr - DIRECT_MAP_BASE) >> 12) * 0x40
offset = addr & 0xfff
```

Uma operação individual não cruza página. As APIs públicas quebram requests
maiores em chunks até o limite de 4 KiB.

## Leitura

`pipe_rw_read_once()`:

1. salva o `pipe_buffer` real;
2. substitui `page` pelo `struct page` alvo;
3. define `offset` e `len+1`;
4. mantém `anon_pipe_buf_ops`, merge flag e `private=0`;
5. lê do read-end do victim;
6. restaura o objeto original.

## Escrita

`pipe_rw_write_once()`:

1. salva o objeto;
2. aponta `page` e `offset` ao alvo;
3. zera `len`;
4. escreve pelo write-end do victim;
5. restaura o objeto.

Uma falha na restauração torna o resultado não confiável. A função retorna
sucesso apenas se operação e restauração passaram.

## Provas antes de instalar

O backend não é marcado instalado após apenas encontrar um candidato. Ele faz
quatro provas em `A+0x7100`:

1. configfs escreve tag ASCII de 0x17 bytes; pipe lê e compara;
2. pipe escreve outra tag ASCII; configfs lê e compara;
3. configfs escreve `uint64_t`; pipe lê e compara;
4. pipe escreve `uint64_t`; configfs lê e compara.

Isso prova direção, tamanho e endianness com duas larguras.

## Coordenação entre threads

O callback marca `g_prepare_request`. O loop v14 observa e chama
`oss_pipe_rw_service_pending()`. Flags atômicas usam acquire/release para
publicar base, resultado e conclusão.

Harnesses standalone não possuem o loop v14. Para evitar espera infinita,
`oss_pipe_rw_install()` aguarda 5 s e atende a própria solicitação somente se
ela ainda não tiver sido reivindicada. Após 20 s, o holder emite diagnóstico
de fase, mas não é morto nem tem seus bancos fechados enquanto pode estar numa
fase allocator crítica. Misses concluídos continuam sujeitos ao orçamento de
120 s; o supervisor de 180 s é o último limite para holder travado.

## Retries e cleanup

Cada miss executa `oss_pipe_rw_reset()` e registra razão, fase, `errno` e
duração:

- mata holder anterior;
- fecha ambos os bancos;
- zera endereços e índice victim;
- limpa flags atômicas;
- descarta estado instalado.

Nunca se reutiliza metade de uma geometria anterior. Após 12 misses, o backend
falha e `root_umh` não é executado.

Leituras/escritas pelo victim usam `O_NONBLOCK` com deadline de 1 s. Depois de
forjar o `pipe_buffer`, a restauração é tentada mesmo quando a operação expira;
falha de restauração recebe razão própria e nunca é promovida a sucesso.

## Invariantes de manutenção

- Não trocar a ordem reclaim→PCP sem nova evidência do fechado.
- Não substituir pre31/post32 por filhos vivos.
- Não remover o repin CPU0.
- Não aceitar victim só por padrão visual; manter confirmação ativa.
- Não remover provas bidirecionais.
- Não fechar reclaim pipes antes do fim de `root_umh`.
- Não usar o backend para endereços fora do direct-map validado.
- Manter cleanup completo por tentativa.
