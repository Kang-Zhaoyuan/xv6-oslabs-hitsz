#include "kernel/types.h"
#include "user/user.h"

#include "ai_student_impl.h"

// 完成对应任务并通过单项测试后，将标志改为 1。
#define AI_MODEL_TASK_IMPLEMENTED 1
#define AI_KV_TASK_IMPLEMENTED 1
#define AI_PREFETCH_TASK_IMPLEMENTED 1

static int
student_begin_common(struct ai_session *session, int worker, int requests,
                     int prefetch)
{
  memset(session, 0, sizeof(*session));
  session->worker = worker;
  session->requests = requests;
  session->state[0] = prefetch;

  if(ai_student_model_begin(session) < 0)
    return -1;
  if(ai_student_kv_begin(session) < 0) {
    ai_student_model_end(session);
    return -1;
  }
  return 0;
}

static int
student_begin(struct ai_session *session, int worker, int requests)
{
  return student_begin_common(session, worker, requests, 0);
}

static int
prefetch_begin(struct ai_session *session, int worker, int requests)
{
  return student_begin_common(session, worker, requests, 1);
}

static int
student_load(struct ai_session *session, uint family, uint shard,
             char *block, struct ai_io *io)
{
  return ai_student_model_load(session, family, shard, block, io);
}

static int
student_begin_kv_write(struct ai_session *session)
{
  return ai_student_kv_begin_write(session);
}

static int
student_store_kv(struct ai_session *session, int request,
                 struct kv_entry *kv, struct ai_io *io)
{
  return ai_student_kv_store(session, request, kv, io);
}

static int
student_end_kv_write(struct ai_session *session)
{
  return ai_student_kv_end_write(session);
}

static int
student_begin_kv_read(struct ai_session *session)
{
  if(ai_student_kv_begin_read(session) < 0)
    return -1;
  if(session->state[0] && ai_student_prefetch_begin(session) < 0)
    return -1;
  return 0;
}

static int
student_prefetch_next(struct ai_session *session, int request)
{
  if(!session->state[0])
    return 0;
  return ai_student_prefetch_next(session, request);
}

static int
student_prefetch_wait(struct ai_session *session, int request)
{
  if(!session->state[0])
    return 0;
  return ai_student_prefetch_wait(session, request);
}

static int
student_restore_kv(struct ai_session *session, int request,
                   struct kv_entry *kv, struct ai_io *io)
{
  if(session->state[0])
    return ai_student_prefetch_restore(session, request, kv, io);
  return ai_student_kv_restore(session, request, kv, io);
}

static void
student_end(struct ai_session *session)
{
  if(session->state[0])
    ai_student_prefetch_end(session);
  ai_student_kv_end(session);
  ai_student_model_end(session);
  memset(session, 0, sizeof(*session));
}

struct ai_path ai_student_path = {
  .mode = AI_MODE_STUDENT,
  .name = "student",
  .implemented = AI_MODEL_TASK_IMPLEMENTED && AI_KV_TASK_IMPLEMENTED,
  .prefetch = 0,
  .begin = student_begin,
  .load = student_load,
  .begin_kv_write = student_begin_kv_write,
  .store_kv = student_store_kv,
  .end_kv_write = student_end_kv_write,
  .begin_kv_read = student_begin_kv_read,
  .prefetch_next = student_prefetch_next,
  .prefetch_wait = student_prefetch_wait,
  .restore_kv = student_restore_kv,
  .end = student_end,
};

struct ai_path ai_prefetch_path = {
  .mode = AI_MODE_PREFETCH,
  .name = "prefetch",
  .implemented = AI_MODEL_TASK_IMPLEMENTED && AI_KV_TASK_IMPLEMENTED &&
                 AI_PREFETCH_TASK_IMPLEMENTED,
  .prefetch = 1,
  .begin = prefetch_begin,
  .load = student_load,
  .begin_kv_write = student_begin_kv_write,
  .store_kv = student_store_kv,
  .end_kv_write = student_end_kv_write,
  .begin_kv_read = student_begin_kv_read,
  .prefetch_next = student_prefetch_next,
  .prefetch_wait = student_prefetch_wait,
  .restore_kv = student_restore_kv,
  .end = student_end,
};
