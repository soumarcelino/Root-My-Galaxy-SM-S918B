#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef RMG_COMPACT_LOG_H
#define RMG_COMPACT_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int rmg_log_fprintf(FILE *stream, const char *format, ...);
int rmg_log_printf(const char *format, ...);
int rmg_log_puts(const char *text);
void rmg_log_perror(const char *text);

#ifdef __cplusplus
}
#endif

#ifndef RMG_COMPACT_LOG_NO_REMAP
#define fprintf rmg_log_fprintf
#define printf rmg_log_printf
#define puts rmg_log_puts
#define perror rmg_log_perror
#endif

#endif
