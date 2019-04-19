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

void smallfilereadtest(void);
void testinvalidargs(void);
void testmaxfiles(void);
void pipetest(void);

int main() {
  if(open("console", O_RDWR) < 0){
    return -1;
  }
  dup(0);  // stdout
  dup(0);  // stderr

  printf(1, "hello world\n");
  //while(1);

  smallfilereadtest();
  testinvalidargs();
  pipetest();

  printf(stdout, "lab1 tests passed!\n");

  exit();
  return 0;
}

void
smallfilereadtest(void)
{
  int fd;
  int i;
  char buf[11];

  printf(stdout, "small file test\n");
  // Test read only funcionality
  fd = open("/small.txt", O_RDONLY);
  if(fd < 0)
    error("unable to open small file");

  printf(stdout, "open small file succeeded; ok\n");

  if ((i = read(fd, buf, 10)) != 10)
    error("read of first 10 bytes unsucessful was %s bytes", "6");

  buf[10] = 0;
  if (strcmp(buf, "aaaaaaaaaa") != 0)
    error("buf was not 10 a's, was: '%s'", buf);
  printf(stdout, "read of first 10 bytes sucessful\n");

  if ((i = read(fd, buf, 10)) != 10)
    error("read of second 10 bytes unsucessful was %d bytes", i);

  buf[10] = 0;
  if (strcmp(buf, "bbbbbbbbbb") != 0)
    error("buf was not 10 b's, was: '%s'", buf);
  printf(stdout, "read of second 10 bytes sucessful\n");

  // only 25 byte file
  if ((i = read(fd, buf, 10)) != 6)
    error("read of last 6 bytes unsucessful was %d bytes", i);

  buf[6] = 0;
  if (strcmp(buf, "ccccc\n") != 0)
    error("buf was not 5 c's (and a newline), was: '%s'", buf);

  printf(stdout, "read of last 5 bytes sucessful\n");

  if (read(fd, buf, 10) != 0)
    error("read more bytes than should be possible");

  if (close(fd) != 0)
    error("error closing fd");
  printf(stdout, "small file test ok\n");
}

void
testinvalidargs(void)
{
  int fd;
  int i;
  char buf[11];
  // open
  if (open("/other.txt", O_CREATE) != -1)
    error("created file in read only file system");

  if (open("/small.txt", O_RDWR) != -1 || open("/small.txt", O_WRONLY) != -1)
    error("tried to open a file for writing in read only fs");

  if (open("/other.txt", O_RDONLY) != -1)
    error("opened a file that doesn't exist");

  printf(stdout, "passed argument checking for open\n");

  // read
  if (read(15, buf, 11) != -1)
    error("read on a non existent file descriptor");

  fd = open("/small.txt", O_RDONLY);

  if ((i = read(fd, buf, -100)) != -1)
    error("negative n didn't return error was %d", i);

  if (read(fd, (char *)0xffffff00, 10) != -1)
    error("able to read to a buffer not in my memory region");

  printf(stdout, "passed argument checking for read\n");

  // write
  if (write(15, buf, 11) != -1)
    error("write on a non existent file descriptor");

  if (write(fd, buf, 11) != -1)
    error("able to write to a file in read only fs");

  printf(stdout, "passed argument checking for write\n");

  // dup
  if (dup(15) != -1)
    error("able to duplicated a non open file");

  printf(stdout, "passed argument checking for dup\n");

  // close
  if (close(15) != -1)
    error("able to close non open file");

  if (close(fd) > 0 && close(fd) != -1)
    error("able to close same file twice");

  printf(stdout, "passed argument checking for close\n");

  // pipe
  if (pipe((int *)0xffffff00) != -1)
    error("able to alloc a pipe not in my memory region");

  printf(stdout, "passed argument checking for pipe\n");
}

void
testmaxfiles(void)
{
}

void
pipetest(void)
{
  char buf[11];
  int pfds[2];
  int i;
  pipe(pfds);

  if(write(pfds[1], "aaaaaaaaaa", 10) != 10)
    error("unable to write to pipe");

  if ((i = read(pfds[0], buf, 10)) != 10)
    error("didn't read 10 bytes, only read %d bytes", i);

  buf[10] = 0;
  if (strcmp(buf, "aaaaaaaaaa") != 0)
    error("buf wasn't 10 a's, was '%s'", buf);

  printf(stdout, "read succeeded ok\n");

  if (close(pfds[0]) < 0 || close(pfds[1]) < 0)
    error("closing the files in the pipe");

  printf(stdout, "pipe test ok\n");
}
