#ifndef OSS_CLONE_DENTRY_PIPE_TRANSPORT_H
#define OSS_CLONE_DENTRY_PIPE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "dentry_fops_preflight.h"

#define OSS_DENTRY_PIPE_COUNT 256u
#define OSS_DENTRY_PIPE_PAGES 4u

struct oss_dentry_pipe_transport {
  int pipes[OSS_DENTRY_PIPE_COUNT][2];
  int control_fd;
  pid_t keeper_pid;
  int narrowed_pipe;
};

void oss_dentry_pipe_transport_init(struct oss_dentry_pipe_transport *state);
int oss_dentry_pipe_prepare(struct oss_dentry_pipe_transport *state);
int oss_dentry_pipe_gate(struct oss_dentry_pipe_transport *state,
                         size_t *changed_pipe);
int oss_dentry_pipe_advance(struct oss_dentry_pipe_transport *state);
int oss_dentry_pipe_capture(struct oss_dentry_pipe_transport *state,
                            unsigned char *snapshots, size_t snapshots_size);
int oss_dentry_pipe_narrow(struct oss_dentry_pipe_transport *state,
                           size_t pipe_index);
int oss_dentry_pipe_release(struct oss_dentry_pipe_transport *state);
void oss_dentry_pipe_abort(struct oss_dentry_pipe_transport *state);

#endif
