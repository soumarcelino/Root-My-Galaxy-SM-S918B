# Validação no aparelho

## Política

Toda validação final exige duas execuções em reboots limpos independentes. O
payload, assim como o fechado observado, tem comportamento prático de uma
execução por boot. Repetir depois de falha/sucesso pode medir estado residual,
holders, caches, pipes ou KernelSU já carregado.

Root pré-existente pode ser usado para coleta e forense. A prova final começa
sem `su` e executa o clone pelo contexto shell normal.

## Artefato validado

```text
Projeto:  /home/matias/Projects/Root-My-Galaxy-SM-S918B/afzh3-open-payload-engine
Saída:    build/oss_clone_payload.so
Tamanho:  101120 bytes
SHA-256:  a22ff696a2c096a45c62fbc0bd9c4bf8918d9783886d7227637a5ca980f8a53c
Runner:   /home/matias/Projects/ksu-payload-functional/simple-root
Serial:   RXCX602E20X
Firmware: S918BXXSAFZH3
```

O hash do build e do payload copiado ao diretório de teste foi comparado antes
da validação e era idêntico.

## Critérios por boot

### Precondições

- ADB autorizado e estado `device`;
- `sys.boot_completed=1`;
- build incremental exato;
- `boot_id` novo;
- `/system/bin/su` ausente;
- nenhuma execução anterior do payload naquele boot.

### Cadeia esperada

1. `stage=locating-kernel`;
2. base KASLR válida;
3. leak/alinhamento do groom;
4. readback exato de `ashmem_misc.fops`;
5. restauração global `ok=1`;
6. `[pipe_rw] ready`;
7. `[root_umh] ... writes=1/1/1/1/1`;
8. `wake=1 complete=1 socket=1`;
9. `stage=temporary-root-ready`;
10. `exploit completed attempt=1/24` ou tentativa posterior explícita;
11. `KernelSU control verified`;
12. `su -c id` com UID 0;
13. `boot_id` ainda igual.

## Boot 1

```text
boot_id: 0c8b8a94-560d-461a-8512-fcb990bf88ac
estado inicial: SU_ABSENT
```

Evidência do payload:

```text
[kaslr] source=tracefs base=ffffffc0080f0000 slide=00000000000f0000
[groom] mm leaked=ffffff8a64de1c00 aligned_base=ffffff8a64de0000
[aar_aaw] verify ok: corruption landed, R/W primitive live
[immediate] restore ashmem_misc.fops ... ok=1
[pipe_rw] ready attempt=1/12 ... pipe=107
[root_umh] ... writes=1/1/1/1/1
[root_umh] result wake=1 complete=1 socket=1
stage=temporary-root-ready
exploit completed attempt=1/24
KernelSU control verified version=33214 flags=0x5 uapi=2 features=0x2714
```

A versão antiga do runner consultou `su` imediatamente e registrou
“inaccessible or not found”. Uma consulta posterior, no mesmo boot ID,
retornou:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
```

Isso isolou um falso negativo do runner: a exploração e o late-load tinham
passado; a interface `su` ainda estava sendo publicada.

Log: `/tmp/oss-validation-boot1.log`.

## Ajuste do runner entre provas

O runner passou a:

- anunciar espera de até 30 s;
- executar `/system/bin/su -c 'id'` a cada segundo;
- exigir texto `uid=0(root)`;
- distinguir comando sempre indisponível de comando que respondeu sem UID 0;
- preservar o shell interativo final.

O orquestrador também repete a prova externa por até 30 s. Isso cobre a janela
transitória em que `/system/bin/su` existe, mas a substituição/publicação ainda
retorna `ETXTBSY` (`Text file busy`).

`bash -n simple-root` passou.

## Boot 2

```text
boot_id: f6047267-c565-4145-8e2d-64bca0eb7ab6
boot_completed: 1
build: S918BXXSAFZH3
uptime pré-teste: 28.41 s
estado inicial: SU_ABSENT
```

Evidência:

```text
[kaslr] source=tracefs base=ffffffc0081a8000 slide=00000000001a8000
[groom] mm leaked=ffffff8a3624c400 aligned_base=ffffff8a36248000
[aar_aaw] verify ok: corruption landed, R/W primitive live
[immediate] restore ashmem_misc.fops ... ok=1
[pipe_rw] ready attempt=1/12 ... pipe=81
[root_umh] ... writes=1/1/1/1/1
[root_umh] result wake=1 complete=1 socket=1
stage=temporary-root-ready
exploit completed attempt=1/24
KernelSU control verified version=33214 flags=0x5 uapi=2 features=0x2714
[+] Root confirmado
```

Prova externa final, ainda no mesmo boot ID e uptime 162.40 s:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
```

