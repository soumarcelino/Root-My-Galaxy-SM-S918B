# KernelSU Next v3.4.0 no SM-S918B AFZH3: reprodução completa

Registro do procedimento usado em 23/09/2026 para produzir o módulo Samsung
AFZH3, embuti-lo no `ksud` Android e integrá-lo ao `simple-root`. O usuário
confirmou que a execução com os novos arquivos funcionou. Este documento
separa essa confirmação dos testes estáticos e dos registros de falha que
estão disponíveis no repositório.

## 1. Resultado e arquivos finais

O alvo é o Galaxy S23 Ultra `SM-S918B`, codinome `dm3q`, firmware
`S918BXXSAFZH3`, Android 16, kernel em execução:

```text
5.15.189-android13-8-33413713-abS918BXXSAFZH3
```

O build reproduzível grava os arquivos aqui:

```text
kernelsu-next/out/kernelsu-next-afzh3-v3.4.0/
├── android13-5.15_kernelsu.ko
├── ksud-next-v3.4.0
└── SHA256SUMS
```

Hashes da execução documentada:

| Arquivo | SHA-256 |
| --- | --- |
| `android13-5.15_kernelsu.ko` | `dbb8fd27f1b4bd1253f745e84b08cc4efb4c17a78b204738d3acad92c07d5763` |
| `ksud-next-v3.4.0` | `ddb381156ab70de940c7f539ca74c3359770531b07b0c140e0b60f067600ce0c` |

`ksud-next-v3.4.0` é o executável que o helper inicia. Ele contém o `.ko`
acima como asset Rust incorporado **na compilação**. O `.ko` independente fica
disponível para inspeção e acompanha o bundle do `simple-root`; o late-load
usa a cópia embutida no `ksud`.

O nome `android13-5.15` é o KMI selecionado pelo loader. Ele **não** indica
que todo módulo desse KMI funcionará neste firmware. O `vermagic`, os símbolos,
os CRCs e as particularidades de KDP/RKP/DEFEX do kernel Samsung precisam
corresponder ao alvo.

## 2. Por que o artefato oficial não funcionou

O release oficial v3.4.0 contém `aarch64-android13-5.15_kernelsu.ko`, copiado
anteriormente para `kernelsu-next/`. Sua identidade local é:

```text
SHA-256:  c6814bb47ebb853f39aa77bd0579f86e9c2f4c180b1de9ab5dd01f1c7403b723
vermagic: 5.15.202-android13-5.15.202_r00-dirty SMP preempt mod_unload modversions aarch64
```

O aparelho usa `5.15.189-android13-8-33413713-abS918BXXSAFZH3`.
Além da diferença de `vermagic`, o KernelSU genérico não tinha o tratamento
necessário para credenciais protegidas pelo Samsung KDP neste kernel. Duas
execuções anteriores produziram `synchronous external abort` em
`ksu_mark_running_process_locked+0x284/0x3f8 [kernelsu]`. Os registros foram
preservados em:

```text
simple-root/evidence/20260923-ksu-crash/lastkmsg-KP.log.gz
simple-root/evidence/20260923-ksu-crash/lastkmsg-KP-earlier.log.gz
simple-root/evidence/20260923-ksu-crash/logcat.txt
simple-root/evidence/20260923-ksu-crash/device-state.txt
```

Uma mensagem de que o `.ko` carregou não bastava: o panic surgia depois,
durante o uso de credenciais. Por isso o módulo do release fica apenas como
referência histórica. O `simple-root.sh` exige o build AFZH3 desta pasta.

## 3. Entradas fixadas para reproduzir o build

| Entrada | Local e função |
| --- | --- |
| Código Samsung | `/home/matias/Projects/SM-S918B_16_Opensource/kernel_platform/common` |
| Imagem exata do kernel de `boot.img` | `/home/matias/Projects/SM-S918B_16_Opensource/device-afzh3/boot-unpacked/kernel` |
| SHA-256 dessa imagem | `1f20782f0c0c91e329958d4715364c8f1682a42dc79aa4dce42a80f4aa3ae1d7` |
| KernelSU Next | `/home/matias/Projects/github/KernelSU-Next-v3.4.0`, tag `v3.4.0`, commit `1a879d6a866f80b1fa1c1009a2ffa747873cbb5e` |
| Commit do port AFZH3 | `c300cb9d712126e8ac2a678014a1f2e8adbb9362`, diretamente sobre a base acima |
| Patch Samsung | `kernelsu-next/patches/KernelSU-Next-v3.4.0-samsung-afzh3-kdp-rkp-defex.patch` |
| SHA-256 do patch | `edbcc0edbc7a90211a479bc20a7801beff3223cbd4047a1b6a074178aa8b54fd` |
| Compilador do kernel | `/home/matias/Projects/github/android-clang-r450784e`, Android Clang 14.0.7, build 8508608 |
| Android NDK | `/home/matias/Android/Sdk/ndk/28.2.13676358` |
| Rust target | `aarch64-linux-android` |

