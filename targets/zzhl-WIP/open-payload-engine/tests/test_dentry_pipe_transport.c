#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dentry_pipe_transport.h"

static void read_exact(int fd, size_t size) {
  unsigned char buffer[4096];
  while (size) {
    size_t chunk = size < sizeof(buffer) ? size : sizeof(buffer);
    assert(read(fd, buffer, chunk) == (ssize_t)chunk);
    size -= chunk;
  }
}

int main(void) {
  struct oss_dentry_pipe_transport state;
  oss_dentry_pipe_transport_init(&state);
  assert(oss_dentry_pipe_prepare(&state));

  size_t bytes = OSS_DENTRY_PIPE_COUNT * OSS_DENTRY_PIPE_SNAPSHOT_SIZE;
  unsigned char *snapshots = malloc(bytes);
  assert(snapshots != NULL);
  assert(oss_dentry_pipe_capture(&state, snapshots, bytes));
  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    assert(snapshots[i * OSS_DENTRY_PIPE_SNAPSHOT_SIZE] == 'R');
  }
  assert(oss_dentry_pipe_advance(&state));

  for (size_t i = 0; i < OSS_DENTRY_PIPE_COUNT; i++) {
    read_exact(state.pipes[i][0], 3 * 4096);
    unsigned char value = i == 17 ? 0x41 : 'R';
    assert(write(state.pipes[i][1], &value, 1) == 1);
  }
  size_t selected = 0;
  assert(oss_dentry_pipe_gate(&state, &selected) == 1);
  assert(selected == 17);
  assert(state.narrowed_pipe == 17);
  assert(oss_dentry_pipe_release(&state));

  free(snapshots);
  oss_dentry_pipe_abort(&state);
  return 0;
}
