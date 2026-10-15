#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "ai_student_impl.h"

// state[1..3]: file descriptor, next request, phase (1 write, 2 closed, 3 read).
int
ai_student_kv_begin(struct ai_session *session)
{
  session->state[1] = -1;
  session->state[2] = 0;
  session->state[3] = 0;
  if(session->worker < 0 || session->worker >= AI_MAX_WORKERS ||
     session->requests <= 0 || session->requests > AI_MAX_REQUESTS)
    return -1;
  return 0;
}

int
ai_student_kv_begin_write(struct ai_session *session)
{
  char name[AI_STREAM_NAME_LEN];
  if(session->state[3] != 0)
    return -1;
  ai_student_kv_name(name, session->worker);
  unlink(name);
  int fd = open(name, O_CREATE | O_WRONLY);
  if(fd < 0)
    return -1;
  session->state[1] = fd;
  session->state[2] = 0;
  session->state[3] = 1;
  return 0;
}

static int
kv_transfer(struct ai_session *session, int request, struct kv_entry *kv,
            struct ai_io *io, int writing)
{
  if(session->state[3] != (writing ? 1 : 3) || session->state[1] < 0 ||
     request < 0 || request >= session->requests || request != session->state[2])
    return -1;
  int offset = 0;
  while(offset < AI_KV_FILE_BYTES) {
    int n;
    if(writing)
      n = write(session->state[1], (char *)kv + offset, AI_KV_FILE_BYTES - offset);
    else
      n = read(session->state[1], (char *)kv + offset, AI_KV_FILE_BYTES - offset);
    if(n <= 0) {
      session->state[3] = -1;
      return -1;
    }
    offset += n;
  }
  session->state[2]++;
  if(writing)
    io->kv_write_bytes += AI_KV_FILE_BYTES;
  else
    io->kv_read_bytes += AI_KV_FILE_BYTES;
  return 0;
}

int
ai_student_kv_store(struct ai_session *session, int request,
                    struct kv_entry *kv, struct ai_io *io)
{
  return kv_transfer(session, request, kv, io, 1);
}

int
ai_student_kv_end_write(struct ai_session *session)
{
  if(session->state[3] != 1 || session->state[2] != session->requests)
    return -1;
  close(session->state[1]);
  session->state[1] = -1;
  session->state[3] = 2;
  return 0;
}

int
ai_student_kv_begin_read(struct ai_session *session)
{
  char name[AI_STREAM_NAME_LEN];
  if(session->state[3] != 2)
    return -1;
  ai_student_kv_name(name, session->worker);
  int fd = open(name, O_RDONLY);
  if(fd < 0)
    return -1;
  session->state[1] = fd;
  session->state[2] = 0;
  session->state[3] = 3;
  return 0;
}

int
ai_student_kv_restore(struct ai_session *session, int request,
                      struct kv_entry *kv, struct ai_io *io)
{
  return kv_transfer(session, request, kv, io, 0);
}

void
ai_student_kv_end(struct ai_session *session)
{
  char name[AI_STREAM_NAME_LEN];
  if(session->state[1] >= 0)
    close(session->state[1]);
  session->state[1] = -1;
  session->state[3] = 0;
  ai_student_kv_name(name, session->worker);
  unlink(name);
}
