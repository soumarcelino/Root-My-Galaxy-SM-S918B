# Runbook de build e validação

## Escopo

Procedimento para o aparelho de laboratório autorizado
`RXCX602E20X`, firmware `S918BXXSAFZH3`. Executar no máximo uma tentativa
completa por boot. Para aceitação final, repetir em dois reboots limpos.

Os exemplos usam `rtk` conforme o ambiente local.

## 1. Confirmar ADB primeiro

```sh
rtk adb devices -l
rtk adb -s RXCX602E20X get-state
```

Não prosseguir com serial implícito quando houver mais de um device.

## 2. Identificar alvo e boot

```sh
rtk adb -s RXCX602E20X shell 'getprop ro.product.model; getprop ro.product.device; getprop ro.build.version.incremental'
rtk adb -s RXCX602E20X shell 'cat /proc/sys/kernel/random/boot_id; cat /proc/version; cat /proc/uptime'
```

Esperado: `SM-S918B`, `dm3q`, `S918BXXSAFZH3`.

## 3. Verificar boot completo e estado limpo

```sh
rtk adb -s RXCX602E20X shell 'getprop sys.boot_completed; if [ -x /system/bin/su ]; then echo SU_PRESENT; else echo SU_ABSENT; fi'
```

Se `su` já estiver presente, esse boot serve para diagnóstico, não para prova
limpa.

## 4. Build

```sh
cd /home/matias/Projects/Root-My-Galaxy-SM-S918B/targets/zzhl-WIP/open-payload-engine
rtk make -B -j2 all so
rtk sha256sum build/oss_clone_payload.so
```

Saídas esperadas:

```text
build/app_main
build/oss_clone_payload.so
```

O build validado usa NDK em
`/home/matias/Android/Sdk/ndk/28.2.13676358` e API 35, conforme fallback do
Makefile.

## 5. Validar runner

```sh
cd /home/matias/Projects/ksu-payload-functional
rtk bash -n simple-root
rtk ./simple-root --help
```

Assets obrigatórios:

```text
assets/ksu-helper
assets/ksu-payload
assets/ksud-f731u-kdp
```

Antes do teste, substituir `assets/ksu-payload` pelo build aberto e comparar o
hash. Preserve os demais assets compatíveis usados pelo fluxo funcional.

## 6. Reboot limpo

```sh
rtk adb -s RXCX602E20X reboot
rtk adb -s RXCX602E20X wait-for-device
```

Aguardar `sys.boot_completed=1`, registrar novo boot ID, confirmar build e
`SU_ABSENT`. Não contar reboot se o ID não mudou.

## 7. Executar uma vez

Uso interativo:

```sh
cd /home/matias/Projects/ksu-payload-functional
rtk ./simple-root RXCX602E20X
```

Captura não interativa:

```sh
rtk bash -c 'set -o pipefail; timeout 600 ./simple-root RXCX602E20X </dev/null 2>&1 | tee /tmp/oss-validation.log'
```

O payload pode aguardar uma janela quieta do allocator com base no uptime.
Essa espera é esperada; não iniciar outra instância.

O runner define `EXPLOIT_ATTEMPT_TIMEOUT_SEC=180`. O backend pipe alerta após
20 s sem destruir um holder ativo e limita retries já concluídos a 120 s; o
timeout externo é o último guard rail do processo inteiro.

O orquestrador interrompe a campanha após falha do primeiro boot ou erro ADB;
não inicia automaticamente outro payload depois de panic/reboot inesperado.

## 8. Critérios durante execução

Interromper classificação como sucesso se faltar qualquer marca:

```text
[aar_aaw] verify ok
[immediate] restore ... ok=1
[pipe_rw] ready
[root_umh] result wake=1 complete=1 socket=1
stage=temporary-root-ready
KernelSU control verified
[+] Root confirmado
```

## 9. Prova externa

```sh
rtk adb -s RXCX602E20X shell 'cat /proc/sys/kernel/random/boot_id; /system/bin/su -c id; cat /proc/uptime'
```

Exigir UID 0 e mesmo boot ID. O contexto esperado é `u:r:ksu:s0`.

## 10. Segundo reboot

Após registrar completamente o primeiro resultado:

1. reboot;
2. novo boot ID;
3. `SU_ABSENT`;
4. mesmo hash do payload;
5. mesma execução;
6. mesma prova final.

Sucesso final = 2/2. Um resultado isolado permanece provisório.

## Uso de root para diagnóstico

Quando root funcional existir antes do reboot, ele pode coletar:

```sh
rtk adb -s RXCX602E20X shell su -c 'cat /proc/kallsyms'
rtk adb -s RXCX602E20X shell su -c 'cat /proc/last_kmsg'
rtk adb -s RXCX602E20X shell su -c 'cat /proc/slabinfo'
```

Salvar dados antes do reboot seguinte. Não confundir uma execução iniciada com
root diagnóstico com prova de escalada desde shell.

## Se ADB cair

1. não executar novamente imediatamente;
2. aguardar reconexão;
3. comparar boot ID e uptime;
4. se reiniciou, obter `/proc/last_kmsg` antes de outro teste;
5. salvar log completo, não apenas últimos 128 KiB;
6. classificar como panic/reboot até prova contrária.

## Encerramento

Registrar:

- resultado por estágio;
- hash;
- boot IDs;
- logs;
- qualquer holder residual;
- se o runner abriu shell ou recebeu EOF;
- forense de panic, se aplicável.

Não fazer cleanup destrutivo de evidência antes da coleta.
