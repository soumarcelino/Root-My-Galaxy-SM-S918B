# Estado do port DYI3

Alvo: **SM-S911U1 / dm1q / S911U1UES6DYI3** (One UI 7, kernel 5.15.153,
`5.15.153-android13-8-30958972-abS911U1UES6DYI3`).

Port feito a partir do engine ZZHL (`../zzhl/`), com o contrato derivado do
próprio `boot.img` do aparelho — não copiado de outro alvo. `src/target.h` é
**gerado**; não edite à mão (veja `port-dm1q/derive_target.py` no workspace do
port, ou o README do engine).

## Contrato

- 112 valores derivados, 155 herdados, 29 alterados; `verify_contract` passa.
- 22 símbolos conferidos contra o ELF DYI3, 61 campos e 8 tamanhos contra o BTF
  do aparelho, mais as relações derivadas.
- `firmware/` guarda apenas o necessário para re-derivar e auditar:
  `config_DYI3` (IKCONFIG extraído do boot), `kallsyms_DYI3.kallsyms`,
  `vmlinux_DYI3.btf` e `kcrctab_DYI3.json` (CRCs dos símbolos exportados).
  O `vmlinux` completo não é versionado; a tabela de CRCs o substitui na
  auditoria do módulo.

## Correções específicas deste alvo

Ambas foram validadas no aparelho e são a diferença entre ~40% e 100% de
sucesso na rota determinística:

1. **Planos de escrita com posição livre** (`08_ashmem_configfs_rw.c`).
   `ki_pos` é um parâmetro livre — o kernel escreve em `bin_buffer + ki_pos` —
   então o plano procura um deslocamento que sobreviva ao replay do `strscpy`
   em vez de rejeitar o endereço. Sem isso, todo slot de `struct page` abaixo de
   2 GB era rejeitado e a rota determinística falhava 12/12 tentativas.
   Cobertura verificada em host: 0 não-codificáveis em 3.343.676 endereços.
2. **Índice da linha de kmalloc derivado do BTF** (`tools/bope/contract.py`).
   `kmalloc_caches` é `[NR_KMALLOC_TYPES][14]`; a linha do cgroup vem do enum
   BTF `kmalloc_cache_type`. O valor herdado do doador (`33`) apontava para a
   linha *reclaim* em vez da cgroup (`25`), então o gate de cache dos pipes
   nunca casava e a rota legada falhava com `cache-select` sem diagnóstico.

## Resultado

Boot limpo, rota determinística na **tentativa 1/12**, 0 skips de guarda,
root em 2–3 s. KernelSU Next v3.4.0 carrega via late-load e `su` responde em
`u:r:ksu:s0`; veja `kernelsu-next/` e `compatibility.json` para a auditoria do
módulo (CRC + layout contra o BTF do aparelho + relocação de `this_module`).