Também são usados `git`, `make`, `bash`, `python3`, `rg`, `modinfo`,
`sha256sum`, `cargo`, `rustup` e ferramentas usuais do kernel/NDK. Os scripts
verificam as entradas que determinam a compatibilidade: hash da imagem de
boot, revisão do Clang, commit do KernelSU, `kernel.release`, presença de
`Module.symvers` e `vermagic` do módulo final.

A árvore Samsung veio do pacote open source do dispositivo. Ela não é um
checkout Git neste local; o hash da imagem de boot e a configuração extraída
do aparelho são as âncoras reproduzíveis deste caso. O material capturado do
AFZH3 está descrito no
[`device-afzh3/README.md`](../../SM-S918B_16_Opensource/device-afzh3/README.md)
do projeto de origem, fora deste repositório.

## 4. Identificar o aparelho e conferir a configuração

Antes de construir ou usar o módulo, a identidade do dispositivo foi lida
por ADB. Exemplo, com o serial desta bancada:

```sh
adb devices -l
adb -s RXCX602E20X shell getprop ro.build.fingerprint
adb -s RXCX602E20X shell uname -r
```

O fingerprint observado foi
`samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZH3:user/release-keys`.
O `uname -r` coincidiu com a string do início deste documento. Não use apenas
`SM-S918B` ou apenas `android13-5.15` como decisão de compatibilidade: outros
firmwares do mesmo modelo têm releases distintos.

A configuração foi extraída da `Image` contida em `boot.img` com o próprio
`scripts/extract-ikconfig` da árvore Samsung. Antes dos ajustes de build, o
resultado foi comparado byte a byte com `/proc/config.gz` coletado do aparelho:

```sh
OSS=/home/matias/Projects/SM-S918B_16_Opensource
RMG=/home/matias/Projects/Root-My-Galaxy-SM-S918B
cmp <("$OSS/kernel_platform/common/scripts/extract-ikconfig" \
       "$OSS/device-afzh3/boot-unpacked/kernel") \
    <(gzip -dc "$RMG/simple-root/evidence/20260923-ksu-crash/device-proc-config.gz")
```

O comando terminou sem diferenças. A configuração de saída depois recebe o
caminho absoluto da whitelist ABI e passa por `olddefconfig`; por isso a
comparação acima deve usar a configuração **extraída da imagem**, não a
`.config` já preparada no diretório `out/`.

A configuração real habilita `CONFIG_LTO_CLANG_FULL=y`,
`CONFIG_MODVERSIONS=y`, `CONFIG_KPROBES=y` e `CONFIG_KRETPROBES=y`. Preparar
`Module.symvers` com essa configuração é parte do build, não um detalhe
opcional.

## 5. Preparar um clone exato do KernelSU Next

O clone usado fica em `Projects/github`, separado deste repositório. Para
recriar do zero:

```sh
mkdir -p /home/matias/Projects/github
git clone https://github.com/KernelSU-Next/KernelSU-Next.git \
  /home/matias/Projects/github/KernelSU-Next-v3.4.0
git -C /home/matias/Projects/github/KernelSU-Next-v3.4.0 \
  checkout 1a879d6a866f80b1fa1c1009a2ffa747873cbb5e
git -C /home/matias/Projects/github/KernelSU-Next-v3.4.0 \
  describe --tags --exact-match HEAD
```

O último comando deve imprimir `v3.4.0`. O patch Samsung fica versionado em
`kernelsu-next/patches/`; não é necessário editar manualmente os 15 arquivos
do clone. `build-afzh3-v3.4.0.sh` aceita o commit original ou o commit do
patch diretamente sobre ele. Primeiro verifica se o patch já está aplicado
(`git apply --check --reverse`). No commit original, se necessário, executa
`git apply --check` e aplica o patch. Falhas nesse ponto pedem um clone limpo
no commit exato; o script não tenta resolver conflitos.

