# Project structure

## Shared code and applications

- `app/`: standalone Android Gradle project, including wrapper, settings,
  source, assets, and build output.
- `RootMyGalaxyDesktop/`: desktop runner and bundled runtime assets.
- `Makefile`: native build entry point; it selects sources inside each
  firmware's `payload/src/` and `helper/` directories.
- `tools/`: shared porting tools.
- `targets/afzf5/kernelsu/`: classic KernelSU reference used by the AFZF5 port.
- `targets/afzh3/kernelsu-next/`: AFZH3 KernelSU Next build and reference.
- `targets/afzg1/kernelsu-next/`: AFZG1 KernelSU Next build, guard, and patch.
- `targets/afzg1/payload/`, `targets/afzg1/helper/`: AFZG1 payload and root
  helper copies; app and desktop retain their packaging copies.
- `stability-launcher/`, `simple-root/`: launcher and root helper.
- `docs/`: shared guides and screenshots.

## Firmware-specific material

- `targets/afzf5/specs/`: AFZF5 payload patch specification.
- `targets/afzf5/payload/`: verified payload, native source, and build output.
- `targets/afzf5/helper/`: prebuilt app helper and native helper source.
- `targets/afzh3/reference/kernel/`: AFZH3 kernel reference material.
- `targets/afzh3/reference/kernel/legacy-target/`: archived native target
  headers retained as reference data.
- `targets/afzh3/helper/`: AFZH3 helper source, build, and output.
- `targets/zzhl-WIP/specs/`, `targets/zzhl-WIP/docs/`: ZZHL specifications and notes.
- `targets/zzhl-WIP/payload/src/`: ZZHL native P0 source and target headers.
- `targets/zzhl-WIP/helper/`: ZZHL helper source, build, and output.
- `targets/zzhl-WIP/firmware/`: ZZHL kernel dump, extracted data, and porting
  reference.
- `targets/afzh3/open-payload-engine/`,
  `targets/zzhl-WIP/open-payload-engine/`: separate engines, each with source,
  tests, tools, documentation, and build outputs.

Firmware paths use `targets/<firmware>/` directly. Active native target headers
live under each native payload's `src/targets/`.

Generated build directories and local virtual environments are not source.
