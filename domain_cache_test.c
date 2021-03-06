#include "buffer.h"
#include "exit.h"
#include "cache.h"
#include "cache.h"
#include "str.h"
#include "uint64.h"

int main(int argc,char **argv)
{
  int i;
  int j;
  char *x;
  char *y;
  unsigned int u;
  uint32 ttl;
  uint64 len;
  char addr_buf[8];
  uint64 res;

  if (!cache_init(200)) _exit(111);

  if (*argv) ++argv;

  while (x = *argv++) {
    i = str_chr(x,':');
    j = str_chr(x,'-');
    if (x[i])
      cache_set_domain_entry(x,i,x + i + 1,str_len(x) - i - 1,86400);
    else if (x[j]) {
      cache_del_subdomains(x,j);
    }
    else {
      y = cache_get_domain_entry(x,i,&u,&ttl);
      if (y)
        buffer_put(buffer_1,y,u);
      buffer_puts(buffer_1,"\n");
    }
  }
  buffer_flush(buffer_1);
  _exit(0);
}