O patch tem 799 linhas adicionadas e 41 removidas. As mudanças centrais são:

| Área | Mudança necessária no AFZH3 |
| --- | --- |
| `kernel/Kbuild` e `Kconfig` | Incluem os arquivos de compatibilidade e as opções `CONFIG_KSU_SAMSUNG_KDP`, `RKP`, `DEFEX` e `NO_PATCH_TEXT`. |
| `compat/samsung_kdp.c` | Instala credenciais de forma compatível com `prepare_ro_creds()` e `kdp_assign_pgd()`, e libera referências protegidas com `kdp_usecount_dec_and_test()`. |
| `core/init.c`, `policy/app_profile.c`, `hook/tp_marker.c` | Inicializam KDP antes do uso, encaminham a elevação de privilégio pelo fluxo Samsung e substituem a liberação simples de credencial no ponto associado ao panic. |
| `compat/samsung_defex.c` | Sincroniza o estado de credenciais do DEFEX e limita o tratamento da verificação ao contexto KernelSU root. |
| `hook/arm64/patch_memory.c` e `syscall_hook.c` | Bloqueiam alteração direta de texto protegido e tornam a falha na instalação do dispatcher explícita. |
| `hook/syscall_hook_manager.c` | Quando o dispatcher não pode ser instalado por causa do RKP, usa `kretprobe` de `setresuid` e `kprobes` por endereço para `sucompat`. |
| `selinux/selinux.c` e arquivos auxiliares | Completam a integração com as credenciais e hooks Samsung. |

Essas opções ficam desativadas por padrão no Kconfig. O script de build as
ativa ao compilar **este** módulo. Não use o patch para supor que outro
firmware Samsung já foi validado.

## 6. Preparar o build completo do kernel AFZH3

Defina os caminhos da bancada uma vez. Os valores abaixo são os defaults dos
scripts, exceto `RMG`, usado apenas nos comandos deste guia:

```sh
export RMG=/home/matias/Projects/Root-My-Galaxy-SM-S918B
export SAMSUNG_SOURCE_ROOT=/home/matias/Projects/SM-S918B_16_Opensource
export SAMSUNG_KERNEL_SRC="$SAMSUNG_SOURCE_ROOT/kernel_platform/common"
export KSU_NEXT_SRC=/home/matias/Projects/github/KernelSU-Next-v3.4.0
export CLANG_ROOT=/home/matias/Projects/github/android-clang-r450784e
export ANDROID_NDK_HOME=/home/matias/Android/Sdk/ndk/28.2.13676358
export KERNEL_OUT="$RMG/kernelsu-next/out/afzh3-kernel"
```

Execute a preparação:

```sh
JOBS=4 "$RMG/kernelsu-next/prepare-afzh3-kernel-v3.4.0.sh"
```

O script executa, nessa ordem:

1. Confere `kernel_platform/common/Makefile`, `scripts/extract-ikconfig`,
   imagem de boot AFZH3 e seu SHA-256 fixado.
2. Confere que o Clang local é o build 8508608 da revisão r450784e.
3. Extrai a configuração da imagem de boot para
   `kernelsu-next/out/afzh3-kernel/.config`.
4. Processa as listas `android/abi_gki_aarch64*` citadas pelo
   `build.config.gki.aarch64` Samsung. Gera `abi_symbollist`, relatório e
   `abi_symbollist.raw`, usado em `CONFIG_UNUSED_KSYMS_WHITELIST`.
5. Executa `olddefconfig` com `ARCH=arm64`, LLVM/Clang Samsung,
   `GOOGLE_BRANCH=android13-5.15`, `KMI_GENERATION=8`,
   `LOCALVERSION=-33413713` e `BUILD_NUMBER=S918BXXSAFZH3`.
6. Compara o `kernelrelease` calculado com
   `5.15.189-android13-8-33413713-abS918BXXSAFZH3`.
7. Compila `vmlinux` e os módulos para gerar a árvore de saída completa e
   `Module.symvers`. Confere novamente `include/config/kernel.release`.

Esse estágio compila o kernel como **base para o módulo**. `vmlinux`,
`System.map`, cabeçalhos gerados e `Module.symvers` ficam em:

```text
kernelsu-next/out/afzh3-kernel/
```

Não é uma nova imagem `boot.img` nem uma etapa de flash. O `vmlinux` desta
execução ocupa cerca de 500 MB. O kernel usa LTO completo; o primeiro build
pode consumir bastante RAM e disco. Na preparação desta bancada houve uso
temporário de swap, removido depois. Se faltar memória, reduza `JOBS` e
repita a preparação antes de compilar o módulo.

