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

int state;

void
get_progress(void)
{
  int fd;
  fd = open("progress.txt", O_RDONLY);
  if (fd < 0) {
    //nothing is created yet
    state = 1;
    fd = open("progress.txt", O_CREATE | O_RDWR);
    write(fd, &state, sizeof(int));
    close(fd);
  } else {
    read(fd, &state, sizeof(int));
    close(fd);

    if (state > 1000)
      error("too many steps before operating is complete");

    state ++;
    fd = open("progress.txt", O_RDWR);
    write(fd, &state, sizeof(int));
    close(fd);
  }
}

void
check1(void)
{
  int fd;
  int i,j;
  struct stat st;

  fd = open("big.txt", O_RDONLY);
  if (fd < 0)
    return;
  fstat(fd, &st);

  // check if it is a zero-size file
  if (st.size == 0)
    return;

  if (st.size % 512 != 0)
    error("write is in-complete, file system not in consistent state!");

  // check if the size of the file is correct
  int progress = st.size / 512;
  if (progress > 3)
    error("write is incorrect, file system not in consistent state!");

  memset(buf, 0, sizeof(buf));
  read(fd, buf, 3 * 512);
  for (i = 0; i < progress; i++) {
    for (j = 0; j < 512; j++ ){
      if (buf[i * 512 + j] != 'a' + i)
        error("file system not in consistent state!, i = %d, j = %d, content = %d", i, j, buf[i*512+j]);
    }
  }
  close(fd);

  if (progress == 3) {
    printf(stdout, "big.txt is completely written\n");
    printf(stdout, "lab5test_b passed!\n");
    exit();
  }
}

void
check2(void)
{
  int fd;
  int i,j;
  struct stat st;

  fd = open("big.txt", O_RDONLY);
  if (fd < 0)
    error("file is not generated");

  fstat(fd, &st);

  // check if the size of the file is correct
  if (st.size != 512 * 3)
    error("write is in-complete, file system not in consistent state!");
  memset(buf, 0, sizeof(buf));
  read(fd, buf, 3 * 512);
  for (i = 0; i < 3; i++) {
    for (j = 0; j < 512; j++ ){
      if (buf[i * 512 + j] != 'a' + i)
        error("file system not in consistent state!");
    }
  }
  close(fd);
  return;
}

void
crashsafe(int steps)
{
  int fd;
  crashn(steps);
  printf(stdout, "crash after %d steps\n", steps);
  fd = open("big.txt", O_CREATE | O_RDWR);
  memset(buf, 'a', 512);
  memset(buf + 512 * 1, 'b', 512);
  memset(buf + 512 * 2, 'c', 512);
  write(fd, buf, 512);
  write(fd, buf + 512, 512);
  write(fd, buf + 1024, 512);
  close(fd);
}

int
main(int argc, char *argv[])
{
  printf(stdout, "lab5test_b starting\n");
  check1();
  get_progress();
  crashsafe(state);
  check2();
  printf(stdout, "lab5test_b passed!\n");
  exit();
}
