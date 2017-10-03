#ifndef CACHE_H
#define CACHE_H

#include "uint32.h"
#include "uint64.h"

extern uint64 cache_motion;
extern int cache_init(unsigned int);
extern void cache_set(const char *,unsigned int,const char *,unsigned int,uint32);
extern char *cache_get(const char *,unsigned int,unsigned int *,uint32 *);
extern void cache_set_domain_entry(const char *,unsigned int,const char *,unsigned int,uint32); /* function that sets cache entries of all subdomains  of a domain in a separache cache */
extern char *cache_get_domain_entry(const char *,unsigned int,unsigned int *,uint32 *); /* function to  get entry from a separate cahce for all subdomains of a  domain */
extern int cache_del_subdomains(const char *,unsigned int); /* function to delete all cache entries for subdomains of a domain */
#endif
