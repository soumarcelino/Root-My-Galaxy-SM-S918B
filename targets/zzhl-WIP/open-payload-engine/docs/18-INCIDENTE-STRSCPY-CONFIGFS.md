# Incidente do encoder ConfigFS no ZZHL

Este documento registra a análise das três execuções mais recentes do perfil
Android `dm3q-S918BXXUAZZHL-ksunext`. O root e o KernelSU Next continuam
comprovados, mas o payload atual possui uma falha dependente dos bytes dos
endereços calculados para a primitiva ConfigFS.

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

## Correção necessária

1. Substituir o simulador do teste por um modelo fiel ao `strscpy()` em blocos
   de oito bytes. Antes de cada `pread64()` ou `pwrite64()`, simular a sequência
   de prefixos e comparar os campos de `configfs_buffer` consumidos pelo kernel.
   Um plano divergente deve retornar `EILSEQ` sem executar a syscall.
2. Fazer preflight do readback do scratch assim que `payload_base` for conhecido
   e antes do trigger futex. Se o plano for irrepresentável, encerrar a tentativa
   ainda sem mutação e deixar o supervisor obter outro reclaim.
3. Depois de instalar o pipe R/W, parar de usar ConfigFS AAR para dados do
   kernel. Guardar o descritor original do `pipe_buffer`, validar as escritas do
   pipe pelo próprio pipe e converter endereços da imagem para o alias linear.
   A leitura e a escrita de `selinux_state.enforcing` devem usar esse alias.
4. Manter a validação em runtime para endereços dinâmicos ainda necessários ao
   bootstrap. Depois de uma mutação, uma rejeição deve seguir a recuperação e o
   reboot controlado, nunca tentar a mesma operação com ponteiro divergente.
5. Separar o log de `private PTY staging failed` em `selinux-read`,
   `selinux-value`, `umh-data-write` e `tty-ops-write` para preservar o ponto
   exato de qualquer falha futura.

O item 1 elimina leitura silenciosa do endereço errado e panic. Os itens 2 e 3
são necessários para o exploit continuar funcional em boots cujos endereços
contêm a geometria de NUL observada.

## Validação exigida

- teste unitário com as geometrias exatas das três execuções;
- rejeição antes de syscall para os planos das execuções 2 e 3;
- build completo do payload e do app;
- execução em boot limpo confirmando AAR/AAW, pipe, restauração de FOPS, UMH,
  KernelSU, SELinux enforcing e ausência de novo pstore;
- campanha em múltiplos boots para cobrir slides e `payload_base` diferentes.
