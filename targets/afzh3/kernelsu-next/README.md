# KernelSU Next para SM-S918B

## AFZH3 / KernelSU Next v3.4.0

Guia completo, com diagnóstico, entradas fixadas, build, integração e
validação: [AFZH3-v3.4.0-PASSO-A-PASSO.md](AFZH3-v3.4.0-PASSO-A-PASSO.md).

O SM-S918B conectado com `S918BXXSAFZH3` roda
`5.15.189-android13-8-33413713-abS918BXXSAFZH3`. O arquivo
`aarch64-android13-5.15_kernelsu.ko` do release v3.4.0 tem `vermagic`
`5.15.202-android13-5.15.202_r00-dirty` e causou dois kernel panics no AFZH3.
Ambos ocorreram em `ksu_mark_running_process_locked+0x284` ao decrementar a
referência da credencial protegida do processo `init`. O KMI `android13-5.15`
identifica a família de build, mas não substitui o suporte Samsung KDP/RKP/DEFEX.
`simple-root.sh` exige agora o build Samsung abaixo e para antes do payload
caso o artefato ainda não exista.

O patch
`patches/KernelSU-Next-v3.4.0-samsung-afzh3-kdp-rkp-defex.patch` porta as
alterações Samsung da v3.3.0 para o commit oficial v3.4.0
`1a879d6a866f80b1fa1c1009a2ffa747873cbb5e`. Foi verificado que o patch
aplica sem conflitos sobre esse commit. A árvore Samsung AFZH3 usada é
`/home/matias/Projects/SM-S918B_16_Opensource/kernel_platform/common`.
A configuração foi extraída da imagem de boot AFZH3 e comparada byte a byte
com `/proc/config.gz` do aparelho. O compilador é o Clang r450784e original
(build 8508608).

Prepare `Module.symvers` e compile o `.ko` e o `ksud` da mesma versão,
embutindo o `.ko` no `ksud` para `late-load`:

```sh
./targets/afzh3/kernelsu-next/prepare-afzh3-kernel-v3.4.0.sh
./targets/afzh3/kernelsu-next/build-afzh3-v3.4.0.sh
```

O primeiro script gera o output do kernel em `targets/afzh3/kernelsu-next/out/afzh3-kernel/`.
O segundo exige `kernel.release` exato e `Module.symvers`, e grava os artefatos
em `targets/afzh3/kernelsu-next/out/kernelsu-next-afzh3-v3.4.0/`. O `ksud` usa
`KBUILD_MODPOST_WARN=1`: o carregador late-load da v3.4.0 resolve símbolos
internos por `/proc/kallsyms` antes de chamar `init_module`.

O build local terminou. Os CRCs de 2.905 símbolos do `vmlinux.symvers`
coincidiram com 431 módulos originais do firmware AFZH3. Os 203 símbolos
indefinidos do `.ko` estão no `System.map`, e seus 139 CRCs coincidem com
`Module.symvers`. `SHA256SUMS` acompanha o `.ko` e o `ksud` no diretório de
saída. O usuário confirmou funcionamento com o novo par integrado ao
`simple-root`; o guia detalha o alcance das provas registradas. O módulo
genérico não deve ser reutilizado no AFZH3.

Para empacotar o `.ko` oficial v3.4.0 já copiado para este diretório em um
`ksud` Android da mesma versão, execute
`./targets/afzh3/kernelsu-next/build-ksud-v3.4.0-release-ko.sh`. O resultado fica em
`targets/afzh3/kernelsu-next/out/kernelsu-next-v3.4.0-release-ko/ksud-next-v3.4.0`. Esse build
apenas embute o asset oficial no `ksud`; esse pacote genérico já causou panic
no AFZH3 e não deve ser usado pelo `simple-root.sh`.
