#define _GNU_SOURCE
#include "dentry_pipe_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define OSS_PAGE_SIZE 4096u
#define OSS_PIPE_MARKER 0x52u

static int io_full(int fd, void *buffer, size_t size, int writing) {
  unsigned char *cursor = buffer;
  while (size) {
    ssize_t done = writing ? write(fd, cursor, size) : read(fd, cursor, size);
    if (done <= 0) return 0;
    cursor += (size_t)done;
    size -= (size_t)done;
  }
  return 1;
}

static void close_fd(int *fd) {
  if (*fd >= 0) close(*fd);
  *fd = -1;
}

void oss_dentry_pipe_transport_init(struct oss_dentry_pipe_transport *state) {
  memset(state, 0, sizeof(*state));
  state->control_fd = -1;
  state->keeper_pid = -1;
  state->narrowed_pipe = -1;
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    state->pipes[i][0] = -1;
    state->pipes[i][1] = -1;
  }
}

static void keeper_child(struct oss_dentry_pipe_transport *state, int control) {
  uint32_t selected = UINT32_MAX;
  if (!io_full(control, &selected, sizeof(selected), 0) ||
      selected >= OSS_DENTRY_PIPE_COUNT) {
    for (;;) pause();
  }
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    if (i == selected) continue;
    close_fd(&state->pipes[i][0]);
    close_fd(&state->pipes[i][1]);
  }
  unsigned char ack = 'K';
  if (!io_full(control, &ack, 1, 1)) {
    for (;;) pause();
  }
  unsigned char command = 0;
  if (!io_full(control, &command, 1, 0) || command != 'R') {
    for (;;) pause();
  }
  close_fd(&state->pipes[selected][0]);
  close_fd(&state->pipes[selected][1]);
  ack = 'D';
  (void)io_full(control, &ack, 1, 1);
  close(control);
  _exit(0);
}

static int spawn_keeper(struct oss_dentry_pipe_transport *state) {
  int control[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, control) != 0) {
    return 0;
  }
  pid_t child = fork();
  if (child == 0) {
    close(control[0]);
    keeper_child(state, control[1]);
  }
  close(control[1]);
  if (child < 0) {
    close(control[0]);
    return 0;
  }
  state->control_fd = control[0];
  state->keeper_pid = child;
  return 1;
}

int oss_dentry_pipe_prepare(struct oss_dentry_pipe_transport *state) {
  unsigned char page[OSS_PAGE_SIZE];
  memset(page, 0x5a, sizeof(page));
  memcpy(page, "RMG-P0-PIPE", 11);
  page[0] = OSS_PIPE_MARKER;

  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    if (pipe2(state->pipes[i], O_CLOEXEC) != 0) return 0;
    (void)fcntl(state->pipes[i][0], F_SETPIPE_SZ,
                OSS_DENTRY_PIPE_PAGES * OSS_PAGE_SIZE);
    for (size_t slot = 0; slot < OSS_DENTRY_PIPE_PAGES; slot++) {
      if (!io_full(state->pipes[i][1], page, sizeof(page), 1)) return 0;
    }
  }
  return spawn_keeper(state);
}

int oss_dentry_pipe_gate(struct oss_dentry_pipe_transport *state,
                         size_t *changed_pipe) {
  size_t changed = 0;
  size_t selected = 0;
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    unsigned char value = 0;
    if (!io_full(state->pipes[i][0], &value, 1, 0)) return -1;
    if (value != OSS_PIPE_MARKER) {
      changed++;
      selected = i;
    }
  }
  if (changed_pipe) *changed_pipe = selected;
  if (changed == 0) return 0;
  if (changed != 1) return -1;
  return oss_dentry_pipe_narrow(state, selected) ? 1 : -1;
}

int oss_dentry_pipe_advance(struct oss_dentry_pipe_transport *state) {
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    unsigned char ignored;
    if (!io_full(state->pipes[i][0], &ignored, 1, 0)) return 0;
  }
  return 1;
}

int oss_dentry_pipe_capture(struct oss_dentry_pipe_transport *state,
                            unsigned char *snapshots, size_t snapshots_size) {
  size_t required = OSS_DENTRY_PIPE_COUNT * OSS_DENTRY_PIPE_SNAPSHOT_SIZE;
  if (!snapshots || snapshots_size < required) return 0;
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    if (!io_full(state->pipes[i][0],
                 snapshots + i * OSS_DENTRY_PIPE_SNAPSHOT_SIZE,
                 OSS_DENTRY_PIPE_SNAPSHOT_SIZE, 0)) {
      return 0;
    }
  }
  return 1;
}

int oss_dentry_pipe_narrow(struct oss_dentry_pipe_transport *state,
                           size_t pipe_index) {
  if (state->control_fd < 0 || pipe_index >= OSS_DENTRY_PIPE_COUNT) return 0;
  uint32_t selected = (uint32_t)pipe_index;
  unsigned char ack = 0;
  if (!io_full(state->control_fd, &selected, sizeof(selected), 1) ||
      !io_full(state->control_fd, &ack, 1, 0) || ack != 'K') {
    return 0;
  }
  /* The K ack means the keeper retained only `selected`. Drop every parent
   * copy now, making the keeper the sole owner. Otherwise its later D ack
   * closes only duplicate descriptors and does not release the pipe object. */
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    close_fd(&state->pipes[i][0]);
    close_fd(&state->pipes[i][1]);
  }
  state->narrowed_pipe = (int)pipe_index;
  return 1;
}

int oss_dentry_pipe_release(struct oss_dentry_pipe_transport *state) {
  if (state->control_fd < 0 || state->keeper_pid <= 0 ||
      state->narrowed_pipe < 0) return 0;
  unsigned char command = 'R';
  unsigned char ack = 0;
  int ok = io_full(state->control_fd, &command, 1, 1) &&
           io_full(state->control_fd, &ack, 1, 0) && ack == 'D';
  close_fd(&state->control_fd);
  int status = 0;
  pid_t waited = waitpid(state->keeper_pid, &status, 0);
  state->keeper_pid = -1;
  return ok && waited > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void oss_dentry_pipe_abort(struct oss_dentry_pipe_transport *state) {
  close_fd(&state->control_fd);
  if (state->keeper_pid > 0) {
    kill(state->keeper_pid, SIGKILL);
    (void)waitpid(state->keeper_pid, NULL, 0);
    state->keeper_pid = -1;
  }
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    close_fd(&state->pipes[i][0]);
    close_fd(&state->pipes[i][1]);
  }
}