Log: `/tmp/oss-validation-boot2.log`.

## Matriz final

| Critério | Boot 1 | Boot 2 |
|---|---:|---:|
| Novo boot ID | passou | passou |
| `SU_ABSENT` inicial | passou | passou |
| Landing fake FOPS | passou | passou |
| Restauração global | passou | passou |
| Pipe backend | tentativa 1 | tentativa 1 |
| UMH completion/socket | passou | passou |
| KernelSU control | passou | passou |
| `su -c id` UID 0 | passou | passou |
| Reboot/panic durante teste | não | não |

## Interpretação

Diferenças de base KASLR, endereço do slab e índice do pipe entre boots são
esperadas. A estabilidade correta está nas invariantes e provas, não em
endereços idênticos.

O resultado 2/2 demonstra root funcional do código aberto no alvo exato com o
artefato registrado. Não autoriza extrapolar offsets para outro firmware.

## Revalidação automatizada após criação das tools

Uma segunda campanha completa foi executada com
`tools/validate-two-boots.sh`, usando o mesmo payload SHA-256 e o runner staged.
O orquestrador reiniciou antes de cada teste, exigiu build exato, `SU_ABSENT`,
boot IDs distintos, execução única, UID 0 e boot ID inalterado.

```text
Campanha: evidence/campaigns/20260919T083500Z
Boot 1: dbd6aa38-7ea1-4713-ac0f-0beabad8c3c8 — PASS
Boot 2: 64009fbc-f695-4841-95cf-454799d8b0bb — PASS
Payload: a22ff696a2c096a45c62fbc0bd9c4bf8918d9783886d7227637a5ca980f8a53c
```

Boot 1 instalou o backend pipe na tentativa 1, victim 109. Boot 2 teve um miss
recuperável na primeira geometria e instalou na tentativa 2, victim 123. Nos
dois boots, o primeiro groom/trigger passou na tentativa 1, `root_umh` retornou
`wake=1 complete=1 socket=1`, KernelSU confirmou `version=33214` e a prova
externa retornou:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
```

Esse resultado também valida o cleanup/retry interno do pipe após um setup
miss, além do caminho ideal da primeira tentativa. O total acumulado passa a
quatro boots limpos com root em quatro execuções.

## Soak de cinco boots com tela ligada

O artefato
`bf41c36892a8fa69eddb761e3bbf7ab2a61b887c213e1aa53686bf92cde33a3f`
passou em cinco boots limpos e independentes com a tela ligada. Antes de cada
execução, o orquestrador exigiu três amostras estáveis de memória disponível,
temperatura, tarefas executáveis e PSI de CPU/memória/I/O. O `loadavg` de um
minuto foi preservado como telemetria, sem bloquear o teste por carga antiga.

```text
Boot 1: adf593a3-14c4-4ed4-a9af-fc87b561738b — PASS — pipe tentativa 2
Boot 2: 6cf7822f-6935-4004-a944-94b65d6cec11 — PASS — pipe tentativa 2
Boot 3: 435c269d-31f8-4376-92ae-b3d6d8504176 — PASS — pipe tentativa 2
Boot 4: 428016e3-d2a1-455f-843f-31ffb4e0365d — PASS — pipe tentativa 1
Boot 5: 1961aa0a-c54f-42e6-86d7-cad1d888b6a2 — PASS — pipe tentativa 1
```

Nos três boots, a primeira geometria falhou na prova `read-string` com
`errno=0` e leitura do marcador `0x61`; a reconstrução adaptativa passou na
tentativa seguinte. Não houve retry externo, reboot inesperado ou panic. A
evidência está em
`evidence/reliability/20260920-bf41c368/screen-on-soak3-v2/` e
`evidence/reliability/20260920-bf41c368/screen-on-extra2/`.

## Evidência mínima para futuras validações

Guardar por boot:

- timestamp;
- hash do payload;
- serial/model/build/kernel;
- boot ID antes e depois;
- uptime;
- presença/ausência de `su` antes;
- stdout/stderr completo;
- `su -c id` final;
- last_kmsg/pstore se houver disconnect ou reboot.

Sem esses dados, classificar resultado como parcial.
