# KernelSU Next v3.4.0 para ZZHL

Este diretório contém o port validado do KernelSU Next para o `SM-S918B`
com firmware `S918BXXUAZZHL` e kernel
`5.15.197-android13-8-34343818-abS918BXXUAZZHL`.

## Compatibilidade

O release oficial v3.4.0 oferece o KMI `android13-5.15`, mas o módulo genérico
não deve ser carregado diretamente neste kernel Samsung. O arquivo oficial usa
`vermagic` `5.15.202-android13-5.15.202_r00-dirty`; além disso, o ZZHL habilita
KDP, RKP, DEFEX e proteção IMR. A mutação convencional de `cred` do módulo
genérico não respeita o caminho protegido da Samsung.

Origem: <https://github.com/KernelSU-Next/KernelSU-Next/releases/tag/v3.4.0>.

| Arquivo oficial preservado em `upstream/` | SHA-256 |
|---|---|
| `KernelSU_Next_v3.4.0_33294-release.apk` | `50339a93c0f812b8a72c1a387a1b441891e3df0f20b2d9daf80fd798d04b3de8` |
| `aarch64-android13-5.15_kernelsu.ko` | `c6814bb47ebb853f39aa77bd0579f86e9c2f4c180b1de9ab5dd01f1c7403b723` |
| `ksud-aarch64-linux-android` | `52ba3da465459bfbac98dd25c53f8d31f93ee5a8ee89d84f67a28fe0d18cdef2` |

O port usa o tag oficial `1a879d6a866f80b1fa1c1009a2ffa747873cbb5e` e o
patch Samsung em `patches/`, cujo commit de referência é
`c300cb9d712126e8ac2a678014a1f2e8adbb9362`. Ele adiciona credenciais KDP por
tarefa, sincronização DEFEX, evita escrita de texto bloqueada pelo RKP e usa
kprobes como fallback. A versão de controle é `33295`: `33294` do release mais
um commit do port Samsung.

## Verificação estática

`verify-zzhl-compat.py` verificou o módulo final contra os dados ZZHL:

- 203 símbolos indefinidos no módulo, todos presentes no `kallsyms` ZZHL;
- 2.544 CRCs comuns comparados em 367 módulos stock, sem divergência;
- layouts BTF idênticos para `cred`, `task_struct`, `work_struct`,
  `completion`, `ucounts`, `user_struct` e `user_namespace`;
- `vermagic` exato do kernel ZZHL.

O relatório reproduzível está em
`out/kernelsu-next-zzhl-v3.4.0/compatibility.json`.

## Build

O build requer o worktree KernelSU no commit Samsung, a árvore Samsung 5.15 já
preparada e os módulos stock ZZHL em `reference-modules/`:

```sh
adb pull /vendor_dlkm/lib/modules reference-modules/vendor_dlkm
JOBS=8 ./build-zzhl-v3.4.0.sh
```

O script recompila o módulo, troca apenas a string de release de mesmo tamanho,
embute o `.ko` no `ksud`, executa a verificação e grava `SHA256SUMS`.

| Artefato | SHA-256 |
|---|---|
| `android13-5.15_kernelsu.ko` | `21f41628172ed1e539d25159a5ade23cb9561d7c654c1e793b6d887f25387309` |
| `ksud-next-v3.4.0` | `971f173977dec6d944f13be16343860218dce82d8431f45300de626fabbfab28` |

## Validação no aparelho

Em 2026-09-27, o runner executou o payload e carregou o artefato final no boot
`b2e9b62d-4082-43e5-b03d-582c78b726bd`. O boot permaneceu inalterado, o
SELinux voltou para `Enforcing` e o controle respondeu:

```text
KernelSU control verified version=33295 flags=0x5 uapi=4 features=0x2714
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
```

A evidência local está em
`brazilian-open-payload-engine/evidence/zzhl-fresh/20260927T114521Z-execute/`.

O runner usa esse loader automaticamente em `--execute`. Para validar apenas o
root temporário, use `--no-kernelsu`.

O mesmo loader está no perfil Android `dm3q-S918BXXUAZZHL-ksunext`. O script
`../brazilian-open-payload-engine/tools/zzhl-app-bundle.py` sincroniza e verifica payload,
helper, launcher, factory e `ksud` dentro do APK.
