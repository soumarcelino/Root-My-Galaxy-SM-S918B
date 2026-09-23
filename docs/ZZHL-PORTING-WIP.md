# Porting ZZHL — registro completo

## Alvo

- Modelo: `SM-S918B`
- Dispositivo: `dm3q`
- Build: `CP2A.260605.016.S918BXXUAZZHL`
- Kernel: `5.15.197-android13-8-34343818-abS918BXXUAZZHL`
- Arquivos-base: `/home/matias/Projects/build-do-firmware/dump/`
- Clone de referência: `/home/matias/Projects/github/rmg-f731u-clean/`

## Fonte e extração

O `kernel.raw`, `kernel.elf`, `kernel.kallsyms` e o BTF foram extraídos do dump. O BTF está no offset `0x21f29dc`, com tamanho `6098779` bytes. A base simbólica usada pelo ELF é `0xffffffc008000000`.

O script `tools/derive_zzhl_target.py` reprocessa o dump, extrai o BTF, valida layouts com `pahole`, resolve símbolos e regenera o fingerprint P0. Resultado: 29 endereços derivados e 10 layouts BTF verificados.

Relatório completo: `ZZHL/derived-target.json`.

## Endereços principais derivados

| Símbolo | Endereço |
| --- | ---: |
| `prepare_kernel_cred` | `0xffffffc00811e694` |
| `commit_creds` | `0xffffffc0081203d0` |
| `override_creds` | `0xffffffc00811f4a8` |
| `init_task` | `0xffffffc00ac05380` |
| `root_task_group` | `0xffffffc00acb9ac0` |
| `anon_pipe_buf_ops` | `0xffffffc009e81e60` |
| `ashmem_fops` | `0xffffffc00a010238` |
| `kmalloc_caches` | `0xffffffc00a067330` |
| `system_unbound_wq` | `0xffffffc00aa90808` |
| `selinux_enforcing` | `0xffffffc00aa3c40c` |

Os layouts `task_struct`, `rt_mutex_waiter`, `mm_struct`, `file_operations`, `pipe_buffer`, `page`, `work_struct`, `pool_workqueue`, `worker_pool` e `configfs_buffer` foram conferidos no BTF.

## Payload fechado do rmg-f731u

O método documentado pelo clone usa o payload fechado F731U e altera 15 construções ARM64 relacionadas a três símbolos. O gerador reproduzível criado em `tools/port_zzhl.py`:

- lê os símbolos do `kernel.kallsyms`;
- valida os imediatos originais em todos os pontos;
- recalcula as formas relativa, virtual e alias;
- preserva opcode, registradores e bits de deslocamento;
- verifica novamente as 15 construções.

Resultado do payload ZZHL: 51 bytes modificados, tamanho 131072 bytes, SHA-256 `d8f843c3fa0aefd19d039d95efd2f4512e6a88cdba7398b5359a18114ba2d4af`.

No aparelho, esse payload falha deterministicamente em `stage=locating-kernel`; portanto ele não deve ser usado como payload final para este kernel 5.15.197.

## Payload aberto recompilado

O alvo nativo `dm3q-S918BXXUAZZHL` foi recompilado com Android NDK `28.2.13676358`. O endereço físico inicialmente usado (`0xa8000000`) foi corrigido para `0xa8068000` com base no log de uma execução real no mesmo ZZHL.

Artefatos:

- `build/dm3q-S918BXXUAZZHL/cve-2026-43499-app.so`
- `build/dm3q-S918BXXUAZZHL/cve-2026-43499-root`
- `build/dm3q-S918BXXUAZZHL/cve-2026-43499`

O resolvedor `tracefs-slide.so` encontrou slides diferentes por boot (`0x40000`, `0x138000`, `0x1c8000`, `0x1d0000`). Por isso o slide não pode ser fixado no binário.

## Testes ADB

O fluxo validado é:

1. enviar helper, payload e resolvedor tracefs;
2. executar `SLIDE_ONLY=1`;
3. extrair `p0_offset` do log;
4. executar o payload com `SLIDE_P0_OFFSET`;
5. validar `id` pelo helper.

Houve execuções que alcançaram `uid=0(root)` e `u:r:kernel:s0`. Outras falharam no reclaim com `exact=0`, ou reiniciaram durante `sigreturn phase1`. Essas falhas são probabilísticas; o payload aborta antes da escrita global quando o preflight não é exato.

O script operacional é `tools/root-adb-zzhl.sh`. Ele prepara os arquivos, resolve o slide, executa até três tentativas e valida o daemon root. O modo `--persist` mantém o processo no PC e refaz o fluxo após mudança de `boot_id`; não cria persistência permanente no firmware.

## APK

O APK foi compilado com `./gradlew test assembleDebug`; os testes unitários passaram. O APK debug foi instalado no `RXCX602E20X`. O manifesto ZZHL foi adicionado em `app/src/main/assets/targets-v3.json` e no perfil desktop.

## Limitações

- Não existe, no dump, uma árvore completa do kernel Samsung nem `Module.symvers` para recompilar o módulo KernelSU específico.
- O `ksud-f731u-kdp` disponível pertence ao kernel F731U 5.15.189 e não é comprovadamente compatível com ZZHL 5.15.197.
- Root temporário expira no reboot ou quando o daemon é encerrado.
- O sucesso do reclaim aberto continua probabilístico e requer recalcular o slide em cada boot.

## Comando operacional

```bash
/home/matias/Projects/Root-My-Galaxy-SM-S918B/tools/root-adb-zzhl.sh -c 'id'
```

O resultado esperado é `uid=0(root)`. Se o preflight retornar `exact=0`, reinicie o aparelho antes de uma nova tentativa.
