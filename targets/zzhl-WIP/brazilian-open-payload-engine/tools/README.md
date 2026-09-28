# Ferramentas do port ZZHL

- `verify-zzhl-target.py`: compara constantes C com o ELF e o BTF ZZHL.
- `audit-profile.py`: audita o perfil declarativo `profiles/zzhl.json`.
- `check-repo.py`: executa as verificações estáticas do repositório.
- `preflight-device.sh`: coleta identidade e exige boot limpo.
- `run-zzhl-device.sh`: faz staging, usa o launcher de estabilidade e registra
  a execução.
- `collect-forensics.sh`: coleta evidência somente-leitura após falha ou reboot.
- `analyze-run-log.py`: resume os estágios do log do payload.
- `check-target-boundary.py`: rejeita constantes e literais ZZHL fora de
  `src/target.h`.

`run-zzhl-device.sh` exige a identidade exata declarada em
`src/target.h`, recusa socket ou holder de execução anterior, confere os
hashes depois do `adb push` e salva preflight, boot IDs, retorno, log, trace e
prova de root em `evidence/zzhl-fresh/<UTC>-<modo>/`.

Depois de qualquer marcador de mutação, uma falha é terminal para o boot
atual. Colete a forense e reinicie; não execute novamente no mesmo boot.

Use primeiro:

```sh
tools/run-zzhl-device.sh --serial RXCX602E20X --slide-only
```

Execução completa, somente em boot limpo:

```sh
tools/run-zzhl-device.sh --serial RXCX602E20X --execute
```

O KernelSU Next ZZHL é carregado automaticamente. Um loader alternativo pode
ser informado explicitamente:

```sh
tools/run-zzhl-device.sh --serial RXCX602E20X --execute --ksud ARQUIVO
```

Use `--no-kernelsu` apenas para validar o root temporário. Não há fallback
automático para artefatos de outro firmware.
