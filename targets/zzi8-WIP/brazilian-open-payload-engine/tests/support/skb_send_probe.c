/* Test-only probe for one socketpair/sendmsg transfer. Production keeps
 * the socket open and sends a batch inside src/05_mm_slab_grooming.c. */
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "skb_send_probe.h"

int probe_skb_send(const unsigned char *object,
                               size_t object_len) {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return 0;
  }

  struct iovec iov = {
    .iov_base = (void *)object,
    .iov_len = object_len,
  };
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  ssize_t sent = sendmsg(fds[0], &msg, 0);
  int ok = sent == (ssize_t)object_len;

  close(fds[0]);
  close(fds[1]);
  return ok;
}
