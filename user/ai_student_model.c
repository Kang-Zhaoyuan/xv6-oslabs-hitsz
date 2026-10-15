#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "ai_student_impl.h"

struct model_cache {
  uchar valid[AI_TOTAL_SHARDS];
  char data[AI_TOTAL_SHARDS][AI_SHARD_BYTES];
};

int
ai_student_model_begin(struct ai_session *session)
{
  struct model_cache *cache = malloc(sizeof(*cache));
  session->memory = cache;
  if(!cache)
    return -1;
  memset(cache->valid, 0, sizeof(cache->valid));
  return 0;
}

int
ai_student_model_load(struct ai_session *session, uint family, uint shard,
                      char *block, struct ai_io *io)
{
  struct model_cache *cache = session->memory;
  char name[AI_SHARD_NAME_LEN];
  uint index;

  if(!cache || (family != AI_MODEL_FAMILY && family != AI_EMBED_FAMILY) ||
     shard >= (family == AI_MODEL_FAMILY ? AI_MODEL_SHARDS : AI_EMBED_SHARDS))
    return -1;
  index = shard + (family == AI_EMBED_FAMILY ? AI_MODEL_SHARDS : 0);
  if(!cache->valid[index]) {
    ai_shard_name(name, family, shard);
    int fd = open(name, O_RDONLY);
    if(fd < 0)
      return -1;
    int offset = 0;
    while(offset < AI_SHARD_BYTES) {
      int n = read(fd, cache->data[index] + offset, AI_SHARD_BYTES - offset);
      if(n <= 0) {
        close(fd);
        return -1;
      }
      offset += n;
    }
    close(fd);
    for(offset = 0; offset < AI_SHARD_BYTES; offset++)
      if((uchar)cache->data[index][offset] != ai_byte(family, shard, offset))
        return -1;
    cache->valid[index] = 1;
    if(family == AI_MODEL_FAMILY)
      io->model_read_bytes += AI_SHARD_BYTES;
    else
      io->embed_read_bytes += AI_SHARD_BYTES;
  }
  memmove(block, cache->data[index], AI_SHARD_BYTES);
  return 0;
}

void
ai_student_model_end(struct ai_session *session)
{
  if(session->memory)
    free(session->memory);
  session->memory = 0;
}
