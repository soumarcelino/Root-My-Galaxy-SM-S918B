# Port ZZHL novo e campanha no aparelho

Este documento registra a implementação e a validação do port novo para o
Samsung Galaxy S23 Ultra `SM-S918B`, codinome `dm3q`, firmware
`S918BXXUAZZHL`. Ele é a fonte consolidada para o estado ZZHL em 2026-09-27.

## Escopo e proveniência

O port foi recriado a partir do engine aberto atual em
`targets/afzh3/brazilian-open-payload-engine`, no commit-base `cb1d37d`. O diretório
legado `targets/zzhl/payload` foi removido e nenhum source, offset ou
binário dele integra esta implementação.

O firmware e os dados auxiliares vieram de:

- `targets/zzhl/firmware/`;
- `/home/matias/Downloads/ZZHL/`.

Os arquivos equivalentes nas duas origens foram comparados por SHA-256 e são
idênticos. O port não reutiliza endereço runtime nem slide KASLR de outro boot.

## Identidade exata do alvo

| Campo | Valor |
|---|---|
| Modelo | `SM-S918B` |
| Device | `dm3q` |
| Build | `S918BXXUAZZHL` |
| Fingerprint | `samsung/dm3qxxx/dm3q:17/CP2A.260605.016/S918BXXUAZZHL:user/release-keys` |
| Kernel | `5.15.197-android13-8-34343818-abS918BXXUAZZHL` |
| Base estática | `0xffffffc008000000` |
| Serial validado | `RXCX602E20X` |

O payload confere modelo, device, build, fingerprint e release do kernel antes
de preparar acesso ao kernel. O runner repete essas verificações e exige boot
concluído, ausência do socket temporário e ausência de holders anteriores.

## Integridade dos insumos

| Arquivo | SHA-256 |
|---|---|
| `boot_ZZHL.img` | `748e90ab15e44af1369d05c193f2d94cdfc742434b61878f0e07dd77708c61ff` |
| `config_ZZHL` | `1676b2bd5d12a82a34c61f53fd8792b674bcea4f95ed903f66ba21913e966df5` |
| `config_ZZHL.gz` | `fcca21304f7fce7b1fc2224195f4888408e4e37e9c7f546294e4703bee761677` |
| `kallsyms_ZZHL.kallsyms` | `75f27a71d04a63015ca245b4837285b5268ee386e5a6c6307c05c46688086d90` |
| `kernel_ZZHL.bin` | `287d7f2f41fd815d2341c53d2dc8cde30f1b9c9c973128eca0547c62c5f71e01` |
| `vmlinux_ZZHL.btf` | `c021c42ebbd61ae67cd94971c699269a2e35bd6a702754a5de2f1c50ce4ddb57` |
| `vmlinux_ZZHL.elf` | `0ca5c7e40dfb5d040a465dd5e7af47dab1f6face6a242bfa33c6c3e60996ca72` |
| `vmlinux_ZZHL_with_BTF.elf` | `1b9cb9dbce2bcf6b918891df986ef6f903a6f0485c80def08519c0bcdbab7bdf` |

`zzhl-root-snapshot-8ed17a84/` preserva uma coleta root anterior usada como
referência forense. O BTF e o config dessa coleta têm os mesmos hashes dos
arquivos do firmware. Os endereços runtime registrados no snapshot pertencem
somente ao boot `8ed17a84-b88f-4d2e-86b3-64a1eff7b380`; o port não os usa.

## Derivação do target

`src/target.h` centraliza a identidade, a base estática, os símbolos, o ABI,
a geometria dos objetos falsos e as calibrações da rota. Os demais arquivos
usam somente nomes `TARGET_*`. `tools/verify-zzhl-target.py` compara o header
com o ELF e o BTF reais.
O verificador cobre:

- 22 símbolos ELF, incluindo ashmem, ConfigFS, pipe, workqueue, SELinux,
  `memstart_addr` e `kimage_voffset`;
- 33 offsets de campos BTF em `page`, `kmem_cache`, `task_struct`,
  `files_struct`, `fdtable`, `file`, `pipe_inode_info`, `pipe_buffer`, TTY e
  `work_struct`;
- 5 tamanhos BTF: `page`, `kmem_cache`, `task_struct`, `files_struct` e
  `tty_operations`;
- a relação `worker_thread + 0x78` usada pelo chamador da workqueue;
- o stride de 40 bytes de `pipe_buffer`.

O resultado atual é:

```text
PASS 22 ELF symbols, 33 BTF fields, 5 BTF sizes
```

## Implementação

O fluxo do port é dividido nos seguintes módulos:

