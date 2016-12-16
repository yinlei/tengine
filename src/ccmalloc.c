#include "ccmalloc.h"

#if defined(USE_JEMALLOC)
#include "jemalloc/jemalloc.h"
#elif defined(USE_TCMALLOC)
#include "google/tcmalloc.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX_SIZE (sizeof(size_t))

#if defined(USE_JEMALLOC)
#define malloc(size) je_malloc(size)
#define calloc(count, size) je_calloc(count, size)
#define realloc(ptr, size) je_realloc(ptr, size)
#define free(ptr) je_free(ptr)
#elif defined(USE_TCMALLOC)
#define malloc(size) tc_malloc(size)
#define calloc(count, size) tc_calloc(count, size)
#define realloc(ptr, size) tc_realloc(ptr, size)
#define free(ptr) tc_free(ptr)
#endif

#ifdef _WIN32
#include <Windows.h>
#define update_ccmalloc_stat_add(__n) (InterlockedExchangeAdd((long *)&__used_memory, (long)__n))
#define update_ccmalloc_stat_sub(__n) (InterlockedExchangeAdd((long *)&__used_memory, (long)-((size_t)__n)))
#else
#define update_ccmalloc_stat_add(__n) __sync_add_and_fetch(&__used_memory, (__n))
#define update_ccmalloc_stat_sub(__n) __sync_sub_and_fetch(&__used_memory, (__n))
#endif

static size_t __used_memory = 0;


static void update_ccmalloc_stat_alloc(size_t n)
{
    size_t _n = n;
    if (_n&(sizeof(long)-1))
        _n += sizeof(long) - (_n&sizeof(long)-1);
	update_ccmalloc_stat_add(_n);
}

static void update_ccmalloc_stat_free(size_t n)
{
    size_t _n = n;
    if (_n&(sizeof(long)-1))
        _n += sizeof(long) - (_n&sizeof(long)-1);
	update_ccmalloc_stat_sub(_n);
}

static void ccmalloc_oom(size_t size)
{
#ifdef _WIN32
    fprintf(stderr, "ccmalloc: out of memory try to allocate %llu bytes",
        (unsigned long long)size);
#else
    fprintf(stderr, "ccmalloc: out of memory try to allocate %zu bytes", size);
#endif
    fflush(stderr);
    abort();
}

void *ccmalloc(size_t size)
{
    void *ptr = malloc(size+PREFIX_SIZE);
    if (!ptr)
        ccmalloc_oom(size);

    *((size_t*)ptr) = size;
    update_ccmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char *)ptr + PREFIX_SIZE;
}

void *cccalloc(size_t size)
{
    void *ptr = calloc(1, size+PREFIX_SIZE);
    if (!ptr)
        ccmalloc_oom(size);

    *((size_t*)ptr) = size;
	update_ccmalloc_stat_alloc(size + PREFIX_SIZE);
    return (char*)ptr + PREFIX_SIZE;
}

void *ccrealloc(void *ptr, size_t size)
{
    void *realptr;
    size_t oldsize;
    void *newptr;

    if (NULL == ptr)
        return ccmalloc(size);

    realptr = (char*)ptr - PREFIX_SIZE;
    oldsize = *((size_t*)realptr);

    newptr = realloc(realptr, size + PREFIX_SIZE);
    if (!newptr)
        ccmalloc_oom(size);

    *((size_t*)newptr) = size;
	update_ccmalloc_stat_free(oldsize);
    update_ccmalloc_stat_alloc(size);

    return (char*)newptr + PREFIX_SIZE;
}

void ccfree(void *ptr)
{
    void *realptr;
    size_t oldsize;

    if (NULL == ptr)
        return;

    realptr = (char*)ptr - PREFIX_SIZE;
    oldsize = *((size_t*)realptr);

	update_ccmalloc_stat_free(oldsize + PREFIX_SIZE);

	free(realptr);
}


char *ccstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = ccmalloc(len);
    memcpy(p, s, len);
    return p;
}

size_t ccmalloc_used_memory()
{
    return __used_memory;
}


void *cclalloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    if (nsize == 0)
    {
        ccfree(ptr);
        return NULL;
    }
    else
        return ccrealloc(ptr, nsize);
}