## 7. Compilar o módulo Samsung e o `ksud` que o contém

Com `Module.symvers` e `kernel.release` prontos, execute:

```sh
JOBS=8 "$RMG/kernelsu-next/build-afzh3-v3.4.0.sh"
```

O script falha se o clone KernelSU não estiver no commit base fixado ou no
commit do patch diretamente sobre ele, se o `kernel.release` divergir, se
faltar `Module.symvers`, Clang ou NDK. Depois:

1. Confere/aplica o patch Samsung sobre o KernelSU Next v3.4.0.
2. Usa a árvore `afzh3-kernel/` para compilar `kernel/kernelsu.ko` como módulo
   externo com `CONFIG_KSU=m` e as quatro opções Samsung ativadas.
3. Usa `llvm-strip --strip-debug` e grava
   `out/kernelsu-next-afzh3-v3.4.0/android13-5.15_kernelsu.ko`.
4. Verifica que o `vermagic` começa exatamente com o `kernel.release` AFZH3.
5. Copia **esse** `.ko` para
   `$KSU_NEXT_SRC/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko`.
6. Compila o `ksud` com `cargo build --locked --release --target
   aarch64-linux-android`, usando o linker Android do NDK.
7. Remove símbolos de debug do executável, grava
   `out/kernelsu-next-afzh3-v3.4.0/ksud-next-v3.4.0` e escreve `SHA256SUMS`.

A ordem dos passos 5 e 6 é decisiva. `userspace/ksud/src/assets.rs` usa
`RustEmbed` sobre `bin/aarch64/`. `userspace/ksud/src/late_load.rs` seleciona
`{kmi}_kernelsu.ko` desses assets e chama `ksuinit::load_module`. Trocar o
arquivo `.ko` **depois** de compilar o `ksud` não altera o executável já
gerado: nesse caso é preciso repetir o build do `ksud`.

O comando de módulo usa `KBUILD_MODPOST_WARN=1`. Há símbolos internos que o
modpost trata como não exportados; o carregador de `ksud` prepara as
relocações usando símbolos do kernel em execução antes de `init_module`.
Esse modo de carregamento é parte da solução. Ele não torna irrelevante uma
falta de símbolo: nomes e CRCs foram verificados separadamente. Não substitua
o fluxo por um `insmod` comum nem use o `.ko` oficial no lugar do Samsung.

O log final da execução validada mostrou `KernelSU-Next version: 33294`, tag
`v3.4.0`, `vermagic` AFZH3 e os dois hashes da seção 1. Esses binários foram
compilados antes de transformar o patch em commit Git. Um rebuild a partir do
commit do patch incrementa o código de versão derivado do histórico Git;
seus hashes novos exigem validação própria no aparelho.

### O arquivo genérico de teste não é o resultado

`build-ksud-v3.4.0-release-ko.sh` é um caminho anterior de comparação. Ele
embute o `.ko` **oficial** copiado para `kernelsu-next/` e gera
`out/kernelsu-next-v3.4.0-release-ko/ksud-next-v3.4.0`. Esse executável não
tem o módulo AFZH3 corrigido e não é selecionado pelo `simple-root`.

## 8. Conferir o resultado local

Os comandos abaixo verificam o par de saída sem tocar no aparelho:

```sh
OUT="$RMG/kernelsu-next/out/kernelsu-next-afzh3-v3.4.0"
cd "$OUT"
sha256sum -c SHA256SUMS
modinfo -F vermagic android13-5.15_kernelsu.ko
modinfo -F name android13-5.15_kernelsu.ko
rg -a -q 'Samsung KDP task-scoped credential' android13-5.15_kernelsu.ko
cmp android13-5.15_kernelsu.ko \
  "$KSU_NEXT_SRC/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"
```

Resultados esperados nesta execução:

```text
android13-5.15_kernelsu.ko: OK
ksud-next-v3.4.0: OK
5.15.189-android13-8-33413713-abS918BXXSAFZH3 SMP preempt mod_unload modversions aarch64
kernelsu
```

`rg` deve retornar código zero para o marcador Samsung; `cmp` deve terminar
sem diferenças.
O arquivo `SHA256SUMS` é refeito a cada build. Para reproduzir **os mesmos
bytes** da seção 1, entradas, ferramentas e código precisam permanecer
iguais; um rebuild com outro ambiente pode gerar hashes novos mesmo mantendo
o nome do arquivo. Confira o `vermagic` e os novos hashes antes de distribuir.