| Módulo | Responsabilidade |
|---|---|
| `00_orchestrator.c` | identidade, tentativas, transições e restauração física de FOPS |
| `00_cpu_discovery.c` | seleção e revalidação da CPU rápida |
| `01_kernel_base_tracefs.c` | base KASLR por tracefs em cada boot |
| `02_slab_cache_probe.c` | geometria e ocupação dos caches SLUB |
| `03_mm_address_sidechannel/` | endereço de `mm_struct` por temporização de futex |
| `04_fake_kernel_objects.c` | layout do objeto `file_operations` falso |
| `05_mm_slab_grooming.c` | reclaim order-3 e instalação da tabela falsa |
| `06_signal_frame_payload.c` | frame FPSIMD usado pelo trigger |
| `07_futex_pi_trigger.c` | callback da rota futex PI |
| `08_ashmem_configfs_rw.c` | AAR/AAW inicial por ashmem e ConfigFS |
| `09_pipe_buffer_rw.c` | backend físico por `pipe_buffer` |
| `10_workqueue_umh_root.c` | PTY privado, workqueue e helper temporário |

O build produz `app_main`, `payload.so`, `mm-exec-factory` e
`stability-launcher-zzhl`. O launcher só entrega o payload depois de validar
temperatura, memória, pressão, tarefas executáveis, uptime, slabs e capacidade
de pipes.

## Correção crítica da restauração de FOPS

As duas primeiras execuções que alcançaram AAR/AAW reiniciaram por watchdog ao
tentar restaurar `ashmem_misc.fops`. A primeira escrevia o símbolo global pela
primitiva ConfigFS; a segunda tentou uma nova passagem pelo futex.

A correção final mantém o descritor ashmem já aberto com a tabela falsa,
instala primeiro o backend físico de pipes e só então restaura o símbolo
global. O endereço é convertido para o alias do linear map com os valores
runtime do próprio kernel:

```text
physical = kernel_virtual - kimage_voffset
linear   = 0xffffff8000000000 + (physical - memstart_addr)
```

O alias é limitado ao intervalo declarado no target, escrito pelo pipe R/W e
relido antes do estágio de root. A execução final registrou, nesta ordem:

```text
pipe-install-start
pipe-install-ready
fops-restore-pipe-start
fops-restore-confirmed
root-umh-start
umh-root-socket-ok
root-umh-ready
```

## Runner e política de execução

`tools/run-zzhl-device.sh` implementa duas modalidades:

```sh
tools/run-zzhl-device.sh --serial RXCX602E20X --slide-only
tools/run-zzhl-device.sh --serial RXCX602E20X --execute
```

O runner:

1. faz preflight e exige estado limpo;
2. valida a identidade exata do target;
3. recusa holder, launcher ou socket anterior;
4. envia payload, helper, factory, launcher e loader, conferindo SHA-256;
5. executa pelo launcher de estabilidade;
6. registra boot anterior e posterior, retorno ADB, log, trace e prova de root;
7. nunca repete automaticamente depois de uma mutação;
8. carrega o KernelSU Next ZZHL validado e exige a confirmação do controle;
9. aceita `--no-kernelsu` para diagnóstico somente com root temporário.

## Campanha de validação

Todos os caminhos abaixo são relativos a `evidence/zzhl-fresh/`. O diretório
de evidências é local e ignorado pelo Git por conter coletas grandes.

| UTC | Boot antes | Boot depois | Resultado |
|---|---|---|---|
| `20260927T110431Z-slide` | `94a0b622-1ece-4f1f-a60a-835abe767fb3` | igual | `SLIDE_ONLY` passou; base `0xffffffc008038000`, sem mutação |
| `20260927T110645Z-execute` | `523c418e-5438-42b5-a11d-260e5a3957ce` | `51f88578-a55a-4bbb-9762-5bec755f9ab6` | AAR/AAW passou; watchdog na restauração direta de FOPS |
| `20260927T111224Z-execute` | `51f88578-a55a-4bbb-9762-5bec755f9ab6` | `f5a1f43f-c866-4a30-9a39-60080785439b` | AAR/AAW passou; watchdog em `fops-restore-futex-start` |
| `20260927T111614Z-execute` | `f5a1f43f-c866-4a30-9a39-60080785439b` | igual | landing não utilizável; `ENOTTY`, holder preservado e nenhuma repetição |
| `20260927T111906Z-execute` | `c9356db4-04e7-4175-b1e1-a903fb7b5d02` | igual | sucesso completo do root temporário na primeira tentativa |
| `20260927T114521Z-execute` | `b2e9b62d-4082-43e5-b03d-582c78b726bd` | igual | root e KernelSU Next v3.4.0 final verificados na primeira tentativa |
| `20260927T125205Z-app` | `6a6be15d-44c6-4b14-b722-dd94997649b7` | igual | encoder ConfigFS corrigido; root e KernelSU verificados na primeira tentativa; pstore vazio |

Após a terceira execução, as coletas foram feitas com o holder preservado. O
reboot seguinte foi iniciado manualmente pelo operador e produziu o boot limpo
`c9356db4-04e7-4175-b1e1-a903fb7b5d02`, usado na execução final.

