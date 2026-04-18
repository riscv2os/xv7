#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int fd;
  char buf[256];
  int n;
  int line_num = 1;
  int i;
  
  fd = open(".sh_history", O_RDONLY);
  if(fd < 0){
    write(1, "No command history found.\n", 26);
    return 0;
  }
  
  while((n = read(fd, buf, sizeof(buf) - 1)) > 0){
    buf[n] = 0;
    i = 0;
    while(i < n){
      int start = i;
      while(i < n && buf[i] != '\n' && buf[i] != 0){
        i++;
      }
      if(i > start){
        buf[i] = 0;
        if(line_num < 10) write(1, " ", 1);
        if(line_num < 100) write(1, " ", 1);
        write(1, "   ", 3);
        write(1, "  ", 2);
        write(1, buf + start, i - start);
        write(1, "\n", 1);
        line_num++;
      }
      i++;
    }
  }
  close(fd);
  return 0;
}
