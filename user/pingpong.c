#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int p2c[2];
  int c2p[2];
  char buf[1];

  pipe(p2c);
  pipe(c2p);
  int parent_pid = getpid();
  int pid = fork();

  if(pid < 0){
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(p2c[1]);
    close(c2p[0]);

    read(p2c[0], buf, 1);
    printf("%d: received ping from pid %d\n", getpid(), parent_pid);

    write(c2p[1], buf, 1);

    close(p2c[0]);
    close(c2p[1]);
    exit(0);
  } else {
    close(p2c[0]);
    close(c2p[1]);

    write(p2c[1], "x", 1);

    read(c2p[0], buf, 1);
    printf("%d: received pong from pid %d\n", getpid(), pid);

    close(p2c[1]);
    close(c2p[0]);

    wait(0);
    exit(0);
  }
}