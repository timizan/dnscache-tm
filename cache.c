#include "alloc.h"
#include "byte.h"
#include "uint32.h"
#include "exit.h"
#include "tai.h"
#include "cache.h"
#include "domain_cache.h"
#include "str.h"
#include "uint64.h"

uint64 cache_motion = 0;

static char *x = 0;
static uint32 size;
static uint32 hsize;
static uint32 writer;
static uint32 oldest;
static uint32 unused;

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

static void cache_impossible(void)
{
  _exit(111);
}

static void set4(uint32 pos,uint32 u)
{
  if (pos > size - 4) cache_impossible();
  uint32_pack(x + pos,u);
}

static uint32 get4(uint32 pos)
{
  uint32 result;
  if (pos > size - 4) cache_impossible();
  uint32_unpack(x + pos,&result);
  return result;
}

static unsigned int hash(const char *key,unsigned int keylen)
{
  unsigned int result = 5381;

  while (keylen) {
    result = (result << 5) + result;
    result ^= (unsigned char) *key;
    ++key;
    --keylen;
  }
  result <<= 2;
  result &= hsize - 4;
  return result;
}

char *cache_get(const char *key,unsigned int keylen,unsigned int *datalen,uint32 *ttl)
{
  struct tai expire;
  struct tai now;
  uint32 pos;
  uint32 prevpos;
  uint32 nextpos;
  uint32 u;
  unsigned int loop;
  double d;

  if (!x) return 0;
  if (keylen > MAXKEYLEN) return 0;

  prevpos = hash(key,keylen);
  pos = get4(prevpos);
  loop = 0;

  while (pos) {
    if (get4(pos + 4) == keylen) {
      if (pos + 20 + keylen > size) cache_impossible();
      if (byte_equal(key,keylen,x + pos + 20)) {
        tai_unpack(x + pos + 12,&expire);
        tai_now(&now);
        if (tai_less(&expire,&now)) return 0;

        tai_sub(&expire,&expire,&now);
        d = tai_approx(&expire);
        if (d > 604800) d = 604800;
        *ttl = d;

        u = get4(pos + 8);
        if (u > size - pos - 20 - keylen) cache_impossible();
        *datalen = u;

        return x + pos + 20 + keylen;
      }
    }
    nextpos = prevpos ^ get4(pos);
    prevpos = pos;
    pos = nextpos;
    if (++loop > 100) return 0; /* to protect against hash flooding */
  }

  return 0;
}

void cache_set(const char *key,unsigned int keylen,const char *data,unsigned int datalen,uint32 ttl)
{
  struct tai now;
  struct tai expire;
  unsigned int entrylen;
  unsigned int keyhash;
  uint32 pos;

  if (!x) return;
  if (keylen > MAXKEYLEN) return;
  if (datalen > MAXDATALEN) return;

  if (!ttl) return;
  if (ttl > 604800) ttl = 604800;

  entrylen = keylen + datalen + 20;

  while (writer + entrylen > oldest) {
    if (oldest == unused) {
      if (writer <= hsize) return;
      unused = writer;
      oldest = hsize;
      writer = hsize;
    }

    pos = get4(oldest);
    set4(pos,get4(pos) ^ oldest);
  
    oldest += get4(oldest + 4) + get4(oldest + 8) + 20;
    if (oldest > unused) cache_impossible();
    if (oldest == unused) {
      unused = size;
      oldest = size;
    }
  }

  keyhash = hash(key,keylen);

  tai_now(&now);
  tai_uint(&expire,ttl);
  tai_add(&expire,&expire,&now);

  pos = get4(keyhash);
  if (pos)
    set4(pos,get4(pos) ^ keyhash ^ writer);
  set4(writer,pos ^ keyhash);
  set4(writer + 4,keylen);
  set4(writer + 8,datalen);
  tai_pack(x + writer + 12,&expire);
  byte_copy(x + writer + 20,keylen,key);
  byte_copy(x + writer + 20 + keylen,datalen,data);

  set4(keyhash,writer);
  writer += entrylen;
  cache_motion += entrylen;
}


int cache_del(const char *key,unsigned int keylen)
{
  uint32 prevpos;
  uint32 pos;
  unsigned int keyhash;

  if (!x) return 0;
  if (keylen > MAXKEYLEN) return 0;

  keyhash = hash(key,keylen);
  pos = get4(keyhash);
  if (pos) {
      prevpos = get4(pos) ^ keyhash;
      set4(keyhash, prevpos);
      if (prevpos)  {
          set4(prevpos, get4(prevpos) ^ keyhash ^ pos);
      }
      return 1;
  }
  return 0;
}

