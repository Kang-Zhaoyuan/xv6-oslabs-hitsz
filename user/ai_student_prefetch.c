#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "ai_student_impl.h"

// state[4..7]: child pid, command pipe, result pipe, pending/ready state.
static int
transfer(int fd, void *buffer, int length, int writing)
{
  char *p = buffer;
  while(length > 0) {
    int n = writing ? write(fd, p, length) : read(fd, p, length);
    if(n <= 0)
      return -1;
    p += n;
    length -= n;
  }
  return 0;
}

static void
prefetch_worker(struct ai_session *session, int commands, int results)
{
  struct kv_entry kv[AI_KV_TOKENS];
  struct ai_io io = {0};
  char name[AI_STREAM_NAME_LEN];
  int request;

  close(session->state[1]);
  ai_student_kv_name(name, session->worker);
  session->state[1] = open(name, O_RDONLY);
  while(transfer(commands, &request, sizeof(request), 0) == 0) {
    int status = ai_student_kv_restore(session, request, kv, &io);
    if(transfer(results, &status, sizeof(status), 1) < 0 || status < 0 ||
       transfer(results, kv, sizeof(kv), 1) < 0)
      break;
  }
  if(session->state[1] >= 0)
    close(session->state[1]);
  close(commands);
  close(results);
  exit(0);
}

int
ai_student_prefetch_begin(struct ai_session *session)
{
  int commands[2], results[2];
  if(session->state[3] != 3 || session->state[2] != 0 || session->state[4] != 0)
    return -1;
  if(pipe(commands) < 0)
    return -1;
  if(pipe(results) < 0) {
    close(commands[0]);
    close(commands[1]);
    return -1;
  }
  int pid = fork();
  if(pid == 0) {
    close(commands[1]);
    close(results[0]);
    prefetch_worker(session, commands[0], results[1]);
  }
  close(commands[0]);
  close(results[1]);
  if(pid < 0) {
    close(commands[1]);
    close(results[0]);
    return -1;
  }
  session->state[4] = pid;
  session->state[5] = commands[1];
  session->state[6] = results[0];
  session->state[7] = 0;
  return 0;
}

int
ai_student_prefetch_next(struct ai_session *session, int request)
{
  if(session->state[4] <= 0 || session->state[7] != 0 ||
     request < 0 || request >= session->requests || request != session->state[2])
    return -1;
  if(transfer(session->state[5], &request, sizeof(request), 1) < 0)
    return -1;
  session->state[7] = 1;
  return 0;
}

int
ai_student_prefetch_wait(struct ai_session *session, int request)
{
  int status;
  if(session->state[4] <= 0 || session->state[7] != 1 ||
     request != session->state[2])
    return -1;
  if(transfer(session->state[6], &status, sizeof(status), 0) < 0 || status < 0)
    return -1;
  session->state[7] = 2;
  return 0;
}

int
ai_student_prefetch_restore(struct ai_session *session, int request,
                            struct kv_entry *kv, struct ai_io *io)
{
  if(session->state[4] <= 0 || session->state[7] != 2 ||
     request < 0 || request >= session->requests || request != session->state[2])
    return -1;
  if(transfer(session->state[6], kv, AI_KV_FILE_BYTES, 0) < 0)
    return -1;
  session->state[2]++;
  session->state[7] = 0;
  io->kv_read_bytes += AI_KV_FILE_BYTES;
  return 0;
}

void
ai_student_prefetch_end(struct ai_session *session)
{
  if(session->state[4] > 0) {
    close(session->state[5]);
    close(session->state[6]);
    wait(0);
    session->state[4] = 0;
  }
}
