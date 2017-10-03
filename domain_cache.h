#ifndef DOMAIN_CACHE_H
#define DOMAIN_CACHE_H

#include "uint32.h"
#include "uint64.h"

struct domain_cache {
    uint64 cache_motion;
    char *x;
    uint32 size;
    uint32 hsize;
    uint32 writer;
    uint32 oldest;
    uint32 unused;
};

extern struct domain_cache *domain_cache_init(unsigned int);
extern void domain_cache_set(struct domain_cache *,const char *,unsigned int,const char *,unsigned int,uint32);
extern char *domain_cache_get(struct domain_cache *,const char *,unsigned int,unsigned int *,uint32 *);
extern void domain_cache_delete(struct domain_cache *);
#endif
