# Ferramentas, fontes e evidências que concretizaram o port

## Visão geral

O resultado não veio de uma única ferramenta. Foi necessário combinar análise
estática do fechado, fonte compatível do kernel, introspecção do kernel real,
forense de panic, execução controlada via ADB e comparação rigorosa de logs.

## Ghidra

### Uso

- decompilação headless do shared object fechado;
- identificação de funções `FUN_...`;
- reconstrução do fluxo do constructor e supervisor;
- descoberta de chamadas, branches, globals e layouts aparentes;
- correlação entre root stage, pipe primitive e workqueue.

### Contribuição

Ghidra forneceu o mapa de alto nível que permitiu dividir o projeto em módulos.
Também mostrou que o backend pipe era um estágio separado anterior ao
`root_umh`, detalhe essencial para abandonar configfs nos objetos dinâmicos.

### Limite

Decompiler pode esconder atomics, truncar tipos, inventar structs ou apresentar
ordem enganosa. Pontos críticos foram confirmados no disassembly bruto.

## radare2 (`r2`) e disassembly AArch64

### Uso

- inspeção por endereço virtual bruto;
- confirmação do handshake futex;
- identificação de `ldaddal`/atomic fetch-add que abriu o gate;
- verificação de ordem de chamadas e socketpairs;
- desativação de nomes automáticos enganosos (`asm.varsub`) em trechos críticos.

### Contribuição decisiva

Uma busca por escrita constante não encontrava quem abria o gate. A leitura das
instruções mostrou uma operação atômica LSE. Isso permitiu mover o callback para
o waiter e preservar o call site real.

## Python para extração binária

Scripts curtos leram bytes e tabelas diretamente do arquivo fechado, por
exemplo a tabela de oito delays em `.rodata`. Esse método evitou transcrever
valores apresentados de forma ambígua pelo decompiler.

Uso típico:

```text
open(binary, 'rb').read()[offset:offset+n]
struct.unpack_from('<...')
```

Também foi útil para comparar blobs, offsets, hashes e padrões sem executar o
payload.

## Fonte Samsung do kernel

Árvore:

```text
/home/matias/Projects/SM-S918B_16_Opensource/
```

### Uso

- semântica de `rt_mutex_adjust_pi` e `rt_mutex_adjust_prio_chain`;
- caminhos de workqueue e listas;
- interpretação de completion e `call_usermodehelper`;
- validação conceitual de campos BTF/offsets;
- entendimento de HARDENED_USERCOPY.

### Contribuição

A fonte transformou call traces em mecanismos concretos. No panic rtmutex,
permitiu ligar `task->pi_blocked_on`, `waiter->lock` e rb-tree. No root stage,
ajudou a entender por que publicar lista/contadores fora de ordem é perigoso.

### Limite

A árvore é compatível, mas não foi tratada como prova de identidade exata do
kernel instalado.

## BTF e kallsyms do device

### Uso

- confirmar símbolos e endereços no boot real;
- validar tamanho/offset de estruturas;
- comparar perfil AFZH3 com builds próximos;
- derivar endereços a partir da base KASLR.

### Contribuição

Evitaram usar perfis de firmware vizinho apenas porque offsets pareciam
plausíveis. Também sustentaram os checks direct-map e relações wq→pwq→pool.

## ADB com serial explícito

### Uso

- staging de helper/payload/ksud;
- execução shell não privilegiada;
- consultas de boot ID, uptime, build e kernel;
- prova final `su -c id`;
- coleta privilegiada quando root diagnóstico estava disponível;
- reboot limpo e espera do device.

### Contribuição

O device real resolveu diferenças que análise estática não podia prever:
allocator, timing de KernelSU, alias ashmem, pipe limits e races.

O serial `RXCX602E20X` foi sempre usado explicitamente para evitar operar outro
device conectado.

## `simple-root`

Local:

```text
/home/matias/Projects/ksu-payload-functional/simple-root
```

### Uso

- reproduzir exatamente o caminho operacional real;
- verificar ELF magic e permissões dos assets;
- carregar o `.so` por `LD_PRELOAD`;
- fornecer `CVE43499_ROOT_HELPER`;
- executar KernelSU late-load;
- provar `su`.

### Contribuição decisiva

Testar o clone fora desse runner poderia produzir sucesso artificial ou falha
irrelevante. O primeiro boot também revelou que o próprio detector era cedo
demais. O poll de 30 s eliminou esse falso negativo sem mudar o exploit.

## NDK Clang e Make

Ferramentas:

```text
Android NDK 28.2.13676358
aarch64-linux-android35-clang
make -B -j2 all so
```

### Uso

- build PIE e shared object;
- warnings `-Wall -Wextra`;
- `_Static_assert` de layouts;
- mesmo conjunto de fontes nos dois formatos.

### Contribuição

O build simples e determinístico tornou fácil comparar o artefato testado com o
arquivo staged por SHA-256.

## `sha256sum`, `bash -n`, `rg` e utilitários

