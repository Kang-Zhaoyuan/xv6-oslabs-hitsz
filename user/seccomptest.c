#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SECCOMP_SETMASK  0
#define SECCOMP_MAXCHILD 1

int passed = 1;

void check(int ok, char *msg) {
  if (!ok) {
    printf("FAIL: %s\n", msg);
    passed = 0;
  } else {
    printf("PASS: %s\n", msg);
  }
}

int
main(int argc, char *argv[])
{
  printf("seccomp test\n");

  // === Task 1: Syscall Allowlist ===
  printf("\n-- Task 1: Syscall Allowlist --\n");

  int set_zero = seccomp_ctl(SECCOMP_SETMASK, 0);
  int blocked_pid = getpid();
  uint64 zero_log[32];
  int zero_len = 32;
  int read_zero = seccomp_getlog(zero_log, &zero_len);
  int restore_mask = seccomp_ctl(SECCOMP_SETMASK, ~0UL);
  check(set_zero == 0, "seccomp_ctl() accepts an empty allowlist");
  check(blocked_pid == -1, "zero mask blocks ordinary syscalls");
  check(read_zero == 0 && zero_len == 1 && zero_log[0] == 11,
        "seccomp_getlog() bypasses allowlist");
  check(restore_mask == 0, "seccomp_ctl() bypasses allowlist");

  // Only allow: write(16), exit(2), getpid(11), sbrk(12), close(21),
  //            seccomp_ctl(23), seccomp_getlog(24)
  uint64 mask = (1UL << 2) | (1UL << 11) | (1UL << 12) | (1UL << 16)
              | (1UL << 21) | (1UL << 23) | (1UL << 24);
  seccomp_ctl(SECCOMP_SETMASK, mask);

  // Fork should be blocked (SYS_fork = 1, not in mask)
  int pid = fork();
  check(pid < 0, "fork() blocked by seccomp");

  // Open should be blocked (SYS_open = 15, not in mask)
  int fd = open("test.txt", 0);
  check(fd < 0, "open() blocked by seccomp");

  // Write should still work (SYS_write = 16, in mask)
  printf("  write() still works - good\n");

  // === Task 2: Audit Log ===
  printf("\n-- Task 2: Audit Log --\n");

  uint64 logbuf[32];
  int loglen = 32;
  seccomp_getlog(logbuf, &loglen);

  if (loglen > 0) {
    printf("  Audit log has %d blocked syscall(s):\n", loglen);
    for (int i = 0; i < loglen; i++) {
      printf("    [%d] syscall %d blocked\n", i, (int)logbuf[i]);
    }
    check(loglen == 3 && logbuf[0] == 11 && logbuf[1] == 1 && logbuf[2] == 15,
          "audit log contains blocked syscalls in order");
  } else {
    check(0, "audit log should not be empty");
  }

  // Fork inherits the mask, with a fresh audit log.
  uint64 inherited_mask = (1UL << 1) | (1UL << 2) | (1UL << 16);
  seccomp_ctl(SECCOMP_SETMASK, inherited_mask);
  pid = fork();
  if (pid == 0) {
    int child_pid = getpid();
    uint64 child_log[32];
    int child_len = 32;
    int child_read = seccomp_getlog(child_log, &child_len);
    exit(child_pid == -1 && child_read == 0 && child_len == 1 &&
         child_log[0] == 11 ? 0 : 1);
  }
  seccomp_ctl(SECCOMP_SETMASK, ~0UL);
  int child_status = -1;
  int waited = wait(&child_status, 0);
  check(pid > 0 && waited == pid && child_status == 0,
        "child inherits allowlist and its own audit log");

  // === Task 3: Process Resource Limits ===
  printf("\n-- Task 3: Process Resource Limits --\n");

  // Allow every syscall for the resource-limit test.
  seccomp_ctl(SECCOMP_SETMASK, ~0UL);

  // Set max children to 2
  seccomp_ctl(SECCOMP_MAXCHILD, 2);

  // First fork should succeed
  pid = fork();
  if (pid == 0) {
    exit(0);
  }
  check(pid > 0, "first fork() succeeds (within limit)");

  // Wait for first child so child_count resets
  wait(0, 0);

  // Fork two more children to test that limit works
  seccomp_ctl(SECCOMP_MAXCHILD, 2);

  pid = fork();
  if (pid == 0) {
    sleep(5);
    exit(0);
  }
  check(pid > 0, "second fork() succeeds");

  pid = fork();
  if (pid == 0) {
    sleep(5);
    exit(0);
  }
  check(pid > 0, "third fork() succeeds (child_count = 2, limit = 2)");

  // Now child_count = 2, max_children = 2, next fork should fail
  pid = fork();
  check(pid < 0, "fourth fork() blocked by child limit");

  // Wait for remaining children
  wait(0, 0);
  wait(0, 0);

  seccomp_ctl(SECCOMP_MAXCHILD, 1);
  pid = fork();
  if (pid == 0) {
    int first = fork();
    if (first == 0) exit(0);
    int second = fork();
    if (second == 0) exit(1);
    if (first > 0) wait(0, 0);
    if (second > 0) wait(0, 0);
    exit(first > 0 && second == -1 ? 0 : 1);
  }
  child_status = -1;
  waited = wait(&child_status, 0);
  check(pid > 0 && waited == pid && child_status == 0,
        "child inherits max-children limit");

  printf("\n--- %s ---\n", passed ? "seccomp test OK" : "seccomp test FAIL");

  if (passed) {
    printf("seccomp test OK\n");
  } else {
    printf("seccomp test FAIL\n");
  }
  exit(0);
}
