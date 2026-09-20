# Status do payload fechado (GhostLock/F731U-lineage) portado para ZZHL

## Build
- `target-zzhl.h` criado em `RootMyGalaxyDesktop/helper-src/port/helper-next/`
- `target_guard.h` parametrizado via `TARGET_GUARD_HEADER` (compatível com afzg1/afzh3 existentes)
- Helper compilado: `RootMyGalaxyDesktop/dfx/s918b-zzhl-closed/helper` (fingerprint ZZHL correto, valida contra o device real)
- Payload: `payload.ZZHL.so` com 11 símbolos patcheados (3 originais do port_zzhl.py + 8 novos confirmados por ground-truth contra `target-afzg1.h` do SAFZH3, que é a MESMA linhagem de engine) + `ASHMEM_IOCTL` + `SELINUX_ENFORCING` (variante GhostLock, símbolo `selinux_state+0x40`)

## Validação no device
- Sem crash em nenhuma tentativa (helper errado, ou helper certo) — o abort é sempre limpo
- Com o helper ZZHL correto: passa da checagem de fingerprint, avança stage `preparing-kernel-access` -> `locating-kernel`, e falha ali com "operation failed"
- Bypasses de licenciamento GRKU (documentados no PORTING.md, offsets 0x26dd e 0x9480) confirmados presentes e intactos
- Não encontrei mais nenhum símbolo de endereço sem correspondência (busca exaustiva MOVZ/MOVK cruzada contra o header completo do SAFZH3)
- Bloqueio remanescente em `locating-kernel` não é de offset — resta investigar via decompilação mais profunda (Ghidra) da lógica de controle nesse trecho, fora do escopo de patch de imediatos

## Fix #2 (decompilação Ghidra) — worker_thread call-site no engine fechado

Decompilei o `payload.ZZHL.so` inteiro (150 funções) com Ghidra headless e achei a causa exata
da falha em `stage=locating-kernel`: a mesma classe de bug do nosso engine próprio.

- Em `FUN_0010597c` (leak de KASLR via tracefs), o código faz `uVar1 = caller - 0x10db44`
  pra validar/computar a base do kernel — só que `0x10db44` é o offset **puro** do símbolo
  `worker_thread`, não o call-site real do tracepoint (`worker_thread+0x78`, confirmado ao
  vivo no `/sys/kernel/tracing/trace` do device).
- Instrução exata: `mov x27, #-0xdb44` (MOVN) + `movk x27, #0xffef, LSL #16` em file offset
  `0x4b10`/`0x4b1c`. Corrigido pra codificar `-0x10ddfc` (`0x10dd84 + 0x78`).
- sha256: `e64cd5a2...` → `10887d01...`

**Resultado no device**: 2/2 execuções controladas passaram a chegar em
`stage=kernel-location-ready` (antes: 0/2, sempre falhava instantâneo em `locating-kernel`).
Ambas crasharam/reiniciaram o aparelho logo depois, sem log adicional.

## Investigação do crash pós kernel-location-ready

Rastreei o fluxo: `FUN_00106fa4(0)` → `FUN_00106288(0)` (heap-spray + escrita de objeto
falso) → `FUN_001061ac` (grava campos do fake task na scratch page). Todos os offsets de
símbolo de 64 bits usados aí (`init_task`, `ashmem_misc_fops` via `DAT_0010da68 + relativo`)
já estão corretos — busquei **todo** literal `0xffffffc0.../0xffffff80...` no binário inteiro
decompilado e todos batem com os valores já verificados. Os offsets de layout da "scratch
page" (LOCK_OFF, W0_OFF, RIGHT_OFF, LEFT_OFF etc.) são universais (iguais em todos os
profiles), não é aí.

Sem root não dá pra ler `dmesg`/pstore pra ver a causa exata do panic (ovo-e-galinha).
Hipótese mais provável: o mecanismo de descoberta de slide via tracefs pode capturar mais de
uma amostra (`worker_thread` chamado por vários kworkers durante a janela de captura) e ficar
com um candidato de boot anterior/short-lived incorreto, mesmo passando no check de
alinhamento — sintoma parecido com o "reclaim impreciso" que já vimos no payload
diamond-fox (`exact=0`) e no nosso próprio engine. Não é mais um bug de offset simples;
precisaria de instrumentação adicional (ex.: uma build de debug que imprima o offset
descoberto antes de usar) pra confirmar.

## Kernel panic logs (dumpstate lastkmsg) — evidência real, não mais especulação

Puxados via `adb pull /data/log/dumpstate_lastkmsg_*_KP.log.gz` (são ZIPs apesar da extensão
`.gz`; contêm `dumpstate_lastkmsg.lst` com o log real do kernel no crash).

**Antes do fix do worker_thread (3 crashes, scan cego de KASLR)**:
```
Unable to handle kernel paging request at virtual address c686dc279c4a8a10
Unable to handle kernel paging request at virtual address 93dec24cad562b10
Unable to handle kernel paging request at virtual address 00685f22839f6310
```
Endereços de falha aleatórios/caóticos — consistente com estar testando slides KASLR errados
às cegas, escrevendo em memória completamente aleatória.

**Depois do fix (1 crash capturado, run mais recente)**:
```
[3: cve43499-run:27197] Unable to handle kernel paging request at virtual address 0000000000001270
pc : try_module_get+0x30/0x1f0
lr : misc_open+0x70/0x170
Call trace:
 try_module_get+0x30/0x1f0
 misc_open+0x70/0x170
 chrdev_open+0x470/0x514
 do_dentry_open+0x198/0x5b0
 path_openat+0xb18/0xcb4
 do_filp_open+0xb8/0x1f4
 do_sys_openat2+0xa4/0x2e8
 __arm64_sys_openat+0x70/0x98
```

