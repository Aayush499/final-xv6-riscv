#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int priority, pid;
  if(argc < 3){
    fprintf(2,"Usage: nice pid priority\n");
    exit(1);
  }

  priority = atoi(argv[1]);
  pid = atoi(argv[2]);

  if (priority < 0 || priority > 100){
    fprintf(2,"Invalid priority (0-100)!\n");
    exit(1);
  }
  set_priority(priority, pid);
  exit(1);
}