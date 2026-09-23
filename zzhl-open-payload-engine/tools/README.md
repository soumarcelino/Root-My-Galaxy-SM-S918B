# Ferramentas de porting, análise e debug

Todas as ferramentas usam apenas Bash, Python 3 e utilitários comuns. Nenhuma
dependência Python externa é necessária. Scripts de device usam ADB primeiro e
exigem serial explícito quando uma ação pode alterar estado.

Neste clone ZZHL, use `tools/profiles/zzhl.json` e `run-zzhl-device.sh`.
Referências a AFZH3 nas seções históricas abaixo pertencem à base copiada.

## Visão rápida

| Ferramenta | Tipo | Modifica device? | Finalidade |
|---|---|---:|---|
| `preflight-device.sh` | Bash | não | identidade, boot, root e limites em JSON |
| `collect-forensics.sh` | Bash | não | last_kmsg, pstore, dmesg, logcat, kallsyms |
| `run-zzhl-device.sh` | Bash | **sim** com `--execute` | uma tentativa ZZHL, logs e coleta automática após reboot |
| `build-manifest.py` | Python | não | hashes, Git, toolchain e metadados ELF |
| `compare-elf.py` | Python | não | comparar binário fechado/candidato |
| `analyze-run-log.py` | Python | não | classificar logs e extrair provas |
| `scaffold-target.py` | Python | não | criar perfil incompleto para novo firmware |
| `audit-profile.py` | Python | não | conferir literais/offsets e hash do payload |
| `derive-symbol-offsets.py` | Python | não | derivar offsets de um kallsyms salvo |
| `extract-btf-layouts.sh` | Bash | não | extrair layouts BTF locais ou do device |
| `check-zzhl-abi.py` | Python | não | validar todos os layouts BTF consumidos pelo payload ZZHL |
| `check-repo.py` | Python | não | links, segredos, sintaxe, perfil e build |
| `validate-two-boots.sh` | Bash | **sim** | campanha final 2-reboots com gates |
| `soak-boots.sh` | Bash | **sim** | soak retomável de 3 boots e resumo comparável |
| `summarize-soak.py` | Python | não | agrega sucesso e duração por perfil do soak |

## 1. Preflight do device

```sh
rtk tools/preflight-device.sh --serial RXCX602E20X
rtk tools/preflight-device.sh --serial RXCX602E20X \
  --expect-build S918BXXSAFZH3 --require-clean \
  --output evidence/preflight.json
```

Campos: modelo, codinome, firmware, kernel, boot ID, uptime, boot completion,
bootreason, presença/funcionamento de `su`, pipe budgets e threads-max.

`--require-clean` falha se o boot não terminou ou `/system/bin/su` já existe.

## 2. Coleta forense

```sh
rtk tools/collect-forensics.sh --serial RXCX602E20X
rtk tools/collect-forensics.sh --serial RXCX602E20X --deep \
  --output evidence/forensics/caso-001
```

Sem root coleta dados públicos. Com root já funcional adiciona last_kmsg,
dmesg e pstore. `--deep` inclui kallsyms, slabinfo e tracefs. Cada coleta gera
`manifest.json` com hashes e tamanhos.

O script não tenta obter root nem altera tracing.

No ZZHL, `collect-forensics.sh` também baixa o arquivo Samsung
`dumpstate_lastkmsg_*.log.gz` (ZIP apesar do sufixo) e extrai
`samsung-lastkmsg.txt` e `panic-excerpt.txt`. `--previous-lastkmsg CAMINHO` impede que um dump
antigo seja atribuído à tentativa atual. O estado fica em
`samsung-lastkmsg-status.txt` (`downloaded`, `unchanged`, `missing` ou erro).

Para uma tentativa do clone ZZHL com coleta automática no boot de recuperação:

```sh
rtk tools/run-zzhl-device.sh --serial RXCX602E20X          # plano
rtk tools/run-zzhl-device.sh --serial RXCX602E20X --execute
```

O runner salva `device.log`, `summary.json` e, após falha com reboot,
`recovery/` com logcat, DropBox, trace e Samsung last_kmsg completo. Faz só
uma tentativa; aguarda até 900 s pelo próximo boot. Não usa o runner AFZH3.

## 3. Manifesto de build

```sh
rtk tools/build-manifest.py build/oss_clone_payload.so \
  --target-build S918BXXSAFZH3 \
  --asset helper=/home/matias/Projects/ksu-payload-functional/assets/ksu-helper \
  --output evidence/manifests/afzh3.json
```

Registra SHA-256, tamanho, modo, cabeçalho ELF, commit/branch/dirty state,
Python e compilador detectado.

## 4. Comparação ELF

```sh
rtk tools/compare-elf.py /caminho/ksu-payload-fechado \
  build/oss_clone_payload.so --output evidence/elf-comparison.json
```

Compara:

- hashes e tamanhos;
- classe, máquina, tipo e entrypoint;
- seções e diferenças de tamanho;
- bibliotecas `NEEDED`;
- imports exclusivos;
- strings comuns e amostras exclusivas.

Não afirma equivalência semântica. O relatório orienta Ghidra/radare2.

## 5. Análise de logs

