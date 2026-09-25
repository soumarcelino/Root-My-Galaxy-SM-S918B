# Limitações, riscos e manutenção

## Compatibilidade

O código está validado para `SM-S918B/dm3q`, firmware `S918BXXSAFZH3`, kernel
registrado nesta documentação. Offsets, layouts e comportamento do allocator
são específicos. Um firmware próximo pode compilar e iniciar, mas ainda ser
incompatível.

## Fonte Samsung

A árvore `/home/matias/Projects/SM-S918B_16_Opensource/` ajudou a entender
semântica de workqueue, rtmutex e estruturas. Ela é compatível com a família
5.15.189/Kalama/dm3q, mas essa compatibilidade não equivale a identidade byte a
byte com o kernel do aparelho. BTF/kallsyms/runtime têm precedência para o
alvo em execução.

## Concorrência da workqueue

O código reduz a janela TOCTOU, mas não possui o lock interno do pool. Carga
concorrente pode mudar worklist/idle após a última leitura. Dois boots passaram;
isso não constitui prova matemática de ausência de race.

## Uma execução por boot

O fluxo conserva holders, pipes e estado temporário. A observação do fechado e
do clone indica uma execução útil por boot. Uma segunda execução pode falhar
por estado residual e não deve ser usada para aceitar/rejeitar mudança.

## Logs e timing

Adicionar `fprintf`, alocações, sleeps ou syscalls em janelas críticas pode
alterar allocator/scheduler. Logs devem ocorrer antes ou depois de:

- ondas de close;
- send de reclaim;
- sigreturn/sched_setattr;
- publicação da worklist e wake.

## Processo holder

`cve43499-hold` mantém alocações necessárias. Matá-lo cedo pode liberar memória
ainda referenciada. Em boot diagnóstico, processos órfãos também alteram a
geometria de uma nova tentativa. Preferir reboot limpo em vez de cleanup
parcial improvisado.

## Segurança de falha

Depois de uma mutação potencialmente irreversível:

- não repetir automaticamente;
- não tentar “desfazer” lista concorrente sem lock;
- não liberar páginas fake enquanto kernel pode referenciá-las;
- salvar evidência;
- reiniciar limpo para próximo teste.

## Artefatos e reprodutibilidade

Toda release/teste deve registrar:

- commit ou snapshot de fontes;
- versão do NDK e API;
- comando de build;
- hash e tamanho do `.so`;
- hashes dos helper/ksud quando mudarem;
- firmware/kernel/serial;
- dois logs e dois boot IDs.

O diretório `/tmp` não é armazenamento permanente. Logs finais relevantes
devem ser copiados para uma área versionada ou arquivo de evidência antes de
limpeza/reboot do host.

## Regressões mínimas

Antes de teste no device:

```sh
rtk make -B -j2 all so
rtk bash -n /home/matias/Projects/ksu-payload-functional/simple-root
rtk sha256sum build/payload.so
```

O Makefile atual compila com `-Wall -Wextra`. Warnings novos em código de
ponteiros, tamanhos ou signedness devem ser tratados antes do device.

## Atualização de offsets

Quando o firmware mudar:

1. coletar identidade e kallsyms do novo boot;
2. extrair BTF/layouts;
3. comparar binário fechado correspondente;
4. derivar offsets novamente;
5. verificar cada ponteiro por faixa/relação;
6. não carregar perfil antigo como fallback silencioso;
7. repetir protocolo 2 reboots.

## Atualização documental

- `STATUS.md`: cronologia, tentativas, hipóteses e evidência bruta.
- `README.md`: estado resumido e build.
- `docs/`: arquitetura consolidada e procedimentos atuais.

Se um documento contradizer o código, o conflito deve ser resolvido por
evidência; não ajustar texto para esconder divergência.

## Itens que devem permanecer explícitos

- configfs não é backend geral para SLUB dinâmico;
- a ordem de socketpairs é parte da geometria;
- CPU affinity precisa ser restaurada;
- root exige `su -c id`, não apenas log interno;
- final exige dois boots;
- compatível não significa kernel-fonte exato;
- uma falha do fechado também pode revelar race real, não necessariamente
  divergência do clone.

## Próximas melhorias seguras

- arquivar logs finais fora de `/tmp` com manifesto de hashes;
- adicionar ferramenta somente-leitura que gere relatório por boot;
- gerar tabela automática de offsets do build alvo e compará-la ao código;
- adicionar teste host dos layouts/`_Static_assert` sem duplicar implementação;
- registrar versões de Ghidra/radare2/NDK usadas em futuras derivações.

Essas melhorias não devem inserir trabalho no caminho crítico do payload.
