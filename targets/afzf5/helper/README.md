# AFZF5 helper

`libcve43499root.so` is the prebuilt helper expected by the AFZF5 port script
(MD5 `e00bec081b9d35635245bc76cf2d2a51`). The script copies it into the
Android app bundle and stages it over ADB. Source for this closed binary is
not available in this repository. The app bundle may contain a different
helper until the AFZF5 preparation script runs.

`su_daemon.c` is the separate open-source native helper built by the root
`Makefile` for the AFZF5 target. It is not the closed app library above.
Build it directly with `make -C targets/afzf5/helper`.
