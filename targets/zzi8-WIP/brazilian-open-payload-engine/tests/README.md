# Focused diagnostics

This directory contains the small diagnostics still useful for isolating
production components. `tests/support/` contains implementations used only by
those diagnostics. None of these files is linked into the payload.

Build all diagnostics with:

```sh
make tests
```

Run the host-only regression for configfs AAR address encoding with
`make test-aar-read-plan`. It includes the historical crash geometry plus the
four real read/write addresses rejected by recent ZZI8 runs. Run
`make test-pipe-plan` to prove that only aligned candidates with complete
read/write plans can make `pipe_plan.ready` true before mutation.
Run `make test-umh-binfmt-plan` to pin the real ZZI8 static-helper offset,
the short replacement path, UID-to-binfmt encoding, and the complete original
firmware guard bytes used for restoration.

`test_futex_trigger` touches live kernel state and requires the exact current
boot kernel base. The remaining tests keep their original scope and safety
notes in their source headers.

Superseded root-chain variants (`test_root*` and `test_root_v2` through
`test_root_v9`) were removed. Their reasoning and results remain in Git and
`STATUS.md`.
