# Automação operacional AFZH3

> **Escopo histórico:** este documento pertence ao engine AFZH3 usado como
> base. A automação ZZHL vigente é `tools/run-zzhl-device.sh`.

Este documento reúne o fluxo reutilizável criado após a investigação do panic
`misc_open -> try_module_get`, a correção da afinidade do reclaim e a entrega
do payload dentro do aplicativo.

## Ferramentas adicionadas

| Ferramenta | Função | Altera device? |
|---|---|---:|
| `tools/check-critical-reclaim.py` | Confere CPU única, fábrica exec, `close_range` e gate exato do reclaim. | não |
| `tools/afzh3-app-bundle.py` | Sincroniza payload/factory/launcher, atualiza tamanhos e confere conteúdo do APK. | não |
| `tools/build-install-app.sh` | Executa build, testes, sincronização, APK e instalação opcional com prova por hash. | somente com `--install` |
| `tools/collect-app-runs.sh` | Coleta últimos históricos do app, logcat, last_kmsg e slab atual. | não |
| `tools/analyze-app-runs.py` | Analisa históricos e panics já coletados. | não |

Os scripts usam somente Bash, Python 3, ADB, Gradle e ferramentas já exigidas
pelo projeto. Não há dependência Python externa.

## Pipeline de build e aplicativo

Build completo sem tocar no device:

```sh
tools/build-install-app.sh
```

O pipeline executa, nesta ordem:

1. build PIE, payload compartilhado, fábrica `execve` e stability launcher;
2. testes focados e checks de geometria;
3. verificação estática da CPU crítica;
4. cópia do payload, da fábrica e do launcher para os assets do app;
5. atualização pontual dos tamanhos em `targets-v3.json`, preservando o
   formato do arquivo;
6. testes unitários Android e `assembleDebug`;
7. comparação dos assets locais com o conteúdo real do APK;
8. `git diff --check` e relatório JSON.

Para compilar o launcher, o script usa primeiro `ANDROID_NDK_HOME` ou
`ANDROID_NDK_ROOT`. Se a instalação indicada não tiver o clang para API 35,
ele procura automaticamente um NDK compatível em `$ANDROID_SDK_ROOT/ndk`.

Build, instalação e prova do APK instalado:

```sh
tools/build-install-app.sh --install --serial RXCX602E20X
```

Com `--install`, o script exige serial explícito, usa `adb install -r`, obtém o
caminho com `pm path`, puxa o `base.apk` e exige que seu SHA-256 seja idêntico
ao APK local. O relatório padrão é `build/afzh3-app-bundle.json`.

Para sincronizar ou conferir artefatos separadamente:

```sh
tools/afzh3-app-bundle.py sync
tools/afzh3-app-bundle.py verify \
  --apk ../../../app/build/outputs/apk/debug/app-debug.apk
```

## Proteção da invariante de reclaim

O `mm_struct` é alocado durante `clone()` ou `execve`. A CPU da criação precisa
ser a mesma usada nas liberações, drains e reclaim; fixar apenas o filho depois
da criação não garante isso.

`00_cpu_discovery.c` cruza a afinidade efetiva com `cpu_capacity`, frequência
máxima e o estado Samsung `core_ctl`. CPUs pausadas ou `not_preferred` são
descartadas quando existe uma opção estável. A política é revalidada antes das
janelas críticas, pois o `core_ctl` pode mudar depois da seleção inicial.

O check exige a seguinte ordem:

```text
KernelSnitch setup
  -> parent pin na CPU dinâmica
  -> um PID fábrica cria pre31
  -> clone do alvo
  -> mesmo PID fábrica cria post32
  -> PCP priming
  -> close_range(62 vizinhos), mantendo o leak vivo
  -> close_range(38 seeds de slabs distintos)
  -> confirmação da mesma CPU e do core_ctl
  -> close_range(apenas a referência leak conhecida)
  -> primeiro sendmsg imediato
  -> prova exata: -32 objetos, -1 slab ativo, -1 slab total
```