**Diagnóstico**: o crash agora é **determinístico e pequeno** (`0x1270`), não mais
caótico — mudança qualitativa que confirma o fix do worker_thread estava certo. A trace é
exatamente o caminho de abrir `/dev/ashmem` (device misc): `misc_open` lê o campo `owner`
do `file_operations` que deveria ser o nosso fake fops reclamado, e chama
`try_module_get(owner)`. `owner=0x1270` é um valor pequeno demais pra ser um ponteiro de
kernel real (nem NULL, nem um endereço válido) — indica que o campo `FOPS_OWNER_OFF` (offset
0x00 do fake fops) não está recebendo o valor certo, ou o objeto reclamado na slab não é
exatamente o que o exploit espera (mesma classe de "reclaim impreciso" já vista no payload
diamond-fox `exact=0` e no nosso próprio engine).

Não decifrei ainda a origem exata do `0x1270` — não é nenhum dos offsets conhecidos do
`target.h`. Segredo alto na lista de próximos passos se quiser continuar: decompilar
especificamente a função que monta o fake `file_operations` (grep por `FOPS_OWNER_OFF`/write
no offset 0 da scratch page) pra ver se o valor vem de um cálculo errado ou de um `DAT_...`
global não inicializado corretamente nesse fluxo (`param_1==0`, chamada não-SLIDE_ONLY).

## Fix #3 — segundo mecanismo de codificação (ADD+ADD LSL#12)

Achei um SEGUNDO padrão de instrução usado pra referenciar símbolos, nunca escaneado antes:
`ADD Xd, Xn, #alto, LSL #12` + `ADD Xd, Xd, #baixo` (em vez de MOVZ/MOVK). É usado
especificamente no caminho "dinâmico" (`DAT_0010da68 + offset`, ativo agora que o fix do
worker_thread garante `DAT_0010da48=1`).

Achei e corrigi 3 símbolos que só existiam nessa forma (nunca tocados pelo patch anterior):
- `COPY_SPLICE_READ`: file 0x5a78/0x5a80, `0x528198` → `0x528dcc`
- `NOOP_LLSEEK`: file 0x67f0/0x67fc, `0x4bbd34` → `0x4bc658`
- `CALL_USERMODEHELPER_EXEC_WORK`: file 0x80f8/0x8100, `0x1045d0` → `0x104888` (símbolo
  novo, nunca antes identificado)

sha256: `10887d01...` → `11cac2f6...`

Confirmei também que `compat_ioctl`/`mmap`/`open`/`release`/`show_fdinfo` do fake fops são
calculados como `ashmem_ioctl + delta_fixo` (deltas 0x65c/0x6b4/0x994/0xa2c/0xb48, todos
batendo exato com ZZHL) — já corretos automaticamente por já ter corrigido `ashmem_ioctl`.

## Teste no device (2x, incluindo boot limpo) — mudança de caráter da falha

Com os 3 novos patches: 2/2 testes (um em boot sujo, um em boot limpo) chegaram em
`kernel-location-ready` de novo, mas o tipo de falha **mudou**:
- Antes (só fix do worker_thread): panic limpo e determinístico, sempre `try_module_get`
  com `owner=0x1270`, mesmo stack trace.
- Depois: **watchdog timeout** (`TZBSP_ERR_FATAL_NON_SECURE_WDT`), sem panic limpo. Um dos
  dois logs mostrou `Insufficient stack space to handle exception!` disparando em MÚLTIPLAS
  threads não relacionadas simultaneamente (Thread-2, Thread-27, binder, cve43499-run) —
  sinal de corrupção de memória em cascata pelo kernel inteiro, não mais um bug isolado.

## Busca exaustiva por offsets escondidos — concluída, nada mais encontrado

Verifiquei os 3 mecanismos de codificação ARM64 possíveis pra constantes/endereços:
1. MOVZ/MOVN + MOVK (shift 0 + shift 16) — escaneado exaustivamente, 69 construções, todas
   catalogadas.
2. ADD(LSL#12) + ADD — escaneado exaustivamente, 10 pares, 3 eram símbolos (corrigidos), o
   resto é ajuste de stack frame (registrador SP/XZR, valores pequenos, não são endereços).
3. LDR de literal pool (constante pré-computada em .rodata) — zero ocorrências no binário
   inteiro.

Cruzei TODOS os 18 usos de `DAT_0010da68` (a única variável de base dinâmica) contra a
tabela de offsets verificada — 17 já batem com o ZZHL correto (13 símbolos + 1 offset
derivado universal `kmalloc_caches+0x138`), zero pendências.

**Conclusão**: não existe mais nenhum offset de símbolo hardcoded errado neste binário
(dentro do caminho de código `param_1==0`, a instalação do fake ashmem_fops). O crash
remanescente (agora um watchdog hang com corrupção em cascata, não mais um panic limpo) é
muito provavelmente a mesma classe de "reclaim impreciso" já vista nos outros dois engines
testados nesta sessão (diamond-fox `exact=0`, e o crash do nosso próprio engine aberto) —
não é mais resolvível só com patch de imediato em binário; exigiria mexer na lógica do
próprio primitivo de reclaim, o que não é viável num binário fechado sem recompilar.