Foi feita também uma verificação estática mais ampla nesta bancada:

- Na comparação de 431 módulos originais do firmware AFZH3 com o
  `vmlinux.symvers` reconstruído, 2.905 símbolos tiveram CRC correspondente,
  sem divergências.
- Os 203 símbolos indefinidos do `.ko` final tinham nomes presentes no
  `System.map` reconstruído.
- Os 139 CRCs expostos por `modprobe --dump-modversions` para o `.ko`
  coincidiam com `Module.symvers` do build.

Essas verificações reforçam a compatibilidade, mas não substituem um teste
no hardware. O `System.map` reconstruído confirma nomes; seus endereços não
são uma prova de que a imagem compilada é bit a bit igual ao kernel de fábrica.
O teste de runtime continua necessário.

## 9. Como o `simple-root` usa os arquivos

`../simple-root/simple-root.sh` define a saída Samsung AFZH3 como fonte
obrigatória. No início da execução ele:

1. Exige `.ko`, `ksud` e `SHA256SUMS` não vazios.
2. Verifica os hashes, o marcador Samsung KDP e o `vermagic` AFZH3.
3. Copia `ksud-next-v3.4.0` para `simple-root/assets/ksud-selected`, o nome
   esperado pelo helper.
4. Copia `android13-5.15_kernelsu.ko` para
   `simple-root/assets/android13-5.15_kernelsu.ko` como artefato separado.
5. Compila o launcher, a biblioteca do payload e o helper AFZH3, depois
   envia somente os cinco assets selecionados para
   `/data/local/tmp/simple-root/`. O antigo `ksud-f731u-kdp` que ainda pode
   existir em `assets/` não entra nessa lista.

Os hashes dos dois arquivos atualmente copiados em `simple-root/assets/`
coincidem com a seção 1. Para conferir novamente:

```sh
sha256sum "$RMG/simple-root/assets/ksud-selected" \
          "$RMG/simple-root/assets/android13-5.15_kernelsu.ko"
```

Depois que o payload confirma root temporário, o script copia
`ksud-selected` para `/data/local/tmp/ksud-selected` e
`/data/local/tmp/.ksud-stage`. O helper de `src/su_daemon.c` espera o primeiro
caminho. Seu `--late-load` usa um namespace de montagem privado, faz bind do
executável sobre `logcat` nesse namespace e executa `logcat late-load --kmi
android13-5.15 --package-name com.resukisu.resukisu`. O `ksud` carrega o
módulo embutido, inicia seus serviços e o helper verifica a interface de
controle do KernelSU. O runner aguarda `su -c id` por até 30 segundos.

Para executar o fluxo completo no dispositivo conectado:

```sh
cd "$RMG"
./simple-root/simple-root.sh RXCX602E20X
```

Se `/system/bin/su -c id` já devolver `uid=0(root)` antes do payload, o
script para após preparar e enviar o bundle. Nesse caso a saída do runner,
isoladamente, não prova que o **novo** `ksud` foi carregado naquele boot.
Para provar o caminho late-load, verifique o log de carregamento em um boot
sem root anterior e confirme versão, módulo e `su` depois.

## 10. Validação no aparelho e estado da evidência

Depois de uma execução que chegou ao late-load, os comandos de leitura são:

```sh
adb -s RXCX602E20X shell uname -r
adb -s RXCX602E20X shell /system/bin/su -c id
adb -s RXCX602E20X shell /system/bin/su -c 'grep kernelsu /proc/modules'
adb -s RXCX602E20X shell sha256sum \
  /data/local/tmp/simple-root/ksud-selected
```

No output do helper, a linha `KernelSU control verified` comprova que a
interface de controle respondeu. Para este build, o log de compilação informa
versão `33294`. O `id` deve mostrar `uid=0(root)`. Verifique também que o
`boot_id` não mudou durante a tentativa se for registrar uma campanha de
estabilidade:

```sh
adb -s RXCX602E20X shell cat /proc/sys/kernel/random/boot_id
```

O usuário informou que a versão integrada funcionou. Não há, neste diretório,
um log completo arquivado dessa execução bem-sucedida com hashes remotos,
`boot_id` inicial/final e resposta do helper. Os logs em
`simple-root/evidence/20260923-ksu-crash/` são dos testes anteriores e não
devem ser apresentados como prova do novo par de arquivos.

