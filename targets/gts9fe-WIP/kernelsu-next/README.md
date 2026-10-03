# KernelSU Next para SM-X518U (gts9fe)

## GTS9FE / KernelSU Next v3.4.0

O SM-X518U conectado com `X518UVLSFEZG3` roda
`5.15.189-android13-3-33478785` (`#1 SMP PREEMPT Sat Jul 18 02:16:22 KST 2026`).
O `.ko` do release oficial v3.4.0 tem `vermagic`
`5.15.202-android13-5.15.202_r00-dirty` e é recusado por esse kernel. O KMI
`android13-5.15` identifica a família de build, mas não substitui o suporte
Samsung KDP/RKP/DEFEX nem o `vermagic` exato.

O patch `patches/KernelSU-Next-v3.4.0-samsung-gts9fe-kdp-rkp-defex.patch` porta
as alterações Samsung da v3.3.0 para o commit oficial v3.4.0
`1a879d6a866f80b1fa1c1009a2ffa747873cbb5e`. Verificado com
`patch -p1 --dry-run` sobre uma extração limpa do tag v3.4.0: os 15 arquivos
aplicam sem conflitos.

## Configuração do módulo

O módulo é compilado com o DDK `ghcr.io/ylarod/ddk-min:android13-5.15-20260828`,
que fornece o `Module.symvers` do KMI `android13-5.15`. As opções Samsung são
habilitadas explicitamente:

```text
CONFIG_KSU=m
CONFIG_KSU_SAMSUNG_KDP=y
CONFIG_KSU_SAMSUNG_RKP=y
CONFIG_KSU_SAMSUNG_DEFEX=y
CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y
KCFLAGS="-D__ANDROID_COMMON_KERNEL__ -DCONFIG_DEBUG_INFO_BTF_MODULES=1"
```

`KBUILD_MODPOST_WARN=1` é necessário: o carregador `late-load` da v3.4.0 resolve
símbolos internos por `/proc/kallsyms` antes de chamar `init_module`, então o
`modpost` estrito do DDK falha em símbolos que existem no aparelho.

## Build

```sh
./targets/gts9fe-WIP/kernelsu-next/build-gts9fe-v3.4.0.sh
./targets/gts9fe-WIP/kernelsu-next/build-ksud-v3.4.0-ko.sh
```

O primeiro script compila `out/kernelsu-next-gts9fe-v3.4.0/android13-5.15_kernelsu.ko`
com o `vermagic` reescrito para `5.15.189-android13-3-33478785`. O segundo compila o `ksud`
embutindo o `.ko` em `userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko`,
que é o que o app usa em `--late-load`.

O `ksud` compilado é instalado como `/data/adb/ksud` e o módulo é carregado
pelo `late-load`; o app verifica o canal de controle com
`KernelSU control verified` antes de marcar a instalação como concluída.

## Artefatos

`out/kernelsu-next-gts9fe-v3.4.0/` acompanha o `.ko` e o `ksud` com
`SHA256SUMS`. Os binários enviados ao app são:

```text
app/src/main/assets/ksud-next-v3.4.0-gts9fe    (6268176 B)
```

O `.ko` não é enviado separado ao app: ele vai embutido no `ksud`.

## Validação no aparelho

```sh
adb shell 'cat /proc/modules | grep kernelsu'
# kernelsu 303104 1 - Live 0x0000000000000000 (OE)
```

`KernelSU control verified version=33294 flags=0x5 uapi=4 features=0x2714`
aparece no log da instalação quando o canal de controle responde.
