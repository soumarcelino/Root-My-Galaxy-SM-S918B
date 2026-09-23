# ZZHL firmware dossier

Target único: Samsung SM-S918B (`dm3q`), firmware `S918BXXUAZZHL`.

## Identidade

| Campo | Valor |
|---|---|
| Fingerprint | `samsung/dm3qxxx/dm3q:17/CP2A.260605.016/S918BXXUAZZHL:user/release-keys` |
| Kernel | `5.15.197-android13-8-34343818-abS918BXXUAZZHL` |
| Dispositivo | `RXCX602E20X` |
| Base estática | `0xffffffc008000000` |
| Boot analisado | `6584b354-980b-4460-813a-a9255495a906` |
| Slide deste boot | `0xe8000` |

## Fontes e artefatos

| Item | Local |
|---|---|
| Código aberto portado | `/home/matias/Projects/Root-My-Galaxy-SM-S918B/zzhl-open-payload-engine` |
| Fonte anterior AFZH3 | `/home/matias/Projects/Root-My-Galaxy-SM-S918B/afzh3-open-payload-engine` |
| Kernel ELF | `/home/matias/Projects/Root-My-Galaxy-SM-S918B/ZZHL/vmlinux_ZZHL.elf` |
| BTF | `/home/matias/Projects/Root-My-Galaxy-SM-S918B/ZZHL/vmlinux_ZZHL.btf` |
| Firmware extraído | `/home/matias/Projects/build-do-firmware` |
| APK fechado extraído | `/home/matias/Downloads/DiamondFox-beta-1.02.100-extracted` |
| APK decompilado | `/home/matias/Downloads/DiamondFox-beta-1.02.100-decompiled` |

## Evidência coletada

| Evidência | Conteúdo |
|---|---|
| `evidence/zzhl-validation/runtime-kallsyms-unmasked-6584b354.txt` | Símbolos runtime completos do boot analisado |
| `evidence/zzhl-validation/btf-layouts-6584b354/` | 50 layouts ABI: VFS, pipe, task, cred, futex, SLAB, workqueue, SELinux, configfs, binder |
| `evidence/zzhl-validation/future-forensics-6584b354/` | dmesg, pstore, logcat, trace, slabinfo, kallsyms, DropBox, Samsung last_kmsg e manifest hashado |
| `evidence/zzhl-validation/future-reference-6584b354.md` | Tabela de offsets, hashes, ABI e limite conhecido |
| `/sdcard/Download/rmg-zzhl-forensics-6584b354` | Cópia de recuperação no device |

`evidence/` está no `.gitignore`; preservar antes de limpar o workspace.

## Estado do port

`src/target_zzhl.h` contém os offsets ZZHL auditados. `tools/check-repo.py`
valida cada literal e o perfil `tools/profiles/zzhl.json`.

Os offsets não provam a rota. O clone usa abertura direta de ashmem; o payload
fechado bem-sucedido usa preflight dentry/fops. A falha anterior
`try_module_get(x0=0x1270)` é owner inválido na tabela `file_operations`, não
um endereço de kernel a substituir.

## Procedimento seguro

1. Primeiro `adb -s RXCX602E20X`; confirmar fingerprint, kernel e boot ID.
2. Não repetir payload no mesmo boot após marcador de mutação/panic. Coletar
   logs; reboot; confirmar boot ID novo.
3. Usar `tools/preflight-device.sh`, `tools/run-zzhl-device.sh` e launcher de
   estabilidade. Logs live devem ficar em Kitty.
4. Antes de executar, comparar SHA do payload aberto, bundle runtime e asset
   APK. Build local não é prova de execução.
5. Só considerar sucesso com `uid=0(root)`, prova KernelSU e boot íntegro.

## Revalidação em firmware/boot novo

- Extrair BTF e kallsyms com root; nunca reutilizar slide.
- Recalcular offsets a partir de `_text`/base estática.
- Conferir campos BTF usados: `file_operations`, `miscdevice`, `file`,
  `pipe_inode_info`, `pipe_buffer`, `page`, `task_struct`, `files_struct`,
  `fdtable`, `rt_mutex_waiter`, `selinux_state`.
- Comparar ELF/raw e símbolos contra `src/target_zzhl.h` antes de editar.
