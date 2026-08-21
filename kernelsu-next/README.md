# KernelSU Next para SM-S918B AFZG1

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

Artefatos são gravados em `out/kernelsu-next-afzg1/` e permanecem ignorados
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
adb push out/kernelsu-next-afzg1/ksu-helper-next /data/local/tmp/
adb shell chmod 0755 /data/local/tmp/ksu-helper-next
adb shell /data/local/tmp/ksu-helper-next --probe
```
