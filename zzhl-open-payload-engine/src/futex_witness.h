#ifndef OSS_FUTEX_WITNESS_H
#define OSS_FUTEX_WITNESS_H
#include <stdint.h>
struct oss_futex_witness { uint64_t task, waiter, lock; int pid; };
int oss_parse_futex_witness_text(const char *, int,
                                 struct oss_futex_witness *);
int oss_read_futex_witness_trace(int, struct oss_futex_witness *);
#endif
