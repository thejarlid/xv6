#include <cdefs.h>
#include <stat.h>
#include <user.h>
#include <stdarg.h>
#include <fcntl.h>

#define error(msg, ...) \
  do { \
    printf(stdout, "ERROR (line %d): ", __LINE__); \
    printf(stdout, msg,  ##__VA_ARGS__); \
    printf(stdout, "\n"); \
    while(1) {} \
  } while (0)

int stdout = 1;

void forktest(void);
void pipetest(void);
void pkilltest(void);
void exectest(void);

int main() {
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    return -1;
  }
  dup(0);  // stdout
  dup(0);  // stderr

  pid = fork();
  if(pid < 0){
    printf(1, "fork failed\n");
    exit();
  }

  if(pid == 0){
    forktest();
    pipetest();
    exectest();

    printf(1, "lab2 tests passed!!\n");

    while(1);
  }

  while((wpid=wait()) >= 0 && wpid != pid)
    printf(1, "zombie!\n");

  exit();
  return 0;
}

void
forktest(void)
{
  int n, pid;
  int nproc = 6;

  printf(1, "forktest\n");

  for(n=0; n<nproc; n++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
      exit();
  }

  if(n != nproc){
    error("forktest: fork claimed to work %d times! but only %d\n", nproc, n);
  }

  for(; n > 0; n--){
    if(wait() < 0){
      error("forktest: wait stopped early\n");
    }
  }

  if(wait() != -1){
    error("forktest: wait got too many\n");
  }

  printf(1, "forktest: fork test OK\n");
}

void
pipetest(void)
{
  char buf[500];
  int fds[2], pid;
  int seq, i, n, cc, total;

  if(pipe(fds) != 0){
    error("pipetest: pipe() failed\n");
  }
  pid = fork();
  seq = 0;
  if(pid == 0){
    close(fds[0]);
    for(n = 0; n < 5; n++){
      for(i = 0; i < 95; i++)
        buf[i] = seq++;
      if(write(fds[1], buf, 95) != 95){
        error("pipetest: oops 1\n");
      }
    }
    exit();
  } else if(pid > 0){
    close(fds[1]);
    total = 0;
    cc = 1;
    while((n = read(fds[0], buf, cc)) > 0){
      for(i = 0; i < n; i++){
        if((buf[i] & 0xff) != (seq++ & 0xff)){
          error("pipetest: oops 2\n");
        }
      }
      total += n;
      cc = cc * 2;
      if(cc > sizeof(buf))
        cc = sizeof(buf);
    }
    if(total != 5 * 95){
      error("pipetest: oops 3 total %d\n", total);
    }
    close(fds[0]);
    wait();
  } else {
    error("pipetest: fork() failed\n");
  }
  printf(1, "pipetest ok\n");
}

void
pkilltest(void)
{
  char buf[11];

  int pid1, pid2, pid3;
  int pfds[2];

  printf(1, "preempt: ");
  pid1 = fork();
  if(pid1 == 0)
    for(;;)
      ;

  pid2 = fork();
  if(pid2 == 0)
    for(;;)
      ;

  pipe(pfds);
  pid3 = fork();
  if(pid3 == 0){
    close(pfds[0]);
    if(write(pfds[1], "x", 1) != 1)
      error("pkilltest: write error");
    close(pfds[1]);
    for(;;)
      ;
  }

  close(pfds[1]);
  if(read(pfds[0], buf, sizeof(buf)) != 1){
    error("pkilltest: read error");
  }
  close(pfds[0]);
  printf(1, "kill... ");
  kill(pid1);
  kill(pid2);
  kill(pid3);
  printf(1, "wait... ");
  wait();
  wait();
  wait();
  printf(1, "pkilltest: ok\n");
}

void
racetest(void)
{
  int i, pid;

  for(i = 0; i < 100; i++){
    pid = fork();
    if(pid < 0){
      error("racetest: fork failed\n");
    }
    if(pid){
      if(wait() != pid){
        error("racetest: wait wrong pid\n");
      }
    } else {
      exit();
    }
  }
  printf(1, "racetest ok\n");
}

void
exectest(void)
{
  printf(1, "exectest: starting ls\n");
  int pid = fork();
  if(pid < 0){
    error("exectest: fork failed\n");
  }

  char *argv[] = {0};

  if(pid == 0){
    exec("ls", argv);
    error("exectest: exec ls failed\n");
  } else {
    pid = wait();
  }

  char *echoargv[] = { "echo", "echotest", "ok", 0 };
  printf(stdout, "exectest: test argument\n");

  pid = fork();
  if(pid < 0){
    error("exectest: fork failed\n");
  }
  if(pid == 0){
    exec("echo", echoargv);
    error("exectest: exec echo failed\n");
  } else {
    pid = wait();
  }
}