As falhas com reboot têm `post-reboot-forensics/`; a falha sem reboot tem
`postfailure-forensics/`. Nenhuma falha foi seguida por uma nova tentativa no
mesmo boot mutado.

## Prova final

Na execução `20260927T111906Z-execute`:

- o launcher aprovou a estabilidade com 41,8 °C e 6465 MB disponíveis;
- tracefs encontrou base `0xffffffc0080f0000`, slide `0xf0000`;
- o reclaim encontrou `mm=0xffffff8921aeb000` e base alinhada
  `0xffffff8921ae8000`;
- o AAR/AAW confirmou a corrupção controlada;
- o pipe R/W ficou pronto na tentativa 1/12 em 13 ms;
- a restauração física de `ashmem_misc.fops` foi relida e confirmada;
- o UMH retornou `complete=1 socket=1 restore=1`;
- a tentativa terminou com retorno ADB 0;
- o boot ID permaneceu inalterado.

A prova gravada e repetida ao vivo foi:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

Esse estado permissivo é intermediário e pertence ao bootstrap temporário. Na
execução final `20260927T114521Z-execute`, o helper carregou o módulo adaptado,
restaurou enforcing e confirmou o controle antes de o runner aprovar:

```text
KernelSU control verified version=33295 flags=0x5 uapi=4 features=0x2714
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
Enforcing
```

Após a correção do encoder ConfigFS, a execução Android
`9075374d-a736-4da8-b0cd-17ab4bc3b739` confirmou novamente o fluxo completo no
boot `6a6be15d-44c6-4b14-b722-dd94997649b7`. O KASLR foi
`0xffffffc008190000`, o reclaim produziu `payload_base=0xffffff8913ab8000`, o
pipe ficou pronto em 12 ms, o UMH retornou `complete=1 socket=1 restore=1` e o
root foi adquirido em 35,169 s. O controle KernelSU retornou os mesmos valores,
SELinux terminou enforcing e `/sys/fs/pstore` permaneceu vazio.

## Artefatos da execução aprovada

| Artefato | SHA-256 |
|---|---|
| `build/payload.so` | `9c412f5af77611f61165f82f02e0576e410b8a1fb556748ff6ddf3cb44364997` |
| helper temporário | `0e29b9706dac9c4ecef30772776e5647fc92014833417ed5f50cee01d990464a` |
| `build/mm-exec-factory` | `3422d63142db11de2968febed36bd47d1fb22f232e40875df8c8fb0cf5b90851` |
| `build/stability-launcher-zzhl` | `77e65c61795534dbbefd62198e20a06cdd4fce56d56570f726cb1dffd80ee0c6` |
| `ksud-next-v3.4.0` | `971f173977dec6d944f13be16343860218dce82d8431f45300de626fabbfab28` |

Os hashes calculados depois do `adb push` foram idênticos aos arquivos host.

## Perfil Android

O app 0.6.0-beta-2 contém o perfil exato `dm3q-S918BXXUAZZHL-ksunext`. Ele seleciona
modelo, build display, fingerprint, kernel release e `uname -v` antes de
expor o fluxo. Os assets são sincronizados e auditados com:

```sh
tools/zzhl-app-bundle.py sync
tools/zzhl-app-bundle.py verify
```

O APK `app-debug.apk`, SHA-256
`cd2b2391da8ef681b22a6c05ed64ae1c69986c2c4301fd89c315480059eedde4`,
passou nos testes. O verificador confirmou dentro do APK os cinco binários da
tabela acima, seus tamanhos e o perfil. O `apkanalyzer` confirmou
`versionCode=37` e `versionName=0.6.0-beta-2`.

## Verificações locais

O estado aprovado passa por:

```sh
python3 tools/verify-zzhl-target.py
python3 tools/audit-profile.py --profile tools/profiles/zzhl.json
python3 tools/check-repo.py
make -B all so tests test-aar-read-plan test-fops-layout test-compact-log
make -C ../helper -B
```

Também passam sintaxe Bash, compilação dos scripts Python, links Markdown,
padrões de segredo, invariantes do reclaim, plano de leitura AAR, layout FOPS e
compact logging. `shellcheck` não estava instalado e foi marcado como `SKIP`.

## Limites atuais

- O módulo KernelSU é carregado em runtime e precisa ser carregado novamente
  após um reboot.
- A campanha final passou em 10/10 reboots limpos com o módulo KernelSU Next
  final, root `u:r:ksu:s0`, SELinux `Enforcing` e nenhum reboot inesperado.
- As evidências brutas ficam fora do Git. Os caminhos, hashes, checkpoints e
  resultados necessários para auditar a campanha estão preservados aqui.
- Toda nova execução completa requer boot limpo. Depois de qualquer marcador
  de mutação, falha ou holder, coletar primeiro e reiniciar antes de testar.
