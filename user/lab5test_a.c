#include <param.h>
#include <cdefs.h>
#include <stat.h>
#include <user.h>
#include <fs.h>
#include <fcntl.h>
#include <syscall.h>
#include <trap.h>
#include <memlayout.h>

char buf[8192];
int stdout = 1;

#define error(msg, ...) do { \
  printf(stdout, "ERROR (line %d): ", __LINE__); \
  printf(stdout, msg,  ##__VA_ARGS__); \
  printf(stdout, "\n"); \
  while(1) {};} while(0)

void
modification(void) {
  int fd;

  printf(stdout, "modification test starting\n");
  strcpy(buf, "lab5 is 451's last lab.\n");
  fd = open("small.txt", O_RDWR);
  write(fd, buf, 50);
  close(fd);

  fd = open("small.txt", O_RDONLY);
  read(fd, buf, 50);

  if (strcmp(buf, "lab5 is 451's last lab.\n") != 0)
    error("file content was not lab5 is 451's last lab., was: '%s'", buf);

  close(fd);

  printf(stdout, "modification test ok!\n");
}

// four processes write different files at the same
// time, to test block allocation.
void
fourfiles(void)
{
  int fd, pid, i, j, n, total, pi;
  char *names[] = { "f0", "f1", "f2", "f3" };
  char *fname;

  printf(1, "fourfiles test\n");

  for(pi = 0; pi < 4; pi++){
    fname = names[pi];

    pid = fork();
    if(pid < 0){
      error("fork failed\n");
    }

    if(pid == 0){
      fd = open(fname, O_CREATE | O_RDWR);
      if(fd < 0){
        error("create failed\n");
      }

      memset(buf, '0'+pi, 512);
      for(i = 0; i < 12; i++){
        if((n = write(fd, buf, 500)) != 500){
          error("write failed %d\n", n);
        }
      }
      exit();
    }
  }

  for(pi = 0; pi < 4; pi++){
    wait();
  }

  for(i = 0; i < 2; i++){
    fname = names[i];
    fd = open(fname, 0);
    total = 0;
    while((n = read(fd, buf, sizeof(buf))) > 0){
      for(j = 0; j < n; j++){
        if(buf[j] != '0'+i){
          error("wrong char\n");
        }
      }
      total += n;
    }
    close(fd);
    if(total != 12*500){
      error("wrong length %d\n", total);
    }
  }

  printf(1, "fourfiles ok\n");
}

int
main(int argc, char *argv[])
{
  printf(stdout, "lab5test_a starting\n");
  modification();
  fourfiles();

  printf(stdout, "lab5test_a passed!\n");
  exit();
}
