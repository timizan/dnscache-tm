#include <sys/types.h>
#include <sys/stat.h>
#include "str.h"
#include "ip4.h"
#include "byte.h"
#include "cache.h"
#include "okclient.h"

#define MAX_TTL 604800

static char fn[3 + IP4_FMT];
static char key[3 + IP4_FMT];

int okclient(char ip[4])
{
  struct stat st;
  int i;
  int iplen;
  char *cached;
  unsigned int cachedlen;
  uint32 ttl;
  char data[2];

  fn[0] = 'i';
  fn[1] = 'p';
  fn[2] = '/';
  iplen = ip4_fmt(fn + 3,ip);
  fn[3 + iplen] = 0;
  byte_copy(key, 3 + iplen, fn);
  /* check cache first and return success hit */
  cached = cache_get(key, 3 + iplen, &cachedlen, &ttl);
  if (cached) {
     return 1;
  }
  for (;;) {
    if (stat(fn,&st) == 0) {
        /* add to cache */
        data[0] = '1';
        data[1] = 0;
        cache_set(key, 3 + iplen, data, 1, MAX_TTL);
        return 1;
    }
    /* treat temporary error as rejection */
    i = str_rchr(fn,'.');
    if (!fn[i]) return 0;
    fn[i] = 0;
  }
}
