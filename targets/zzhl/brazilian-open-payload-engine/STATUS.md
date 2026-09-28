# Estado do port ZZHL

Port recriado em 2026-09-27 a partir do engine AFZH3 atual, sem reutilizar o
payload ZZHL antigo do Git.

- alvo: `SM-S918B / dm3q / S918BXXUAZZHL`;
- identidade, release e fingerprint são verificados antes do payload;
- 22 símbolos são conferidos contra o ELF ZZHL;
- 33 campos e 5 tamanhos de estruturas são conferidos contra o BTF ZZHL;
- o BTF relevante coincide com a geometria usada pelo engine-base;
- launcher próprio: `build/stability-launcher-zzhl`;
- runner próprio: `tools/run-zzhl-device.sh`;
- o diretório legado `../payload` permanece removido.

O `SLIDE_ONLY` do port novo foi validado no boot
`94a0b622-1ece-4f1f-a60a-835abe767fb3`: o launcher aprovou estabilidade e
capacidade de pipes, a descoberta via tracefs encontrou
`base=ffffffc008038000` (`slide=0x38000`) e o boot permaneceu intacto. Esse modo
encerrou antes de groom, futex ou mutação do kernel.

A primeira execução completa confirmou reclaim e AAR/AAW, mas o aparelho
reiniciou por watchdog ao restaurar `ashmem_misc.fops`. Uma segunda execução
isolou o mesmo travamento em `fops-restore-futex-start`. A correção monta o
backend físico de pipes antes da restauração e escreve o ponteiro original
pelo alias linear calculado de `memstart_addr` e `kimage_voffset`.

Essa rota foi validada em 2026-09-27 no boot
`c9356db4-04e7-4175-b1e1-a903fb7b5d02`. A tentativa concluiu na primeira
passagem, confirmou AAR/AAW, instalou o pipe R/W, restaurou e releu
`ashmem_misc.fops`, iniciou o UMH e manteve o mesmo boot. O helper retornou:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

A evidência está em `evidence/zzhl-fresh/20260927T111906Z-execute/`. O root
temporário ZZHL está validado.

O KernelSU Next v3.4.0 foi portado para KDP/RKP/DEFEX e validado novamente em
boot limpo `b2e9b62d-4082-43e5-b03d-582c78b726bd`. A execução
`evidence/zzhl-fresh/20260927T114521Z-execute/` concluiu o exploit na primeira
tentativa, carregou o módulo ZZHL embutido no `ksud`, preservou o boot e
confirmou:

```text
KernelSU control verified version=33295 flags=0x5 uapi=4 features=0x2714
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
Enforcing
```

O runner seleciona esse loader por padrão e considera a execução aprovada
somente se o controle KernelSU for verificado. `--no-kernelsu` mantém o modo de
root temporário para diagnóstico.

## Correção do encoder ConfigFS

Três execuções do app revelaram uma falha no encoder NUL da primitiva
ConfigFS. O teste host assumia cópia byte a byte, enquanto o `strscpy()` do
ZZHL grava blocos de oito bytes e pode alterar bytes posteriores ao NUL. Isso
explica tanto `private PTY staging failed` quanto o panic posterior em
`configfs_read_iter`.

A correção simula o `strscpy()` real, rejeita planos divergentes antes da
syscall, faz preflight antes do futex e abandona ConfigFS AAR depois da
instalação do pipe. SELinux e dados da imagem passam pelo alias linear.

O APK corrigido foi validado no boot limpo
`6a6be15d-44c6-4b14-b722-dd94997649b7`, execução
`9075374d-a736-4da8-b0cd-17ab4bc3b739`: primeira tentativa, root em 35,169 s,
KernelSU Next confirmado, SELinux enforcing e pstore vazio. A análise completa
está em `docs/18-INCIDENTE-STRSCPY-CONFIGFS.md`.

Após essa correção, a campanha final completou **10/10 reboots limpos com
sucesso**. Cada boot executou o fluxo completo sem reboot inesperado, confirmou
KernelSU Next, retornou root no domínio `u:r:ksu:s0` e terminou com SELinux em
`Enforcing`.
