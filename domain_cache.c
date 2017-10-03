#include <stdlib.h>
#include "alloc.h"
#include "byte.h"
#include "uint32.h"
#include "exit.h"
#include "tai.h"
#include "domain_cache.h"

/*
100 <= size <= 1000000000.
4 <= hsize <= size/16.
hsize is a power of 2.

hsize <= writer <= oldest <= unused <= size.
If oldest == unused then unused == size.

x is a hash table with the following structure:
x[0...hsize-1]: hsize/4 head links.
x[hsize...writer-1]: consecutive entries, newest entry on the right.
x[writer...oldest-1]: free space for new entries.
x[oldest...unused-1]: consecutive entries, oldest entry on the left.
x[unused...size-1]: unused.

Each hash bucket is a linked list containing the following items:
the head link, the newest entry, the second-newest entry, etc.
Each link is a 4-byte number giving the xor of
the positions of the adjacent items in the list.

Entries are always inserted immediately after the head and removed at the tail.

Each entry contains the following information:
4-byte link; 4-byte keylen; 4-byte datalen; 8-byte expire time; key; data.
*/

#define MAXKEYLEN 1000
#define MAXDATALEN 1000000

static void domain_cache_impossible(void)
{
  _exit(111);
}

static void domain_set4(struct domain_cache *dc, uint32 pos,uint32 u)
{
  if (pos > (dc->size) - 4) domain_cache_impossible();
  uint32_pack(dc->x + pos,u);
}

static uint32 domain_get4(struct domain_cache *dc, uint32 pos)
{
  uint32 result;
  if (pos > dc->size - 4) domain_cache_impossible();
  uint32_unpack(dc->x + pos,&result);
  return result;
}

static unsigned int domain_hash(struct domain_cache *dc, const char *key,unsigned int keylen)
{
  unsigned int result = 5381;

  while (keylen) {
    result = (result << 5) + result;
    result ^= (unsigned char) *key;
    ++key;
    --keylen;
  }
  result <<= 2;
  result &= dc->hsize - 4;
  return result;
}

char *domain_cache_get(struct domain_cache *dc, const char *key,unsigned int keylen,unsigned int *datalen,uint32 *ttl)
{
  struct tai expire;
  struct tai now;
  uint32 pos;
  uint32 prevpos;
  uint32 nextpos;
  uint32 u;
  unsigned int loop;
  double d;

  if (!dc || !dc->x) return 0;
  if (keylen > MAXKEYLEN) return 0;

  prevpos = domain_hash(dc,key,keylen);
  pos = domain_get4(dc, prevpos);
  loop = 0;

  while (pos) {
    if (domain_get4(dc, pos + 4) == keylen) {
      if (pos + 20 + keylen > dc->size) domain_cache_impossible();
      if (byte_equal(key,keylen,dc->x + pos + 20)) {
        tai_unpack(dc->x + pos + 12,&expire);
        tai_now(&now);
        if (tai_less(&expire,&now)) return 0;

        tai_sub(&expire,&expire,&now);
        d = tai_approx(&expire);
        if (d > 604800) d = 604800;
        *ttl = d;

        u = domain_get4(dc, pos + 8);
        if (u > dc->size - pos - 20 - keylen) domain_cache_impossible();
        *datalen = u;

        return dc->x + pos + 20 + keylen;
      }
    }
    nextpos = prevpos ^ domain_get4(dc, pos);
    prevpos = pos;
    pos = nextpos;
    if (++loop > 100) return 0; /* to protect against hash flooding */
  }

  return 0;
}

void domain_cache_set(struct domain_cache *dc, const char *key,unsigned int keylen,const char *data,unsigned int datalen,uint32 ttl)
{
  struct tai now;
  struct tai expire;
  unsigned int entrylen;
  unsigned int keyhash;
  uint32 pos;

  if (!dc || !dc->x) return;
  if (keylen > MAXKEYLEN) return;
  if (datalen > MAXDATALEN) return;

  if (!ttl) return;
  if (ttl > 604800) ttl = 604800;

  entrylen = keylen + datalen + 20;

  while (dc->writer + entrylen > dc->oldest) {
    if (dc->oldest == dc->unused) {
      if (dc->writer <= dc->hsize) return;
      dc->unused = dc->writer;
      dc->oldest = dc->hsize;
      dc->writer = dc->hsize;
    }

    pos = domain_get4(dc, dc->oldest);
    domain_set4(dc, pos,domain_get4(dc, pos) ^ dc->oldest);
  
    dc->oldest += domain_get4(dc, dc->oldest + 4) + domain_get4(dc, dc->oldest + 8) + 20;
    if (dc->oldest > dc->unused) domain_cache_impossible();
    if (dc->oldest == dc->unused) {
      dc->unused = dc->size;
      dc->oldest = dc->size;
    }
  }

  keyhash = domain_hash(dc, key,keylen);

  tai_now(&now);
  tai_uint(&expire,ttl);
  tai_add(&expire,&expire,&now);

  pos = domain_get4(dc, keyhash);
  if (pos)
    domain_set4(dc, pos,domain_get4(dc, pos) ^ keyhash ^ dc->writer);
  domain_set4(dc, dc->writer,pos ^ keyhash);
  domain_set4(dc, dc->writer + 4,keylen);
  domain_set4(dc, dc->writer + 8,datalen);
  tai_pack(dc->x + dc->writer + 12,&expire);
  byte_copy(dc->x + dc->writer + 20,keylen,key);
  byte_copy(dc->x + dc->writer + 20 + keylen,datalen,data);

  domain_set4(dc,keyhash,dc->writer);
  dc->writer += entrylen;
  dc->cache_motion += entrylen;
}


int domain_cache_del(struct domain_cache *dc, const char *key,unsigned int keylen)
{
  uint32 prevpos;
  uint32 pos;
  unsigned int keyhash;

  if (!dc || !dc->x) return 0;
  if (keylen > MAXKEYLEN) return 0;

  keyhash = domain_hash(dc, key,keylen);
  pos = domain_get4(dc, keyhash);
  if (pos) {
      prevpos = domain_get4(dc, pos) ^ keyhash;
      domain_set4(dc, keyhash, prevpos);
      if (prevpos)  {
          domain_set4(dc, prevpos, domain_get4(dc, prevpos) ^ keyhash ^ pos);
      }
      return 1;
  }
  return 0;
}
struct domain_cache *domain_cache_init(unsigned int cachesize)
{
 
  struct domain_cache *dc = 0;
  dc = (struct domain_cache *) malloc(sizeof(struct domain_cache)); 
  if (!dc) return 0;

  if (cachesize > 1000000000) cachesize = 1000000000;
  if (cachesize < 100) cachesize = 100;
  dc->size = cachesize;

  dc->hsize = 4;
  while (dc->hsize <= (dc->size >> 5)) dc->hsize <<= 1;

  dc->x = alloc(dc->size);
  if (!dc->x) return 0;
  byte_zero(dc->x,dc->size);

  dc->writer = dc->hsize;
  dc->oldest = dc->size;
  dc->unused = dc->size;
  dc->cache_motion = 0;

  return dc;
}

void domain_cache_delete(struct domain_cache *dc)
{
   if (dc && dc->x) {
      alloc_free(dc->x);
      free(dc);
   }
}
