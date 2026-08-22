# Helper standalone legível

O fonte canônico é [`port/helper-next/su_daemon.c`](port/helper-next/su_daemon.c).
Ele é exatamente o helper ligado ao monólito por `analysis/build_monolithic.py`,
onde seu `main` é renomeado para `helper_next_main`.

Este diretório o recompila como ELF standalone, preservando o `main` original:

```sh
make -C helper-src
```

Saída: `helper-src/helper-readable` (Android/AArch64, API 35).

Integração esperada com o payload:

```text
helper-readable --run-payload PAYLOAD helper-readable LOG
  -> define CVE43499_ROOT_HELPER com o próprio caminho
  -> dlopen(PAYLOAD, RTLD_NOW | RTLD_LOCAL)
  -> o construtor do payload executa
  -> se a transição privilegiada tiver sucesso, o kernel chama
     helper-readable --umh ...
  -> o helper cria o daemon temporário
```

O teste real da corrida pode reiniciar o aparelho caso o layout/ABI do payload
legível esteja incorreto. Compilação, `--probe`, `--version` e carregamento com
`SLIDE_ONLY=1` são as validações iniciais não destrutivas.