- `sha256sum`: garantiu que build e asset executado eram idênticos.
- `bash -n`: evitou validar um runner com erro sintático.
- `rg`: localizou símbolos, logs, offsets e assinaturas de panic rapidamente.
- `od`: verificou ELF magic no device.
- `timeout` e `tee`: limitaram execução e preservaram log completo.
- `readelf`/inspeção ELF, quando aplicada: confirma formato e carregamento.

Essas ferramentas não provam root isoladamente; aumentam rastreabilidade.

## kernelsnitch

Código incorporado em `src/03_mm_address_sidechannel/`.

### Uso

- criar colisões controladas;
- vazar candidata `mm_struct`;
- bruteforce/cleanup da geometria;
- repetir a técnica nos dois reclaims.

### Contribuição

Forneceu a ponte entre objetos userspace/memfds e a base order-3 `A`. A análise
do efeito colateral na afinidade levou ao repin CPU0, uma das correções centrais.

## tracefs e ftrace

### Uso

- leak KASLR via `sched_blocked_reason`;
- kprobes para observar caminhos críticos;
- `ftrace_dump_on_oops=1` para persistir buffer no panic;
- filtros por `common_pid`.

### Contribuição

Tracefs é parte do caminho produtivo para KASLR. ftrace/kprobes foram parte da
forense que separou falha de trigger de falha de root stage.

### Lições

- `common_comm` não era campo válido no filtro usado;
- o código KASLR pode desligar tracing;
- buffer grande exige captura completa, não um tail curto.

## `/proc/last_kmsg`, pstore e dumpstate

### Uso

- distinguir reboot real de queda ADB;
- recuperar PC/LR/call trace;
- localizar HARDENED_USERCOPY;
- localizar `list_del corruption`;
- analisar panic rtmutex;
- extrair `Dumping ftrace buffer`.

### Contribuição decisiva

Três avanços vieram de evidência persistente:

1. rtmutex: mostrou race pós-`WAIT_REQUEUE_PI`, motivando v14;
2. clone/root_umh: mostrou configfs lendo `pool_workqueue`, motivando pipe R/W;
3. fechado: mostrou corrupção de lista da workqueue, motivando revalidação e
   wake imediato.

Sem last_kmsg, esses eventos pareceriam apenas “ADB sumiu”.

## Root funcional de laboratório

Root já funcional foi usado para:

- kallsyms;
- BTF/layouts;
- slabinfo;
- last_kmsg;
- tracefs/kprobes;
- checagem de limites;
- confirmação externa de estado.

Ele não foi usado como substituto da prova final. Nos boots de aceitação, o
estado começou sem `su` e o clone precisou criar a cadeia de root.

## Binário fechado funcional

O fechado serviu como oracle comportamental:

- demonstrou que o alvo era explorável;
- forneceu ordem e constantes;
- confirmou execução de uma vez por boot;
- mostrou separação configfs/pipe;
- também revelou que a race da workqueue não era exclusiva do clone.

Comparar apenas sucesso/falha seria insuficiente; o valor real veio de comparar
estágios, lifetimes e crash traces.

## Logs estruturados por estágio

Marcas como `stage=...`, `verify ok`, `pipe_rw ready`, `root_umh result` e
`KernelSU control verified` permitiram localizar o primeiro ponto divergente.

Essa granularidade concretizou a investigação:

- antes: “não dá root”;
- depois: “landing passou, restore passou, panic ao ler `pwq+0` por configfs”.

Uma hipótese ampla virou uma correção pequena e testável.

## Regra dos dois reboots

A exigência de dois boots independentes foi uma ferramenta metodológica:

- elimina estado residual;
- confirma variação normal de KASLR/victim;
- reduz falso positivo de KernelSU já carregado;
- reduz falso negativo de uma condição transitória;
- documenta que o mesmo artefato funciona em duas geometrias reais.

## Evidências que mais ajudaram

Em ordem de impacto:

1. last_kmsg completo com call trace;
2. comparação direta do caminho fechado configfs versus pipe;
3. logs com readback exato de FOPS;
4. prova bidirecional do backend pipe;
5. fonte kernel para interpretar estruturas e races;
6. disassembly bruto para resolver atomics/ordem;
7. execução pelo runner real;
8. dois reboots limpos com boot IDs distintos;
9. hash do artefato staged;
10. `su -c id` como critério final.

## Práticas que não ajudaram ou enganaram

- retries cegos após panic;
- assumir que perda de ADB era transporte;
- usar tail curto de last_kmsg;
- tratar configfs como R/W universal;
- confiar só no decompiler;
- usar perfil de firmware próximo;
- medir sucesso por build/startup;
- consultar `su` uma única vez imediatamente;
- inserir logs dentro de janelas de allocator/lista;
- repetir no mesmo boot.

## Preservação para futuras análises

Para manter o trabalho reproduzível, arquivar:

- binário fechado analisado e seu hash;
- projeto Ghidra/export de símbolos;
- scripts/notebooks de extração;
- dumps BTF/kallsyms;
- fonte kernel usada;
- last_kmsg e ftrace completos;
- logs de ambos os boots;
- manifesto de hashes dos assets;
- versão das ferramentas.

Essa coleção transforma um port “que funcionou uma vez” em uma base auditável.
