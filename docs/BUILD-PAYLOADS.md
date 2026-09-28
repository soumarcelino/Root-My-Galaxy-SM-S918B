# Build the payloads

Use Android NDK `28.2.13676358` or set `ANDROID_NDK_HOME` to another compatible
NDK. Every BOPE target has its own build directory, so rebuilding one firmware
does not silently mix it with another. Run the commands below from the
repository root unless a section changes directories explicitly.

## ZZI8 :: BOPE · latest One UI 9 Beta 2 firmware

```sh
cd targets/zzi8-WIP/brazilian-open-payload-engine
python3 tools/verify-zzi8-target.py
make clean all so
```

## ZZHL :: BOPE

```sh
cd targets/zzhl/brazilian-open-payload-engine
python3 tools/verify-zzhl-target.py
make clean all so
```

## AFZH3 :: BOPE-Beta

```sh
make -C targets/afzh3/brazilian-open-payload-engine clean all so
```

## FZF5 :: Old Chinese Payload

Build the legacy FZF5 payload from the repository root:

```sh
make TARGET=dm3q-S918BXXSAFZF5
```

## AFZG1 and SM-S918N FZG1

The AFZG1 and `SM-S918N` FZG1 profiles currently use their verified prebuilt
payload artifacts. Their buildable KernelSU Next component is separate; AFZG1
can be rebuilt with:

```sh
targets/afzg1/kernelsu-next/build-afzg1.sh
```