`active_objs` permanece no log, mas não é gate: `/proc/slabinfo` não contabiliza
objetos livres presos nas listas per-CPU. Manter o leak por último faz o alvo
chegar à lista parcial do nó ainda ocupado; a liberação final então descarta o
slab de forma síncrona.

Execução isolada:

```sh
tools/check-critical-reclaim.py
tools/check-critical-reclaim.py --artifact build/payload.so
```

`tools/check-repo.py` executa automaticamente a verificação de fonte. A opção
`--artifact` também exige os marcadores de CPU, `close_range` e prova exata.
O fundamento no SLUB, a ordem cage/seed/final e os resultados no device estão
em [Reclaim determinístico e CPU discovery](15-RECLAIM-DETERMINISTICO-E-CPU-DISCOVERY.md).

## Coleta das últimas execuções do app

```sh
tools/collect-app-runs.sh --serial RXCX602E20X --count 3
```

O coletor tenta `run-as` primeiro e usa root já funcional somente como fallback.
Ele não executa o payload, não reinicia o aparelho e não tenta obter root.

Saídas principais:

| Arquivo | Conteúdo |
|---|---|
| `history/` | JSONs originais, incluindo `.corrupt`. |
| `history-index.tsv` | Ordem e tamanho dos arquivos coletados. |
| `device-state.txt` | Boot ID, firmware, uptime, bootreason e root atual. |
| `logcat-current.txt` | Buffers atuais do logcat. |
| `dropbox-last-kmsg.txt` | Último registro Samsung disponível pelo DropBox. |
| `proc-last-kmsg.txt` | Último kmsg quando root já existe. |
| `slab-snapshot.txt` | Estado de `mm_struct`, skb e `kmalloc-4k`. |
| `analysis.json` | Relatório estruturado. |
| `analysis.md` | Resumo humano. |
| `manifest.json` | Hash e tamanho de toda evidência. |

Para adicionar a forense geral já existente:

```sh
tools/collect-app-runs.sh --serial RXCX602E20X --count 3 --forensics
tools/collect-app-runs.sh --serial RXCX602E20X --count 3 --deep
```

`--deep` pode gerar arquivos grandes porque inclui kallsyms, slabinfo completo e
tracefs.

## Análise offline

Qualquer diretório de evidência pode ser reanalisado sem ADB:

```sh
tools/analyze-app-runs.py evidence/app-runs/CASO \
  --extract-logs \
  --json evidence/app-runs/CASO/analysis.json \
  --markdown evidence/app-runs/CASO/analysis.md
```

O analisador extrai tempos de factory, proc-spray, KernelSnitch, reclaim, pipe e
root. Também reconhece:

- JSON vazio ou preenchido somente com zeros;
- sucesso completo com AAR/AAW, restauração, pipe, root e KernelSU;
- execução interrompida durante o gate, preservando a última linha útil;
- CPU rápida e CPU do ciclo crítico;
- panic genérico;
- assinatura específica `try_module_get -> misc_open`, classificada como
  `MISC_FOPS_OWNER` no primeiro `misc_open` anterior à prova AAR/AAW.

Cópias idênticas do mesmo panic em `/proc/last_kmsg` e DropBox são deduplicadas.

## Fluxo recomendado depois de um crash

1. Não repetir o payload no mesmo boot quando houve mutação possível.
2. Coletar as últimas execuções com `collect-app-runs.sh`.
3. Conferir `analysis.md` e preservar o diretório completo.
4. Corrigir e executar `check-critical-reclaim.py`.
5. Gerar o app com `build-install-app.sh`.
6. Fazer campanha em boots distintos com `validate-two-boots.sh` ou
   `soak-boots.sh`.

Build e instalação provam integridade dos artefatos. A confiabilidade do
reclaim continua exigindo boots limpos e prova externa de root.