int cache_init(unsigned int cachesize)
{
  if (x) {
    alloc_free(x);
    x = 0;
  }

  if (cachesize > 1000000000) cachesize = 1000000000;
  if (cachesize < 100) cachesize = 100;
  size = cachesize;

  hsize = 4;
  while (hsize <= (size >> 5)) hsize <<= 1;

  x = alloc(size);
  if (!x) return 0;
  byte_zero(x,size);

  writer = hsize;
  oldest = size;
  unused = size;

  return 1;
}

/* Cache getter function for Domain entries.
 * The function gets domain name and subdomain from the key
 * looks up the address of the cache of subdomains from top level cache 
 *  - address_of_subdomain_cache <- top_level_cache(domain)
 *  - data <- subdomain_cache(subdomain)
 */
char *cache_get_domain_entry(const char *key,unsigned int keylen,unsigned int *datalen,uint32 *ttl)
{
   char domain[MAXKEYLEN];
   char subdomain[MAXKEYLEN];
   int i;
   int domainlen;
   int subdomainlen;
   char *res;
   uint64 cache_address;
   unsigned int resultlen;
   struct domain_cache *dc;
   char *data = 0;

   i = str_rchr(key,'.');
   if (i < keylen - 1) {
      byte_copy(subdomain,i,key); subdomainlen = i;
      byte_copy(domain, keylen - i -1, key + i + 1); domainlen = keylen -i -1;
   } else {
      subdomainlen = 0;
      byte_copy(domain, i, key); domainlen = i;
   }
   if (subdomainlen > 0) {
       res = cache_get(domain, domainlen, &resultlen, ttl);
       if (res) {
           uint64_unpack(res, &cache_address);
           dc = (struct domain_cache *) cache_address;
           data = domain_cache_get(dc, subdomain, subdomainlen, datalen, ttl);
       }
   }
   return data;
}

/* 
 * Cache setter function for Domain entries.
 * The function gets domain name and subdomain from the key
 * sets subdomain entries in a separate cache.
 * address of the cache is stored in the top level cache 
 *   - top level cache -> (domain, address_of_subdomain_cache)
 *   - subdomain cache -> (subdomain, data)
 */
void cache_set_domain_entry(const char *key,unsigned int keylen,const char *data,unsigned int datalen,uint32 ttl)
{
    char domain[MAXKEYLEN];
    char subdomain[MAXKEYLEN];
    int i;
    int domainlen;
    int subdomainlen;
    char *res;
    uint64 cache_address;
    unsigned int resultlen;
    struct domain_cache *dc;
    char addr_buf[8];
    uint32 d_ttl;

    i = str_rchr(key,'.');
    if (i < keylen - 1) {
      byte_copy(subdomain,i,key); subdomainlen = i;
      byte_copy(domain, keylen - i -1, key + i + 1); domainlen = keylen -i -1;
    } else {
      subdomainlen = 0;
      byte_copy(domain, i, key); domainlen = i;
    }
    res = cache_get(domain, domainlen, &resultlen, &d_ttl);
    if (res) {
       uint64_unpack(res, &cache_address);
       dc = (struct domain_cache *) cache_address;
    } else {
        dc = domain_cache_init(10000);
        cache_address = (uint64) dc;
        uint64_pack(addr_buf, cache_address);
        cache_set(domain, domainlen, addr_buf, 8, ttl);
    }

    if (subdomainlen > 0) {
        domain_cache_set(dc, subdomain, subdomainlen, data, datalen, ttl);
    }

}

/*
 * 
 * Deletes cache holding subdomain entries of a domain
 * subdomain cache address <- top_level_cache(domain)
 */
int cache_del_subdomains(const char *key,unsigned int keylen)
{
    char *res;
    unsigned int resultlen;
    uint32 d_ttl;
    uint64 cache_address;
    struct domain_cache *dc;

    res = cache_get(key, keylen, &resultlen, &d_ttl);
    if (res) {
       uint64_unpack(res, &cache_address);
       dc = (struct domain_cache *) cache_address;
       domain_cache_delete(dc);
       return cache_del(key, keylen); 
    } else {
        return 0;
    }
          
}
