#ifndef OSS_TEST_SKB_SEND_PROBE_H
#define OSS_TEST_SKB_SEND_PROBE_H

#include <stddef.h>
#include <stdint.h>

/* Sends one buffer through an AF_UNIX socketpair and closes both ends.
 * This probes sendmsg mechanics only; it is not the production reclaim batch. */
int probe_skb_send(const unsigned char *object,
                               size_t object_len);

#endif
