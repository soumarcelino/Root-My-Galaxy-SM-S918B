# Documentação do port ZZHL

Este diretório acompanha o port novo do open payload engine para
`SM-S918B/dm3q`, firmware `S918BXXUAZZHL` e kernel
`5.15.197-android13-8-34343818-abS918BXXUAZZHL`.

## Estado comprovado

O root e o KernelSU Next foram validados no aparelho em 2026-09-27. A execução
aprovada confirmou KASLR, reclaim, AAR/AAW, pipe R/W, restauração de
`ashmem_misc.fops`, UMH, controle KernelSU, `uid=0` e SELinux enforcing,
mantendo o mesmo boot ID e o pstore vazio.

A campanha final da correção passou em **10/10 reboots limpos**, com o fluxo
completo, KernelSU Next funcional, root `u:r:ksu:s0` e SELinux `Enforcing` em
todas as execuções.

Leia primeiro:

1. [Incidente do encoder ConfigFS no ZZHL](18-INCIDENTE-STRSCPY-CONFIGFS.md):
   causa raiz, correção implementada e validação no aparelho.
2. [Port novo e campanha no aparelho](17-PORT-ZZHL-FRESH-E-CAMPANHA.md):
   proveniência, target, hashes, arquitetura, falhas, correção final, cinco
   execuções, prova de root e limites.
3. [Dossiê do firmware ZZHL](ZZHL-FIRMWARE.md): identidade, fontes, verificações
   ELF/BTF e política de revalidação.
4. [Estado atual](../STATUS.md): resumo operacional curto.
5. [Ferramentas](../tools/README.md): verificadores, preflight, runner e coleta.

## Referência arquitetural herdada

Os documentos 01 a 16 vieram do engine AFZH3 usado como base. Eles preservam
a investigação e as decisões arquiteturais que originaram os módulos atuais.
Medições, campanhas, hashes, endereços e afirmações de KernelSU que citam
`AFZH3` pertencem ao firmware-base e não constituem prova para ZZHL. Para
valores ou resultados ZZHL, use os documentos listados acima.

1. [Escopo e metodologia](01-ESCOPO-E-METODOLOGIA.md)
2. [Arquitetura e fluxo](02-ARQUITETURA-E-FLUXO.md)
3. [Causas raiz e correções](03-CAUSAS-RAIZ-E-CORRECOES.md)
4. [Backend físico por pipes](04-PIPE-PHYSRW.md)
5. [Root UMH e workqueue](05-ROOT-UMH-WORKQUEUE.md)
6. [Mapa de fidelidade](06-MAPA-FIDELIDADE.md)
7. [Validação no device da base](07-VALIDACAO-DEVICE.md)
8. [Runbook da base](08-RUNBOOK.md)
9. [Troubleshooting e forense](09-TROUBLESHOOTING-E-FORENSE.md)
10. [Limitações e manutenção](10-LIMITACOES-E-MANUTENCAO.md)
11. [Ferramentas e evidências](11-FERRAMENTAS-E-EVIDENCIAS.md)
12. [Roadmap histórico](12-PROXIMOS-PASSOS-E-ROADMAP.md)
13. [Resolução direta do pipe](13-RESOLUCAO-DIRETA-PIPE.md)
14. [Automação operacional AFZH3](14-AUTOMACAO-OPERACIONAL.md)
15. [Reclaim determinístico e CPU discovery](15-RECLAIM-DETERMINISTICO-E-CPU-DISCOVERY.md)
16. [AAR ConfigFS com comprimento exato](16-CONFIGFS-AAR-EXACT-LENGTH.md)

## Código principal

| Arquivo | Responsabilidade |
|---|---|
| `src/target_zzhl.h` | Identidade, símbolos e ABI do firmware ZZHL. |
| `src/00_orchestrator.c` | Supervisor, tentativas e restauração física de FOPS. |
| `src/00_cpu_discovery.c` | Seleção e revalidação de CPU. |
| `src/01_kernel_base_tracefs.c` | Descoberta da base KASLR por boot. |
| `src/02_slab_cache_probe.c` | Geometria e ocupação dos slabs. |
| `src/03_mm_address_sidechannel/` | Vazamento do endereço de `mm_struct`. |
| `src/04_fake_kernel_objects.c` | Layout do objeto FOPS falso. |
| `src/05_mm_slab_grooming.c` | Grooming e reclaim order-3. |
| `src/06_signal_frame_payload.c` | Payload do signal frame. |
| `src/07_futex_pi_trigger.c` | Trigger futex PI. |
| `src/08_ashmem_configfs_rw.c` | AAR/AAW inicial. |
| `src/09_pipe_buffer_rw.c` | Backend físico por pipes. |
| `src/10_workqueue_umh_root.c` | PTY, workqueue e helper temporário. |

`evidence/` permanece no `.gitignore` porque contém coletas grandes. O relatório
consolidado preserva os boot IDs, hashes, checkpoints e resultados auditáveis.
