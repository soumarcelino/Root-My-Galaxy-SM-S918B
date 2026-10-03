# Validação no aparelho

## Aparelho

```text
model:       SM-X518U (Galaxy Tab S9 FE 5G)
device:      gts9fe          ro.product.name gts9fecs
SoC:         Exynos 1380 (s5e8835, ro.board.platform erd8835)
CPU:         4x Cortex-A55 (0xd05) @ 2.00 GHz + 4x Cortex-A78 (0xd41) @ 2.40 GHz
RAM:         5555728 kB (5.3 GiB)
firmware:    X518UVLSFEZG3   ro.boot.bootloader X518UVLSFEZG3
fingerprint: samsung/gts9fecs/gts9fe:16/BP4A.251205.006/X518UVLSFEZG3:user/release-keys
One UI:      8.5 (ro.build.version.oneui 80500, ro.build.version.sep 170500)
Android:     16 (API 36), arm64-v8a, 4 KiB pages
kernel:      5.15.189-android13-3-33478785
build:       #1 SMP PREEMPT Sat Jul 18 02:16:22 KST 2026
serial:      R52WA063BCP (ADB over Wi-Fi, adb tcpip 5555)
```

## Evidência de calibração lida do aparelho

```text
/sys/kernel/tracing/events/sched/sched_blocked_reason/id = 108 (0x6c)
/proc/slabinfo  mm_struct          objsize 1024  32/slab  8 pages
/proc/slabinfo  skbuff_head_cache  objsize  256  32/slab  2 pages
/sys/class/thermal/thermal_zone0/temp -> Permission denied (8 zonas existem)
```

## Artefatos validados

```text
payload.so        179528 B  sha256 652d13a2bcae1c148815cb261537f06460024dc252329b1cc6cb300e0eee87c7
stability-launcher 18800 B  sha256 8048723504b0051fd2c52fd60e8af56811d6e23b87edb253d202e636d6f4b0a7
helper             28408 B  sha256 1ed37192fbbf69f671cffbc833e193c2d528b4c45e658e75fc6937ebd9089002
mm-exec-factory     1816 B  sha256 3422d63142db11de2968febed36bd47d1fb22f232e40875df8c8fb0cf5b90851
contract            3592 B  sha256 75c2461dd4822e7c5951bd675fc083296d661acf2157384fb82fdcf3e32eee68
ksud            6268176 B  sha256 2554060226662c8381fcbce262f85abb8e3a878f1579056c0347522eba9193db
```

`payload.so` e `mm-exec-factory` são reprodutíveis: reconstruir a partir de
`targets/gts9fe-WIP/brazilian-open-payload-engine` produz os mesmos bytes.

## Resultado

Execução pelo app (`Install KernelSU Next`), boot limpo, SELinux `Enforcing`,
sem `su` pré-existente:

```text
[BOPE] [*] stage=locating-kernel
[kaslr] source=tracefs base=ffffffc008168000 slide=0000000000168000 p0_offset=00168000 votes=2
[BOPE] [*] stage=verifying-kernel-access
[aar_aaw] verify ok: corruption landed, R/W primitive live
[BOPE] [*] stage=starting-temporary-root
[root_umh] result complete=1 socket=1 restore=1
[BOPE] [+] stage=temporary-root-ready
[BOPE] [+] exploit completed attempt=2/3
BOPE :: Success
        Root achieved in 6 seconds
[app] bootstrap-root=ready
[app] kernelsu=staged
[App] KernelSU control verified version=33294 flags=0x5 uapi=4 features=0x2714
[app] kernelsu=verified
[app] install=complete
```

Módulo carregado:

```text
$ adb shell cat /proc/modules | grep kernelsu
kernelsu 303104 1 - Live 0x0000000000000000 (OE)
```

O gerenciador `com.rifsxd.ksunext` v3.4.0 reporta `Working <LKM> [Jailbreak mode]`.

## Taxa de sucesso e modo de falha

O payload é efetivamente uma execução por boot. A falha observada é
intermitente e acontece na ocupação do `skbuff_head_cache`: quando a página
reclamada não está 100% ocupada, o `file_operations` falso não sobrevive e a
leitura falha com `ENOTTY`:

```text
[aar_aaw] configure read(fd=3, addr=ffffffc00a667750, len=8) failed errno=25(Inappropriate ioctl for device)
[recovery] preserving reclaimed page for reboot target=ffffffc00a667750 replacement=ffffffc009c1be88
[BOPE] stage=kernel-mutation-pending state=2
[supervisor] kernel mutation reached; refusing unsafe retry before reboot
```

Como a mutação de kernel já ocorreu, o supervisor recusa repetir antes do
reboot. O app apresenta **"Restart required"** com botão de reboot; após
reiniciar, a execução seguinte conclui. Esse comportamento é o projetado, não
uma regressão.

## Observações específicas deste alvo

1. **Sensores térmicos bloqueados.** O domínio `temp` das
   `thermal_zone` não é legível pelo UID shell/app, então `read_max_temperature`
   sempre falha. O launcher trata isso como indisponível (`temp=-1.0C`) em vez
   de fatal; o teto térmico é uma guarda de segurança, não um requisito de
   correção.
2. **Orçamento de pipes por UID.** A capacidade de pipes é um recurso por UID
   (`fs.pipe-user-pages-soft`), não por processo. Os bancos do próprio engine já
   ocupam ~480 pipes, então a sonda usa 240 pipes para provar que um banco
   completo pode ser dimensionado.
3. **`worker_thread+0xbc`** é o chamador de `sched_blocked_reason` neste kernel
   (não o `+0x78` herdado); o deslocamento é derivado por disassembly em
   `tools/bope/contract.py`.
4. **Granularidade KASLR 0x8000**, medida em hardware em dois boots
   (`0x108000` e `0x18000`).

## Duas execuções independentes

| execução | boot | resultado | observação |
| --- | --- | --- | --- |
| 1 | limpo, uptime 583s | falhou no `verify` | `ENOTTY`, "Restart required" |
| 2 | limpo após reboot, uptime 122s | **sucesso em 6s** | `KernelSU control verified` |

A execução 1 não é retomável sem reboot por decisão do supervisor. A execução 2
passou pelo gate completo (`gate=3/3 phase=baseline`, `estabilidade
confirmada`) antes de carregar o payload.
