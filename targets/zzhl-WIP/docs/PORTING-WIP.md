# Porting ZZHL — registro WIP

## Verificação atual — 2026-09-21

O perfil `dm3q-S918BXXUAZZHL-ksunext` já está no manifesto Android e no
catálogo desktop, separado dos perfis AFZG1 e AFZH3. A conexão ADB
`RXCX602E20X` confirmou modelo `SM-S918B`, device `dm3q`, build display,
fingerprint, kernel release e kernel version exatamente iguais aos valores do
perfil. O `kernel.raw` em `build-do-firmware/dump/` tem SHA-256
`4e4c47864ed87eed80df0a7b9bf39281936b77b6974bde8c8d9e94711db878e8`.
Uma nova derivação com `tools/derive_zzhl_target.py` reproduziu integralmente
`targets/zzhl-WIP/firmware/derived-target.json`: 29 endereços e 10 layouts BTF. A tabela P0 gerada
contém os mesmos valores do header existente; somente a formatação difere.

O APK inclui payload ZZHL SHA-256
`001a13abc84ae963b884eadcd3d919c22a030d3b5c512bdb8113f2ee4c39cf1d`
e helper SHA-256
`eb2678570acd4e207aaa7d6c6bf3f0eab1d42341af403698e3ed70aeb3e0f651`.
O `ksud-f731u-kdp` continua compartilhado e sua compatibilidade com ZZHL
continua sem prova. Esta auditoria confirma identidade e integridade local;
não constitui nova prova de root ou de estabilidade.

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

Relatório completo: `targets/zzhl-WIP/firmware/derived-target.json`.

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
- `targets/zzhl-WIP/helper/build/cve-2026-43499-root`
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

O APK foi compilado com `cd app && ./gradlew test assembleDebug`; os testes unitários passaram. O APK debug foi instalado no `RXCX602E20X`. O manifesto ZZHL foi adicionado em `app/src/main/assets/targets-v3.json` e no perfil desktop.

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
