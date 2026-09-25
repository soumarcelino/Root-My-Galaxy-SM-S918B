# Troubleshooting e forense

## Diagnóstico por último estágio confirmado

| Último log | Área provável |
|---|---|
| antes de `kernel-location-ready` | tracefs, permissões, base KASLR |
| `kernel-location-ready`, sem leak | slabinfo/kernelsnitch/limites |
| leak, sem `verify ok` | afinidade, ondas de drain, fake FOPS |
| `verify ok`, sem restore | offset real FOPS ou AAW inicial |
| restore ok, sem pipe ready | segundo reclaim, limites de pipe, cache scan |
| pipe ready, sem queued | validações wq/pwq/pool |
| queued, sem completion | lista/worker/fake subprocess_info |
| completion, sem socket | helper/argumento/SELinux/socket |
| temporary root, sem KernelSU | helper late-load/ksud |
| KernelSU verificado, sem `su` imediato | aguardar poll de 30 s |

## Falha de KASLR

Verificar:

- tracefs montado e acessível;
- `sched_blocked_reason` disponível;
- páginas/bytes coletados;
- `tracing_on` não deixado desligado por tentativa anterior;
- fallback `SLIDE_P0_OFFSET` alinhado e dentro de `0..0x1f0000`.

Não inventar base pela proximidade de outro firmware.

## `kernelsnitch collision finding failed`

Possíveis causas:

- pressão de allocator diferente;
- processos/FDs residuais;
- limites baixos;
- execução anterior no mesmo boot;
- afinidade incorreta;
- mudança no tamanho/order do `mm_struct`.

Registrar `/proc/slabinfo` e não repetir dezenas de vezes no mesmo boot.

## AAR/AAW não confirma landing

Comparar:

- `got` versus `want=A+0x1180`;
- alias ashmem escolhido;
- `st_rdev` canônico;
- base `A`, não `D`;
- repin CPU0 após kernelsnitch;
- split 16/16 da preparação;
- pre31/post32 e ordem de close;
- tamanho exato `0x8e80`.

Se a abertura ashmem falha depois do trigger, tratar como mutação terminal;
liberar/repetir pode deixar o kernel usando página reciclada.

## Pipe backend não fica ready

Se `setup miss`, registrar `reason`, `stage`, `errno` e `elapsed_ms`:

- conferir `RLIMIT_NOFILE` e limites de pipe;
- confirmar 240 pipes em cada banco;
- confirmar socketpair reclaim antes de PCP;
- verificar repin CPU0;
- conferir caches kmalloc-2k e offsets do alvo;
- observar se o marker/len identifica algum candidato;
- distinguir falha do scan, confirmação de byte ou prova bidirecional.

O retry interno já faz cleanup. Não adicionar retries externos ilimitados.
O deadline de preparação é diagnóstico e não mata holder em fase allocator;
um miss concluído permite retry interno. `restore` é terminal para a instalação
corrente.

## HARDENED_USERCOPY

Assinatura conhecida:

- cache `pool_workqueue`;
- caminho `configfs_read_iter`;
- acesso a campo dinâmico como `pwq+0`.

Interpretação: backend incorreto para a classe de objeto. Não é resolvido por
trocar apenas largura da leitura. Campos dinâmicos devem usar pipe R/W.

## Corrupção de lista/workqueue

Assinaturas:

- `list_del corruption`;
- backlink apontando para `A+0x6008`;
- panic logo após publicação/wake;
- UFS ou outro work entrando na mesma pool.

Coletar last_kmsg. Verificar se o pool mudou entre validação e publicação. Não
adicionar rollback cego depois da escrita da lista.

## Panic em rt_mutex

Assinatura histórica:

```text
sched_setattr
rt_mutex_adjust_pi
rt_mutex_adjust_prio_chain
rb_erase ou dereferência waiter->lock
```

O v14 foi adotado para permitir cleanup/reentrada do kernel na janela após
`WAIT_REQUEUE_PI`, mantendo o handshake relevante. Não reativar busy-spin
antigo como default sem nova bisseção e forense.

## Coleta de `/proc/last_kmsg`

Após reboot por panic, com root restaurado para diagnóstico:

```sh
rtk adb -s RXCX602E20X shell su -c 'cat /proc/last_kmsg' > /tmp/last_kmsg.txt
rtk rg -n 'panic|Unable to handle|Oops|Call trace|HARDENED_USERCOPY|list_del|rt_mutex' /tmp/last_kmsg.txt
```

Extrair o bloco completo `Dumping ftrace buffer:`. Capturar somente tail de
128 KiB pode perder a call trace, porque o dump pode ter vários MiB.

## ftrace e kprobes

Lições registradas:

- `ftrace_dump_on_oops=1` torna o buffer disponível no last_kmsg;
- filtros devem usar `common_pid`; `common_comm` não existe em todos os eventos;
- o estágio KASLR pode alterar `tracing_on`;
- probes armados antes podem parar de registrar;
- descobrir TIDs antes de aplicar filtro por PID;
- preservar log raw antes de renderização/filtragem.

## ADB caiu: reboot ou transporte?

Não inferir. Verificar:

1. retorno do serial em `adb devices -l`;
2. boot ID;
3. uptime;
4. bootreason;
5. last_kmsg.

Boot ID novo e uptime baixo confirmam reboot. Conexão USB instável sem mudança
de boot ID é falha de transporte.

## KernelSU passou, `su` falhou

Se `KernelSU control verified` apareceu:

- aguardar o poll completo de 30 s;
- consultar caminho absoluto `/system/bin/su`;
- exigir `uid=0(root)`;
- confirmar boot ID não mudou.

O primeiro teste final provou que a consulta imediata gera falso negativo.

## Classificação do resultado

| Classe | Definição |
|---|---|
| PASS final | dois boots limpos com UID 0 |
| PASS provisório | um boot limpo completo |
| Parcial | estágio intermediário confirmado |
| Miss recuperável | falha antes de mutação terminal, cleanup seguro |
| Falha terminal do boot | lista/FOPS potencialmente publicada, panic ou estado ambíguo |
| Inconclusivo | log/boot ID/evidência insuficiente |

Essa classificação evita transformar ausência de informação em hipótese de
sucesso ou falha.
