# Dossiê do firmware ZZHL

Target único: Samsung SM-S918B (`dm3q`), firmware `S918BXXUAZZHL`.

## Identidade

| Campo | Valor |
|---|---|
| Fingerprint | `samsung/dm3qxxx/dm3q:17/CP2A.260605.016/S918BXXUAZZHL:user/release-keys` |
| Kernel | `5.15.197-android13-8-34343818-abS918BXXUAZZHL` |
| Base estática | `0xffffffc008000000` |
| Dispositivo validado | `RXCX602E20X` |
| Boot da prova final | `c9356db4-04e7-4175-b1e1-a903fb7b5d02` |
| Slide da prova final | `0xf0000` |

## Fontes

| Item | Local |
|---|---|
| Port novo | `targets/zzhl-WIP/open-payload-engine` |
| Engine-base | `targets/afzh3/open-payload-engine` |
| Firmware principal | `targets/zzhl-WIP/firmware` |
| Cópia auxiliar conferida | `/home/matias/Downloads/ZZHL` |
| Kernel ELF | `firmware/vmlinux_ZZHL.elf` |
| BTF | `firmware/vmlinux_ZZHL.btf` |
| Kallsyms | `firmware/kallsyms_ZZHL.kallsyms` |

Os arquivos de firmware comuns às duas origens têm SHA-256 idêntico. A tabela
completa de hashes está em
[17-PORT-ZZHL-FRESH-E-CAMPANHA.md](17-PORT-ZZHL-FRESH-E-CAMPANHA.md).

## Verificação do target

`src/target_zzhl.h` é a única fonte de identidade, símbolos e ABI ZZHL usada
pelo payload. O comando:

```sh
python3 tools/verify-zzhl-target.py
```

confere 22 símbolos no ELF, 33 campos e 5 tamanhos no BTF. A auditoria do
perfil declarativo é executada por:

```sh
python3 tools/audit-profile.py --profile tools/profiles/zzhl.json
```

O runtime soma somente o slide descoberto no boot atual. Nenhum endereço
runtime coletado em boot anterior é persistido no target.

## Estado comprovado

Em 2026-09-27 o port novo alcançou root temporário no boot
`c9356db4-04e7-4175-b1e1-a903fb7b5d02`. O aparelho confirmou:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

O mesmo boot permaneceu ativo após KASLR, reclaim, AAR/AAW, instalação do pipe
R/W, restauração confirmada de `ashmem_misc.fops` e UMH. A campanha completa,
incluindo duas falhas por watchdog e a correção física final, está no relatório
do port.

KernelSU não foi validado: nenhum loader exato para ZZHL está disponível. O
runner não aceita substituição automática por artefato AFZH3.

## Procedimento operacional

1. Usar ADB e confirmar fingerprint, kernel, boot completo e boot ID.
2. Executar primeiro `--slide-only` ao mudar target, build ou descoberta KASLR.
3. Usar `tools/run-zzhl-device.sh` para staging, hashes e launcher.
4. Depois de qualquer mutação ou falha, coletar os dados e reiniciar antes de
   outra tentativa.
5. Só declarar root temporário quando o helper retornar `uid=0` e o boot ID
   permanecer igual.
6. Só declarar KernelSU quando um loader exato for fornecido por `--ksud` e o
   controle for verificado explicitamente.

## Revalidação após mudança

- Recalcular símbolos do ELF com base estática `0xffffffc008000000`.
- Reexecutar o verificador BTF/ELF e a auditoria do perfil.
- Recompilar payload, factory, launcher, helper e testes.
- Registrar os hashes host e device depois do `adb push`.
- Fazer um boot limpo para cada tentativa completa.
- Preservar logs e forense antes de qualquer reboot após falha.
