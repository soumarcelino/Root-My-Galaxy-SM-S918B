# Contrato único do target e `src` genérico

## Objetivo

`src/target.h` é o contrato completo do payload com o firmware
`S918BXXUAZZHL`. Os módulos `.c` implementam a rota sem conhecer o nome ZZHL
nem repetir endereços, offsets de estruturas ou calibrações específicas. Para
portar o engine a outro firmware, o ponto inicial é substituir esse contrato e
validá-lo contra o ELF e o BTF correspondentes.

`src/target_zzhl.h` permanece somente como include de compatibilidade para
consumidores externos antigos. Nenhum módulo do payload inclui esse arquivo.

## Dados centralizados

O target contém:

- identidade exata do aparelho, build, fingerprint e kernel;
- base estática e offsets de símbolos, sempre relativos à imagem sem KASLR;
- mapa linear, `vmemmap`, tamanho de página e limites de slide;
- IDs e geometria usados na leitura do ring buffer do tracefs;
- offsets e tamanhos BTF de `page`, `kmem_cache`, `task_struct`, `files_struct`,
  `fdtable`, `file`, `pipe_inode_info`, `pipe_buffer`, `tty_struct`,
  `tty_operations` e `work_struct`;
- layout de `file_operations`, `rt_mutex_waiter`, árvore RB, task falsa,
  signal frame, PTY, workqueue e `subprocess_info`;
- posições de todos os objetos dentro da página order-3 recuperada;
- encoding ConfigFS/ashmem, incluindo os intervalos críticos e o comportamento
  word-at-a-time de `strscpy()`;
- perfil de reclaim, KernelSnitch, pipe R/W e tempos calibrados do futex v14.

Os consumidores usam nomes `TARGET_*`. Não existem aliases locais com valores
literais para esses dados.

## Fronteira automática

`tools/check-target-boundary.py` percorre `src/` e `tests/` e falha quando:

- um arquivo inclui o header de compatibilidade;
- aparece uma macro `ZZHL_*` fora do target;
- outro arquivo define uma macro `TARGET_*`;
- endereços e identidades conhecidos do firmware são duplicados.

`tools/check-repo.py` executa essa verificação junto com ELF/BTF, perfil,
invariantes de reclaim e validações do repositório.

## Regras para um novo port

1. Copiar o engine genérico e fornecer um novo `target.h`.
2. Derivar símbolos do ELF e layouts do BTF do firmware exato.
3. Recalibrar tracefs, reclaim, KernelSnitch, ConfigFS e futex quando o novo
   kernel divergir.
4. Executar `python3 tools/check-repo.py`, todos os testes host e o build
   Android antes de qualquer teste no aparelho.
5. Descobrir o slide novamente em cada boot; nunca colocar endereços runtime
   no target.

## Validação desta refatoração

Em 2026-09-27 passaram:

- build PIE e `payload.so` arm64 sem warnings;
- compilação de toda a suíte arm64;
- testes host de plano ConfigFS, layout FOPS e compact log;
- 22 símbolos ELF, 33 campos BTF e 5 tamanhos BTF;
- perfil declarativo, fronteira do target e invariantes do reclaim;
- testes unitários e build do APK, com todos os assets ZZHL conferidos.

Artefatos gerados no host:

| Artefato | SHA-256 |
|---|---|
| `build/app_main` | `b0d2a675194f9132404d9056d779725eff46f703092c2d7a6431c67747cddb7a` |
| `build/payload.so` | `9596a41a0d64de32ca4fdcfcb09fdb78aabeaf852e74440df6b7ff138d2c5ea6` |
| APK debug | `3f69935f95234e1bde0ac2bcd587bd690919e8a31719df54e26117174d8c8fe1` |

O aparelho foi consultado somente para confirmar identidade, kernel, boot ID e
root existentes. Esta refatoração não executou nem mutou o kernel do aparelho.
