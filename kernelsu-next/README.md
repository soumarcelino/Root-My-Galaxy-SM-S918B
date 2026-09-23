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
./kernelsu-next/prepare-afzh3-kernel-v3.4.0.sh
./kernelsu-next/build-afzh3-v3.4.0.sh
```

O primeiro script gera o output do kernel em `kernelsu-next/out/afzh3-kernel/`.
O segundo exige `kernel.release` exato e `Module.symvers`, e grava os artefatos
em `kernelsu-next/out/kernelsu-next-afzh3-v3.4.0/`. O `ksud` usa
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
`./kernelsu-next/build-ksud-v3.4.0-release-ko.sh`. O resultado fica em
`kernelsu-next/out/kernelsu-next-v3.4.0-release-ko/ksud-next-v3.4.0`. Esse build
apenas embute o asset oficial no `ksud`; esse pacote genérico já causou panic
no AFZH3 e não deve ser usado pelo `simple-root.sh`.

## AFZG1 / KernelSU Next v3.3.0

Este diretório contém o port reproduzível do KernelSU Next para o Samsung
Galaxy S23 Ultra `SM-S918B` (`dm3q`), firmware
`S918BXXSAFZG1`.

Use somente em aparelhos próprios ou com autorização explícita. O root é
temporário e uma incompatibilidade no módulo pode reiniciar o aparelho.

## Base validada

```text
KernelSU Next: v3.3.0
commit: 3b18216f71df189ab3d1b1ce0bdb21be1268e771
version code: 33214
kernel: 5.15.189-android13-8-33413713-abS918BXXSAFZG1
compiler: Android clang 14.0.7 r450784e
Android: 16 / API 36
```

O módulo foi carregado no hardware em modo late-load e confirmou:

```text
KernelSU control verified version=33214 flags=0x5 uapi=2
Samsung KDP task-scoped credential and native PGD path enabled
Samsung DEFEX credential synchronization and KSU-task bypass enabled
dispatcher unavailable; syscall event hooks disabled
Samsung setresuid kretprobe registered
Samsung sucompat kprobes registered
```

## Conteúdo

- `patches/KernelSU-Next-v3.3.0-samsung-afzg1-kdp-rkp-defex.patch`:
  delta completo sobre o tag `v3.3.0`.
- `helper/su_daemon-next.c`: helper late-load usando
  `com.rifsxd.ksunext`.
- `helper/target-afzg1.h`: target guard exato do AFZG1.
- `helper/target_guard.c/.h`: validação fail-closed do target em runtime.
- `build-afzg1.sh`: build do módulo, ksud e helper.

## Alterações necessárias

1. Credenciais Samsung KDP são instaladas por `prepare_ro_creds()` e
   `kdp_assign_pgd()`; referências protegidas usam
   `kdp_usecount_dec_and_test()`.
2. O estado de credenciais do DEFEX é sincronizado após cada elevação.
3. Escrita direta em texto/syscall table é desativada para o perfil Samsung.
4. Quando o RKP bloqueia o dispatcher, setresuid e sucompat usam kretprobe e
   kprobes por endereço.
5. Inicialização falha fechada se símbolos KDP/DEFEX obrigatórios faltarem.
6. O helper remove a opção antiga `--ephemeral`, seleciona o pacote Next e
   aguarda até dez segundos pelo driver, pois `ksud late-load` daemoniza.

## Build

Clone o KernelSU Next e selecione exatamente a base validada:

```sh
git clone https://github.com/KernelSU-Next/KernelSU-Next.git
cd KernelSU-Next
git checkout 3b18216f71df189ab3d1b1ce0bdb21be1268e771
git apply /caminho/Root-My-Galaxy-SM-S918B/kernelsu-next/patches/KernelSU-Next-v3.3.0-samsung-afzg1-kdp-rkp-defex.patch
```

O output Samsung deve estar previamente preparado com a configuração e
headers SELinux do kernel 5.15. Execute:

```sh
KSU_NEXT_SRC=/caminho/KernelSU-Next \
SAMSUNG_KERNEL_SRC=/caminho/kernel_platform/msm-kernel \
KERNEL_OUT=/caminho/kernel-out \
CLANG_ROOT=/caminho/clang-r450784e \
ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/28.2.13676358 \
./kernelsu-next/build-afzg1.sh
```

Artefatos são gravados em `kernelsu-next/out/kernelsu-next-afzg1/` e permanecem ignorados
pelo Git.

## Teste e rollback

Não carregue KernelSU tradicional e Next no mesmo boot. Reinicie antes de
trocar a implementação. Preserve sempre o helper e ksud tradicionais como
rollback. Após o late-load:

```sh
adb shell su -c id
adb shell su -c 'dmesg | grep -E "Samsung KDP|Samsung DEFEX|dispatcher|sucompat"'
```

O target guard deve ser executado antes do exploit:

```sh
adb push kernelsu-next/out/kernelsu-next-afzg1/ksu-helper-next /data/local/tmp/
adb shell chmod 0755 /data/local/tmp/ksu-helper-next
adb shell /data/local/tmp/ksu-helper-next --probe
```
