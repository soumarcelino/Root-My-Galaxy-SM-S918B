# Stability Launcher dos BOPEs One UI 9

`bope-stability-launcher.c` é a fonte compartilhada usada pelos Makefiles de
ZZHL e ZZI8. Ela preserva o Stability Launcher que esses BOPEs já usavam,
antes mantido fora do repositório em `ksu-payload-functional`.

`RMG_BOPE_VALIDATION` usa a geometria de `src/target.h` do firmware durante a
compilação. No aparelho, o launcher confere a geometria de `mm_struct`, testa a
capacidade de 480 pipes, libera os pipes e só então conta uma sequência de
amostras estáveis. As amostras exigem memória, temperatura, runnable, PSI e
baixa variação de `mm_struct`, sem teto absoluto de objetos ou slabs. A
geometria é conferida novamente antes de carregar o payload. A identidade do
firmware é responsabilidade do app; o runner ADB tem seu próprio preflight.
