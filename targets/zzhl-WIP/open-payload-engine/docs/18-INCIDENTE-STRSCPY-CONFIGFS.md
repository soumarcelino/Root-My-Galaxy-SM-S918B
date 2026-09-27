# Incidente do encoder ConfigFS no ZZHL

Este documento registra a análise das três execuções que revelaram a falha e a
correção validada no perfil Android `dm3q-S918BXXUAZZHL-ksunext`.

## Resultado das execuções

| Execução | Resultado | Ponto decisivo |
|---|---|---|
| `78fff39b-fbe1-482b-857b-abbc0e180823` | sucesso | AAR/AAW, pipe R/W, UMH e KernelSU verificados |
| `d117bb7b-221d-4025-b947-449eda33424d` | falha controlada | leitura do estado SELinux apontou para o endereço errado |
| `c47ac3c8-9c3c-47d0-9cc8-efa7f624c438` | panic | leitura de verificação do scratch usou ponteiro não canônico |

A execução bem-sucedida não contradiz as duas falhas: seus ponteiros
calculados não continham NUL nas posições problemáticas.

## Causa raiz

`set_ashmem_name_blob()` tenta publicar NULs em `ashmem_area.name` com uma
escrita longa seguida por escritas de prefixos. O teste host modela essas
escritas byte a byte. O `strscpy()` deste kernel ZZHL usa blocos de oito bytes;
quando encontra NUL, mascara e grava o bloco inteiro, zerando os bytes restantes
do mesmo bloco. Um NUL seguido por byte não zero no bloco é, portanto,
irrepresentável por esse encoder.

Na segunda execução, a leitura de um byte em `selinux_state.enforcing`
pretendia usar:

```text
target = ffffffc00ae96600
page   = 92978c5edb73009d
bytes  = 9d 00 73 db 5e 8c 97 92
```

O NUL zerou também o byte `0x73`. O ponteiro efetivo passou a ser
`ffffffc00a766600`. Removido o slide do boot, o byte correspondente no ELF é
`0x6d`; por isso `original_selinux > 1` encerrou o staging privado da PTY antes
de publicar ou enfileirar o workqueue.

Na terceira execução, a leitura dos 35 bytes de verificação do scratch
pretendia usar:

```text
target = ffffff8a30372180
page   = 92978c2900c0bc3f
bytes  = 3f bc c0 00 29 8c 97 92
```

O `strscpy()` transformou `page` em `0000000000c0bc3f`. Somado ao offset da
leitura, o endereço efetivo foi `6d68736130372180`, exatamente o valor de `x1`
no panic. A pilha termina em `__arch_copy_to_user -> _copy_to_iter ->
configfs_read_iter`; `x2=0x23` confirma a leitura de 35 bytes da magic string.

O reclaim, a descoberta KASLR e o pipe não causaram esse panic.

## Correção implementada

1. O simulador agora reproduz a escrita do `strscpy()` em blocos de oito bytes.
   Cada controle AAR/AAW é simulado antes dos `ioctl`, `pread64` ou `pwrite64`;
   divergência em campos consumidos por `configfs_buffer` retorna `EILSEQ`.
2. O payload valida o readback do scratch, a leitura de FOPS e os dois valores
   usados no alias linear depois do reclaim e antes do trigger futex. Uma
   geometria rejeitada termina com estado pré-mutação, permitindo nova tentativa.
3. O backend de pipe guarda o descritor original validado do `pipe_buffer`.
   Depois da instalação, não existe mais ConfigFS AAR: provas, `init_task`,
   `tty_operations` e SELinux são lidos pelo pipe. Endereços da imagem usam o
   alias calculado com `memstart_addr` e `kimage_voffset` do próprio boot.
4. A escrita e o readback de `selinux_state.enforcing`, a limpeza do owner da
   FOPS falsa e sua restauração usam pipe R/W. O ConfigFS AAW remanescente fica
   restrito a forjar e restaurar o descritor do pipe já validado.
5. O staging da PTY registra separadamente `selinux-alias`, `selinux-read`,
   `selinux-value`, `umh-data-write` e `tty-ops-write`.

## Validação

`tests/test_aar_read_plan.c` reproduz as geometrias exatas. Ele aceita a
execução bem-sucedida, rejeita com `EILSEQ` a leitura SELinux que perdeu `0x73`
e a leitura do scratch que originou o panic, e confirma os planos AAW usados.

O APK corrigido foi executado em boot limpo
`6a6be15d-44c6-4b14-b722-dd94997649b7`. A execução do app
`9075374d-a736-4da8-b0cd-17ab4bc3b739` concluiu na primeira tentativa:

```text
KASLR base:       ffffffc008190000
payload_base:     ffffff8913ab8000
pipe victim:      ffffff8a21099000
pipe setup:       12 ms
UMH:              complete=1 socket=1 restore=1
root:             35.169 s
KernelSU control: version=33295 flags=0x5 uapi=4 features=0x2714
SELinux final:    Enforcing
pstore final:     vazio
```

O payload incorporado tem SHA-256
`9c412f5af77611f61165f82f02e0576e410b8a1fb556748ff6ddf3cb44364997`.
O APK tem SHA-256
`2f326cf1ac99e092f849de5934a41d1882c0a1fd3f274b373ba1e1f0103a5ce6`.
