#include <cdefs.h>
#include <stat.h>
#include <user.h>
#include <fs.h>
#include <fcntl.h>
#include <memlayout.h>
#include <sysinfo.h>

int stdout = 1;

#define error(msg, ...) \
  do { \
    printf(stdout, "ERROR (line %d): ", __LINE__); \
    printf(stdout, msg,  ##__VA_ARGS__); \
    printf(stdout, "\n"); \
    while(1) {} \
  } while (0)

void
swaptest(void)
{
  char *start = sbrk(0);
  char *a;
  int i;
  int b = 4096;
  int num_pages_to_alloc = 6000;
  struct sys_info info1, info2, info3;

  for(i = 0; i < num_pages_to_alloc; i++){
    a = sbrk(b);
    if (a == (char*)-1) {
      printf(stdout, "no more memory\n");
      break;
    }
    memset(a, 0, b);
    *(int*)a = i;
    printf(stdout, "%d pages allocated\n");
  }

  sysinfo(&info1);

  // check whether memory data is consistent
  for(i = 0; i < num_pages_to_alloc; i++){
    printf(stdout, "%d\n", i);
    if (*(int*)(start + i*b) != i) {
      error("data is incorrect, should be %d, but %d\n", i, *(int*)(start + i*b));
    }
  }

  sysinfo(&info2);

  printf(stdout, "number of disk reads = %d\n", info2.num_disk_reads - info1.num_disk_reads);

  for(i = 0; i < num_pages_to_alloc; i++){
    sbrk(-b);
  }

  sysinfo(&info3);
  printf(stdout, "number of pages in swap = %d\n", info3.pages_in_swap);

  printf(stdout, "swaptest OK\n");
}

void
localitytest(void)
{
  char *start = sbrk(0);
  char *a;
  int i,j,k;
  int b = 4096;
  int groups = 6;
  int pages_per_group = 1000;
  struct sys_info info1, info2;

  for(i = 0; i < groups * pages_per_group; i++){
    a = sbrk(b);
    memset(a, 0, b);
    *(int*)a = i;
  }

  printf(stdout, "%d pages allocated\n", groups*pages_per_group);

  sysinfo(&info1);

  // test whether LRU is implemented
  for(i = 0; i < groups; i++){
    for (j = i; j < groups; j++) {
      for (k = pages_per_group-1; k >=0 ; k--) {
        if (*(int*)(start + (j*pages_per_group + k)*b) != j*pages_per_group + k) {
          error("data is incorrect");
        }
      }
    }
  }

  sysinfo(&info2);

  // if LRU is implemented,
  // this will reduce the number of disk reads to less than 230000

  // If LRU is not implemented, assuming page swap at every memory access
  // Number of disk operations is around (6000 + 5000 + 4000) * 8 * 2 = 240000

  // If LRU is implemented, the first ~4000 pages should not incur disk operations
  // Number of disk operations is around (2000 + 5000 + 4000) * 8 * 2 = 176000

  // we set threshold to be 230000 so any LRU-like implementation can pass our test

  printf(stdout, "number of disk reads = %d\n", info2.num_disk_reads - info1.num_disk_reads);
  if (info2.num_disk_reads - info1.num_disk_reads > 230000)
    error("LRU function does not exist!");

  for(i = 0; i < groups * pages_per_group; i++){
    sbrk(-b);
  }

  printf(stdout, "localitytest OK\n");
}

int
main(int argc, char *argv[])
{
  swaptest();
  localitytest();
  printf(stdout, "lab4 tests passed!!\n");
  exit();
}
