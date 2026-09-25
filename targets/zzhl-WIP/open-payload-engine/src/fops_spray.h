#ifndef OSS_CLONE_FOPS_SPRAY_H
#define OSS_CLONE_FOPS_SPRAY_H

#include <stddef.h>
#include <stdint.h>

/* source: FUN_00106288's delivery mechanism -- an AF_UNIX SOCK_STREAM
 * socketpair, sendmsg() of a fixed 0x8e80-byte buffer built by
 * build_fops_install_object() (fops_install.c). The 0x8e80 size is the
 * closed binary's own empirically-chosen spray size (not derived from
 * any named kernel struct constant -- checked: it doesn't match
 * MM_STRUCT_SZ, ORDER3_SIZE, or KMALLOC_PIPE_OBJ_SIZE), read directly
 * out of its .rodata/code (`DAT_0010d9a0 = malloc(0x8e80)`).
 *
 * This step is pure userspace + one sendmsg call. It does not by itself
 * corrupt any real kernel object -- reclaiming the freed mm_struct slot
 * with attacker content is not itself unsafe; the risk starts only once
 * something later reads task->pi_blocked_on into this object and calls
 * rt_mutex_adjust_pi on a real thread (not ported yet). Safe to test
 * standalone: worst case is the spray simply not landing where
 * intended, not a kernel panic. */
int spray_fops_install_object(const unsigned char *object,
                               size_t object_len);

#endif
