# Ferramentas de porting, análise e debug

Todas as ferramentas usam apenas Bash, Python 3 e utilitários comuns. Nenhuma
dependência Python externa é necessária. Scripts de device usam ADB primeiro e
exigem serial explícito quando uma ação pode alterar estado.

## Visão rápida

| Ferramenta | Tipo | Modifica device? | Finalidade |
|---|---|---:|---|
| `preflight-device.sh` | Bash | não | identidade, boot, root e limites em JSON |
| `collect-forensics.sh` | Bash | não | last_kmsg, pstore, dmesg, logcat, kallsyms |
| `build-manifest.py` | Python | não | hashes, Git, toolchain e metadados ELF |
| `compare-elf.py` | Python | não | comparar binário fechado/candidato |
| `analyze-run-log.py` | Python | não | classificar logs e extrair provas |
| `scaffold-target.py` | Python | não | criar perfil incompleto para novo firmware |
| `audit-profile.py` | Python | não | conferir literais/offsets e hash do payload |
| `derive-symbol-offsets.py` | Python | não | derivar offsets de um kallsyms salvo |
| `extract-btf-layouts.sh` | Bash | não | extrair layouts BTF locais ou do device |
| `check-repo.py` | Python | não | links, segredos, sintaxe, perfil e build |
| `validate-two-boots.sh` | Bash | **sim** | campanha final 2-reboots com gates |
| `soak-boots.sh` | Bash | **sim** | soak retomável de 3 boots e resumo comparável |
| `summarize-soak.py` | Python | não | agrega sucesso e duração por perfil do soak |
| `check-critical-reclaim.py` | Python | não | impede regressão da CPU crítica do reclaim |
| `afzh3-app-bundle.py` | Python | não | sincroniza e confere payload/factory/launcher/APK |
| `build-install-app.sh` | Bash | opcional | build, testes, APK e instalação verificável |
| `collect-app-runs.sh` | Bash | não | últimos históricos do app, logcat e panic |
| `analyze-app-runs.py` | Python | não | tempos, sucesso, JSON corrompido e assinatura do panic |

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

## 3. Manifesto de build

```sh
rtk tools/build-manifest.py build/payload.so \
  --target-build S918BXXSAFZH3 \
  --asset helper=/home/matias/Projects/ksu-payload-functional/assets/ksu-helper \
  --output evidence/manifests/afzh3.json
```

Registra SHA-256, tamanho, modo, cabeçalho ELF, commit/branch/dirty state,
Python e compilador detectado.

## 4. Comparação ELF

```sh
rtk tools/compare-elf.py /caminho/ksu-payload-fechado \
  build/payload.so --output evidence/elf-comparison.json
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
  --artifact build/payload.so
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
  --base-symbol _text --symbol init_task --symbol do_SAK \
  --symbol do_SAK_work --symbol call_usermodehelper_exec_work \
  --output evidence/symbol-offsets.json
```

Layouts BTF locais:

```sh
rtk tools/extract-btf-layouts.sh --btf evidence/vmlinux.btf \
  --struct pipe_buffer --struct tty_struct --struct tty_operations \
  --output evidence/btf-layouts
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

## 10. Bundle e instalação do app AFZH3

Pipeline completo sem alterar o device:

```sh
tools/build-install-app.sh
```

Com instalação e comparação do APK puxado do aparelho:

```sh
tools/build-install-app.sh --install --serial RXCX602E20X
```

Uso separado do sincronizador/verificador:

```sh
tools/afzh3-app-bundle.py sync
tools/afzh3-app-bundle.py verify
```

## 11. Últimas execuções do aplicativo

```sh
tools/collect-app-runs.sh --serial RXCX602E20X --count 3
```

O resultado inclui os JSONs originais, estado do device, logcat, last_kmsg
disponível, snapshot dos slabs, relatório Markdown/JSON e manifest de hashes.
`--forensics` integra a coleta geral; `--deep` inclui canais privilegiados
maiores quando root já funciona.

Reanálise sem device:

```sh
tools/analyze-app-runs.py evidence/app-runs/CASO \
  --json evidence/app-runs/CASO/analysis.json \
  --markdown evidence/app-runs/CASO/analysis.md
```

## 12. Invariante da CPU crítica

```sh
tools/check-critical-reclaim.py
tools/check-critical-reclaim.py --artifact build/payload.so
```

O check exige seleção e revalidação dinâmica de uma CPU estável, uma fábrica
crítica por `execve`, ausência de `sched_yield()`, leak conhecido liberado por
último, flush de `cpu_partial` por 38 slabs distintos e gate exato de 32
objetos/um slab. `check-repo.py` executa a verificação de fonte automaticamente.

O fluxo detalhado, arquivos produzidos e procedimento pós-crash estão em
[`docs/14-AUTOMACAO-OPERACIONAL.md`](../docs/14-AUTOMACAO-OPERACIONAL.md).
A análise de SLUB e a validação da ordem protegida estão em
[`docs/15-RECLAIM-DETERMINISTICO-E-CPU-DISCOVERY.md`](../docs/15-RECLAIM-DETERMINISTICO-E-CPU-DISCOVERY.md).

## Saídas e versionamento

`evidence/` é ignorado pelo Git por padrão porque pode conter endereços,
kallsyms e logs grandes. Manifests sanitizados podem ser movidos para um local
versionado quando necessário.