```sh
rtk tools/analyze-run-log.py /tmp/oss-validation-boot1.log \
  /tmp/oss-validation-boot2.log --format markdown \
  --output evidence/run-report.md
```

Classificações:

- `PASS_ROOT`;
- `PARTIAL_KSU_NO_SU_PROOF`;
- `PARTIAL_TEMP_ROOT`;
- `PARTIAL_KERNEL_RW`;
- `PARTIAL_KASLR`;
- `FAIL_EARLY_OR_INCONCLUSIVE`.

Também extrai base KASLR, groom, pipe victim, UMH, KernelSU, UID e erros.

## 6. Perfis de alvo

Perfil validado atual:

```text
tools/profiles/afzh3.json
```

Auditoria:

```sh
rtk tools/audit-profile.py tools/profiles/afzh3.json \
  --artifact build/oss_clone_payload.so
```

Novo port:

```sh
rtk tools/scaffold-target.py \
  --name dm3q-NOVO_BUILD --model SM-S918B --device dm3q \
  --build NOVO_BUILD --kernel TODO \
  --output tools/profiles/novo-build.json
```

O scaffold nasce com `INCOMPLETE_DO_NOT_EXECUTE` e campos `TODO`. Ele não copia
silenciosamente offsets AFZH3.

Offsets de símbolos de um snapshot kallsyms:

```sh
rtk tools/derive-symbol-offsets.py evidence/kallsyms.txt \
  --base-symbol _text --symbol init_task --symbol system_unbound_wq \
  --output evidence/symbol-offsets.json
```

Layouts BTF locais:

```sh
rtk tools/extract-btf-layouts.sh --btf evidence/vmlinux.btf \
  --struct pipe_buffer --struct pool_workqueue \
  --output evidence/btf-layouts
```

Auditoria completa dos campos ABI usados pelo port ZZHL:

```sh
rtk python3 tools/check-zzhl-abi.py \
  --btf /home/matias/Projects/Root-My-Galaxy-SM-S918B/ZZHL/vmlinux_ZZHL.btf
```

Ou diretamente do device, usando root já existente e somente leitura:

```sh
rtk tools/extract-btf-layouts.sh --serial RXCX602E20X \
  --output evidence/btf-layouts-afzh3
```

## 7. Checks do repositório

```sh
rtk tools/check-repo.py
rtk tools/check-repo.py --build
```

Executa:

- links Markdown;
- padrões comuns de segredo;
- `bash -n`;
- compilação sintática Python;
- auditoria do perfil AFZH3;
- shellcheck quando instalado;
- build opcional.

## 8. Validação em dois reboots

Modo plano, sem alterar device:

```sh
rtk tools/validate-two-boots.sh \
  --runner /home/matias/Projects/ksu-payload-functional/simple-root
```

Para evitar o gate host duplicado quando `stability-launcher` já controla
temperatura, memória, PSI, slab, pipes, uptime e cooldown:

```sh
rtk tools/validate-two-boots.sh --skip-host-quiet ...
```

Execução explícita:

```sh
rtk tools/validate-two-boots.sh \
  --serial RXCX602E20X \
  --expect-build S918BXXSAFZH3 \
  --expect-sha256 a22ff696a2c096a45c62fbc0bd9c4bf8918d9783886d7227637a5ca980f8a53c \
  --runner /home/matias/Projects/ksu-payload-functional/simple-root \
  --execute
```

Gates:

- serial, build e hash obrigatórios;
- reboot antes de cada execução;
- boot ID único;
- boot completo e `SU_ABSENT`;
- uma execução por boot;
- prova externa de UID 0;
- boot ID inalterado depois do payload;
- forense automática em falha;
- sucesso somente 2/2.

O orquestrador foi validado end-to-end na campanha `20260919T083500Z`: dois
boots limpos, dois UID 0, incluindo recuperação correta de um `pipe_rw setup
miss` antes do sucesso na tentativa 2.

## 9. Soak curto

O wrapper padrão executa três boots limpos, não envia mensagens ao Codex e
preserva um registro JSON por boot. Device offline pausa a campanha; registros
com boot ID válido permitem retomada. Ao iniciar, abre uma janela Kitty com as
últimas 30 linhas e atualização em tempo real; `q` fecha o visualizador sem
interromper a campanha.

```sh
rtk env SERIAL=RXCX602E20X \
  CAMPAIGN_DIR=evidence/reliability/soak-3 \
  tools/soak-boots.sh
```

Ao terminar, `summary.json` contém taxa de sucesso, média, mediana e perfil
recomendado. `TOTAL_BOOTS` e `BATCH_SIZE` alteram o tamanho quando necessário.

## Regras para outros portings

1. Criar perfil novo com scaffold.
2. Preencher valores derivados do fechado/BTF/kallsyms.
3. Auditar perfil antes de build.
4. Comparar ELF e produzir manifesto.
5. Executar preflight somente-leitura.
6. Validar harnesses antes do payload completo.
7. Usar campanha 2-boots somente quando gates do port estiverem completos.
8. Fazer soak maior antes de declarar estável.

## Saídas e versionamento

`evidence/` é ignorado pelo Git por padrão porque pode conter endereços,
kallsyms e logs grandes. Manifests sanitizados podem ser movidos para um local
versionado quando necessário.
