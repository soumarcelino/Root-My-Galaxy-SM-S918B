# ZZHL Open Payload Engine

Port novo para o Samsung SM-S918B (`dm3q`) no firmware
`S918BXXUAZZHL`, kernel
`5.15.197-android13-8-34343818-abS918BXXUAZZHL`.

O código parte do engine atual em `targets/afzh3/open-payload-engine`, como
solicitado. Os símbolos e layouts foram derivados novamente de
`../firmware/vmlinux_ZZHL.{elf,btf}` e ficam centralizados em
`src/target_zzhl.h`. O payload antigo em `../payload` não integra este port.

Validação estática:

```sh
python3 tools/verify-zzhl-target.py
python3 tools/audit-profile.py --profile tools/profiles/zzhl.json
python3 tools/check-repo.py
```

Build:

```sh
make -B all so tests test-aar-read-plan test-fops-layout test-compact-log
make -C ../helper -B
```

Execução controlada:

```sh
tools/run-zzhl-device.sh --serial RXCX602E20X --slide-only
tools/run-zzhl-device.sh --serial RXCX602E20X --execute
```

O runner confere a identidade exata do aparelho, exige boot limpo, passa pelo
launcher de estabilidade e salva toda a execução em `evidence/zzhl-fresh/`.

O root temporário foi validado no aparelho em 2026-09-27, com `uid=0`,
restauração confirmada de `ashmem_misc.fops` e boot preservado. Consulte
[`STATUS.md`](STATUS.md) para o estado resumido e
[`docs/17-PORT-ZZHL-FRESH-E-CAMPANHA.md`](docs/17-PORT-ZZHL-FRESH-E-CAMPANHA.md)
para a proveniência, implementação, campanha completa, hashes e limites.
KernelSU ainda requer um loader exato para ZZHL.
