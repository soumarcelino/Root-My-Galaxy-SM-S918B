# DYI3 Open Payload Engine

Port para o Samsung **SM-S911U1** (`dm1q`) no firmware **`S911U1UES6DYI3`**
(One UI 7, kernel `5.15.153-android13-8-30958972-abS911U1UES6DYI3`).

O código parte do engine ZZHL (`../zzhl/`), mas o contrato **não** foi herdado:
`src/target.h` é derivado do `boot.img` deste aparelho (símbolos do ELF, campos
e tamanhos do BTF). `src/target.h` é gerado — não edite à mão. Os dois desvios
que este alvo exigiu em relação ao doador estão descritos em
[`STATUS.md`](STATUS.md).

## Build

```sh
make -B all so tests test-aar-read-plan test-fops-layout test-compact-log
make -C ../helper -B
```

O alvo do NDK vem de `ANDROID_NDK_HOME` (ou do fallback em `$HOME/Android/Sdk`).

## Execução no aparelho

Use o runner do repositório, que confere a identidade exata do aparelho
(modelo, device, display, kernel), exige boot limpo, passa pelo launcher de
estabilidade e só então executa:

```sh
RMG_TRACE_FILE=1 bash simple-root-dyi3.sh <serial>
```

Variáveis úteis: `RMG_SKIP_LATELOAD=1` (para no root temporário),
`RMG_DIAG_CAPTURE=1` (captura dmesg/pstore antes do late-load),
`RMG_TRACE_FILE=1` (checkpoints duráveis em `/data/local/tmp/rmg-trace.txt`).

## Testes de host

`tests/` traz diagnósticos que rodam na máquina de build, sem aparelho. O mais
importante é `test_aar_write_plan.c`: prova que o encoder de planos de escrita
cobre **todos** os endereços que a rota determinística pode tocar (varredura de
3,3 M endereços) e que os planos já conhecidos não mudaram.

```sh
make test-aar-read-plan test-fops-layout test-compact-log
cc -O2 -Isrc tests/test_aar_write_plan.c -o /tmp/t && /tmp/t   # ~46 min
```

## KernelSU Next

O módulo e o `ksud` ficam em [`../kernelsu-next/`](../kernelsu-next/), com o
script de build e a auditoria (CRC + layout contra o BTF do aparelho +
relocação de `this_module`). Para reconstruir do zero depois de um clone novo,
use `tools/bootstrap-dyi3-build.sh` na raiz do repositório.
