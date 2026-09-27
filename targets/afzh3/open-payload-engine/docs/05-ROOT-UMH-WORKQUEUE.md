# `root_umh`: usermode helper pela workqueue nativa

## Objetivo

O estágio executa `call_usermodehelper_exec_work` usando a API normal da
workqueue. Um PTY privado fornece um `tty_struct` descartável; `do_SAK()` chama
`schedule_work(&tty->SAK_work)` e o próprio kernel segura o lock do pool,
insere a lista, atualiza os contadores e acorda o worker.

O payload não escreve mais `system_unbound_wq`, `pool->worklist`, `nr_active`,
`nr_in_flight` ou `pwq->refcnt`. Isso elimina a corrida que produziu
`__list_del_entry_valid` e `cancel_work_sync` no panic observado.

## Layout AFZH3

```text
selinux_enforcing                 K + 0x02d8e5c0
call_usermodehelper_exec_work     K + 0x001045d0
do_SAK_work                       K + 0x00bb5f14
do_SAK                            K + 0x00bb8728
tty_struct.ops                    +0x018
tty_struct.SAK_work               +0x2f8
tty_operations.flush_buffer       +0x0a8
UMH data                          A + 0x6200
fake tty_operations              A + 0x6400
```

Os tamanhos e offsets foram conferidos no BTF AFZH3:

- `tty_struct`: 832 bytes em objeto `kmalloc-1k`;
- `SAK_work`: 48 bytes;
- `subprocess_info`: 112 bytes;
- `tty_operations`: 280 bytes;
- `completion`: 32 bytes.

O `subprocess_info` começa em `SAK_work` e termina no padding do mesmo objeto
`kmalloc-1k`. O código salva e restaura os 112 bytes completos.

## Resolução do PTY

Depois de abrir master e slave privados, o payload encontra o `task_struct` do
PID atual a partir de `init_task.tasks`, resolve `files_struct`, `fdtable`, o
`struct file` do slave, `tty_file_private` e finalmente `tty_struct`.

Antes de qualquer escrita exige:

- todos os objetos dinâmicos no direct map;
- `tty.magic == 0x5401`;
- `tty_file_private.file` igual ao file resolvido;
- índice do PTY dentro do limite;
- `tty->ops` dentro da imagem do kernel;
- `tty->port` válido;
- `SAK_work` não pendente;
- lista do work auto-referente;
- função original exatamente `do_SAK_work`.

Qualquer divergência aborta antes de tocar o PTY.

## Publicação

1. Copiar a tabela original `tty_operations` para `A+0x6400`.
2. Trocar apenas `flush_buffer` por `do_SAK`.
3. Copiar o `SAK_work` original e trocar apenas `work.func` por
   `call_usermodehelper_exec_work`.
4. Preencher completion, path, argv e envp em `A+0x6200`.
5. Publicar o `subprocess_info` no PTY.
6. Publicar temporariamente `tty->ops = A+0x6400`.
7. Executar `ioctl(slave, TCFLSH, TCOFLUSH)`.
8. Restaurar imediatamente `tty->ops`.

`TCFLSH/TCOFLUSH` chega a `tty_driver_flush_buffer()`, cuja chamada indireta
tem a mesma assinatura CFI de `do_SAK(struct tty_struct *)`. `do_SAK()` entrega
o work a `schedule_work()`; a partir daí nenhuma estrutura global da workqueue
é alterada pelo payload.

## Completion e restauração

O helper usa uma completion não nula. Isso impede `umh_complete()` de executar
`kfree()` sobre o objeto embutido no PTY.

O código aguarda:

1. completion, provando que o callback e o `kernel_execve` terminaram;
2. conexão com `/data/local/tmp/temp_su.sock`;
3. restauração e leitura byte a byte dos 112 bytes originais.

Os descritores do PTY só são fechados depois dessa prova. Se uma escrita ou
restauração ficar ambígua, os descritores permanecem abertos e a tentativa é
marcada irreversível, evitando liberar um `tty_struct` que ainda possa estar
referenciado pela workqueue.

Se a tentativa falhar após mudar `selinux_enforcing`, o valor original é
restaurado.

## Provas exigidas

Log de sucesso:

```text
[root_umh] native PTY work tty=<direct-map> work=<tty+0x2f8> complete=1 socket=1 restore=1
```

O estágio temporário só passa com `socket=1` e `restore=1`. KernelSU e
`su -c id` continuam sendo provas posteriores e independentes.

## Limites

Os offsets são específicos do AFZH3. Mudança de firmware exige regenerar BTF e
kallsyms. O fluxo continua sendo uma mutação de kernel sensível: depois da
publicação do PTY, uma falha não permite nova tentativa no mesmo boot.
