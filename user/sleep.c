#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("Sleep needs one argument!\n");
    exit(-1);
  }

  int ticks = atoi(argv[1]);
  sleep(ticks);
  printf("Sleep %d\n", ticks);
  exit(0);
}