Uma consulta ADB posterior, de somente leitura, encontrou o mesmo firmware e
kernel AFZH3, mas `/system/bin/su` não estava disponível naquele boot. Isso
não desfaz a confirmação anterior; apenas impede atribuir prova de root à
inicialização consultada. Para uma próxima campanha, guarde a saída completa
do runner, os dois `boot_id` e os hashes dos assets no aparelho.

## 11. Falhas conhecidas e recuperação

| Sintoma | Conferência e ação |
| --- | --- |
| `AFZH3 boot kernel SHA256 mismatch` | A imagem de `device-afzh3/boot-unpacked/kernel` mudou. Confira firmware e origem; não altere o hash esperado para passar no teste sem identificar a imagem. |
| `kernel release mismatch` | Revise imagem, `.config`, `LOCALVERSION`, `BUILD_NUMBER`, Clang e `KERNEL_OUT`; refaça a preparação completa. |
| Falta `Module.symvers` | Rode `prepare-afzh3-kernel-v3.4.0.sh` até o fim. `modules_prepare` isolado não produz a tabela necessária para `CONFIG_MODVERSIONS=y`. |
| Falta memória na compilação LTO | Reduza `JOBS` na preparação e confira espaço livre. Swap temporário foi usado nesta bancada e removido depois. |
| `git apply` falha | Confira commit `1a879d6...` e alterações locais do clone. Use um clone dedicado e limpo para reaplicar o patch. |
| Rust target ou linker ausente | Instale `aarch64-linux-android` com `rustup target add aarch64-linux-android` e aponte `ANDROID_NDK_HOME` para o NDK com `aarch64-linux-android26-clang`. |
| `.ko` correto, mas `ksud` antigo | O módulo é embutido durante `cargo build`. Rode novamente `build-afzh3-v3.4.0.sh` e compare os hashes em `SHA256SUMS` e `simple-root/assets/`. |
| `vermagic` mostra `5.15.202...` | Foi escolhido o `.ko` genérico do release. Use `out/kernelsu-next-afzh3-v3.4.0/`, não `build-ksud-v3.4.0-release-ko.sh`. |
| Runner informa root já ativo | O caminho de late-load foi pulado. Não interprete essa saída como validação do novo módulo. |
| Carrega e depois ocorre panic | Preserve `last_kmsg`, `dmesg`, `logcat`, `boot_id` e hashes exatos. Não repita o carregamento no mesmo boot; investigue primeiro. |

## 12. Receita curta para repetir

Com as entradas da seção 3 presentes e o clone KernelSU no commit fixado:

```sh
export RMG=/home/matias/Projects/Root-My-Galaxy-SM-S918B
export SAMSUNG_SOURCE_ROOT=/home/matias/Projects/SM-S918B_16_Opensource
export SAMSUNG_KERNEL_SRC="$SAMSUNG_SOURCE_ROOT/kernel_platform/common"
export KSU_NEXT_SRC=/home/matias/Projects/github/KernelSU-Next-v3.4.0
export CLANG_ROOT=/home/matias/Projects/github/android-clang-r450784e
export ANDROID_NDK_HOME=/home/matias/Android/Sdk/ndk/28.2.13676358
export KERNEL_OUT="$RMG/kernelsu-next/out/afzh3-kernel"

JOBS=4 "$RMG/kernelsu-next/prepare-afzh3-kernel-v3.4.0.sh"
JOBS=8 "$RMG/kernelsu-next/build-afzh3-v3.4.0.sh"
cd "$RMG/kernelsu-next/out/kernelsu-next-afzh3-v3.4.0"
sha256sum -c SHA256SUMS
modinfo -F vermagic android13-5.15_kernelsu.ko
```

Resultado consumido pelo `simple-root`:

```text
/home/matias/Projects/Root-My-Galaxy-SM-S918B/kernelsu-next/out/kernelsu-next-afzh3-v3.4.0/ksud-next-v3.4.0
/home/matias/Projects/Root-My-Galaxy-SM-S918B/kernelsu-next/out/kernelsu-next-afzh3-v3.4.0/android13-5.15_kernelsu.ko
```

O diretório `out/` é saída de build local. Os três arquivos finais da seção 1
foram incluídos explicitamente no Git para preservar o par que funcionou;
o restante de `out/` continua ignorado. Preserve `SHA256SUMS` junto com os
dois binários ao copiar o resultado para outra máquina.
