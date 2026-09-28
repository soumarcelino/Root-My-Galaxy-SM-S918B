# AAR ConfigFS com comprimento exato

> **Escopo histórico:** este documento veio do engine AFZH3 usado como base.
> A validação AAR/AAW do ZZHL está no relatório 17.

## Falha observada

O boot falhou em uptime 104,443957 s com:

```text
usercopy: Kernel memory exposure attempt detected from SLUB object
'kmalloc-128' (offset 8, size 221)!
```

A pilha foi `usercopy_abort -> __check_heap_object -> __check_object_size ->
configfs_read_iter -> vfs_read -> __arm64_sys_pread64`. O registro `x19` era
`0xdd`, confirmando que o kernel validou 221 bytes. A evidência original está
em
`evidence/app-runs/20260927T064207Z-34445c30/forensics/last_kmsg.txt`.

## Causa raiz

O atributo forjado devolve o count fixo `0x6d6873612f766564`, derivado de
`"dev/ashm"`. No kernel AFZH3, `configfs_read_iter()` chama:

```c
copy_to_iter(buffer->page + iocb->ki_pos,
             buffer->count - iocb->ki_pos, to);
```

O Hardened Usercopy valida o comprimento fornecido a `copy_to_iter()` antes de
o iterador limitar a quantidade efetivamente copiada para userspace.

O plano antigo evitava bytes zero no ponteiro de `buffer->page`. Para o alvo
`ffffff8923636588` e comprimento oito, ele avançou o ponteiro em 213 bytes e
recuou `ki_pos` em 213. O endereço calculado continuou correto, mas o
comprimento validado passou a ser:

```text
8 + 213 = 221 = 0xdd
```

O começo da origem estava no offset oito de um objeto `kmalloc-128`; validar
221 bytes necessariamente atravessava o limite do objeto e acionava o BUG.

## Correção

`oss_kernel_read()` agora constrói somente este plano:

```text
offset = OSS_CONFIGFS_COUNT - len
page = target - offset
page + offset = target
OSS_CONFIGFS_COUNT - offset = len
```

O ponteiro não é deslocado. `set_ashmem_name_blob()` publica os bytes zero com
escritas sucessivas de prefixos, do fim para o início. Antes da syscall, o
código rejeita tamanho zero, overflow do alvo, tamanho acima de `SSIZE_MAX`,
offset que não cabe em `off_t`, endereço reconstruído divergente e comprimento
do kernel diferente do solicitado.

Existe uma única chamada `pread64()` no source do engine e ela fica depois
dessas provas. Portanto, este padrão de extensão de comprimento não possui um
caminho alternativo dentro do payload.

## Provas

`tests/test_aar_read_plan.c`:

- reproduz deslocamento 213 e comprimento antigo 221;
- exige comprimento novo exatamente oito;
- testa endereços históricos e o endereço do crash;
- modela o encoder de NULs e compara o blob reconstruído byte a byte;
- rejeita tamanho zero e overflow.

A campanha
`evidence/reliability/20260927T-usercopy-exact-soak3-v2/` usou o payload
`62e6563f43945ac790b4678cb3e5398067692057fd270960f3a7b4d511a763d5`.
Os três boots distintos chegaram a:

```text
[aar_aaw] verify ok: corruption landed, R/W primitive live
[immediate] restore ashmem_misc.fops ... confirmed=1
[pipe_rw] ready ... det=1
```

Não existe ocorrência de `usercopy`, `configfs_read_iter`, `Kernel panic` ou
reboot espontâneo nas evidências novas. O boot
`7a72939d-1212-4134-bba4-9c36487b7e38` concluiu também root temporário,
KernelSU e `uid=0(root)`. Nos dois primeiros boots, a falha posterior foi
`private PTY staging failed`.

`tools/verify-usercopy-campaign.py` exige boots distintos, o mesmo hash de
payload, AAR verificada, restauração confirmada da FOPS e pipe pronto em cada
boot. Ele também rejeita qualquer evidência com os marcadores do panic. O
relatório `usercopy-proof.json` desta campanha retornou
`pass_usercopy_path_all_boots: true` para 3/3 boots.

## Escopo da garantia

Para este panic específico, a prevenção é estrutural: a syscall não é feita
se o comprimento que `configfs_read_iter()` validará diferir do pedido. A
campanha confirma a integração no aparelho. Isso não constitui garantia de
ausência de qualquer outro panic em estágios independentes do exploit.
