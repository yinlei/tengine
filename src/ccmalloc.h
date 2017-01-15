#ifndef TENGINE_CCMALLOC_H
#define TENGINE_CCMALLOC_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void *ccmalloc(size_t size);

void *cccalloc(size_t size);

void *ccrealloc(void *ptr, size_t size);

void ccfree(void *ptr);

char *ccstrdup(const char *s);

size_t ccmalloc_used_memory();

void *cclalloc(void *ud, void *ptr, size_t osize, size_t nsize);

#ifdef __cplusplus
}
#endif

#endif
