/* Alternative malloc implementation for multiple threads without
lock contention based on dlmalloc. (C) 2005-2009 Niall Douglas

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#ifdef _MSC_VER
/* Enable full aliasing on MSVC */
/*#pragma optimize("a", on)*/
#pragma warning(push)
#pragma warning(disable:4100)	/* unreferenced formal parameter */
#pragma warning(disable:4127)	/* conditional expression is constant */
#pragma warning(disable:4706)	/* assignment within conditional expression */
#endif

/*#define FULLSANITYCHECKS*/
#define USE_ALLOCATOR 1
#define REPLACE_SYSTEM_ALLOCATOR 1
#define USE_MAGIC_HEADERS 0
#define MAXTHREADSINPOOL 1
#define FINEGRAINEDBINS 1
#define ENABLE_LARGE_PAGES

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) x=x
#endif

#include "nedmalloc.h"
#ifdef _WIN32
 #include <malloc.h>
 #include <stddef.h>
#endif
#if USE_ALLOCATOR==1
 #define MSPACES 1
 #define ONLY_MSPACES 1
#endif
#define USE_DL_PREFIX 1
#ifndef USE_LOCKS
 #define USE_LOCKS 1
#endif
#define FOOTERS 1           /* Need to enable footers so frees lock the right mspace */
#if defined(DEBUG) && !defined(_DEBUG)
 #define _DEBUG
#elif !defined(NDEBUG) && !defined(DEBUG) && !defined(_DEBUG)
 #define NDEBUG
#endif
#undef DEBUG				/* dlmalloc wants DEBUG either 0 or 1 */
#ifdef _DEBUG
 #define DEBUG 1
#else
 #define DEBUG 0
#endif
#ifdef NDEBUG               /* Disable assert checking on release builds */
 #undef DEBUG
#endif
/* The default of 64Kb means we spend too much time kernel-side */
#ifndef DEFAULT_GRANULARITY
#define DEFAULT_GRANULARITY (1*1024*1024)
#if DEBUG
#define DEFAULT_GRANULARITY_ALIGNED
#endif
#endif
/*#define USE_SPIN_LOCKS 0*/


/*#define FORCEINLINE*/
#include "malloc.c.h"
#ifdef NDEBUG               /* Disable assert checking on release builds */
 #undef DEBUG
#endif
#if defined(__GNUC__) && defined(DEBUG)
#warning DEBUG is defined so allocator will run with assert checking! Define NDEBUG to run at full speed.
#endif

/* The maximum concurrent threads in a pool possible */
#ifndef MAXTHREADSINPOOL
#define MAXTHREADSINPOOL 16
#endif
/* The maximum number of threadcaches which can be allocated */
#ifndef THREADCACHEMAXCACHES
#define THREADCACHEMAXCACHES 256
#endif
/* The maximum size to be allocated from the thread cache */
#ifndef THREADCACHEMAX
#define THREADCACHEMAX 32768
#endif
#ifdef FINEGRAINEDBINS
/* The number of cache entries for finer grained bins. This is (topbitpos(THREADCACHEMAX)-4)*2 */
#define THREADCACHEMAXBINS ((15-4)*2)
#else
/* The number of cache entries. This is (topbitpos(THREADCACHEMAX)-4) */
#define THREADCACHEMAXBINS (15-4)
#endif
/* Point at which the free space in a thread cache is garbage collected */
#ifndef THREADCACHEMAXFREESPACE
#define THREADCACHEMAXFREESPACE (512*1024*4)
#endif

#ifdef _WIN32
 #define TLSVAR			DWORD
 #define TLSALLOC(k)	(*(k)=TlsAlloc(), TLS_OUT_OF_INDEXES==*(k))
 #define TLSFREE(k)		(!TlsFree(k))
 #define TLSGET(k)		TlsGetValue(k)
 #define TLSSET(k, a)	(!TlsSetValue(k, a))
 #ifdef DEBUG
static LPVOID ChkedTlsGetValue(DWORD idx)
{
	LPVOID ret=TlsGetValue(idx);
	assert(S_OK==GetLastError());
	return ret;
}
  #undef TLSGET
  #define TLSGET(k) ChkedTlsGetValue(k)
 #endif
#else
 #define TLSVAR			pthread_key_t
 #define TLSALLOC(k)	pthread_key_create(k, 0)
 #define TLSFREE(k)		pthread_key_delete(k)
 #define TLSGET(k)		pthread_getspecific(k)
 #define TLSSET(k, a)	pthread_setspecific(k, a)
#endif

#if defined(__cplusplus)
#if !defined(NO_NED_NAMESPACE)
namespace nedalloc {
#else
extern "C" {
#endif
#endif

#if USE_ALLOCATOR==0
static void *unsupported_operation(const char *opname) THROWSPEC
{
	fprintf(stderr, "nedmalloc: The operation %s is not supported under this build configuration\n", opname);
	abort();
	return 0;
}
static size_t mspacecounter=(size_t) 0xdeadbeef;
#endif

static FORCEINLINE void *CallMalloc(void *mspace, size_t size, size_t alignment) THROWSPEC
{
	void *ret=0;
    UNREFERENCED_PARAMETER(alignment);
#if USE_MAGIC_HEADERS
	size_t *_ret=0;
	size+=alignment+3*sizeof(size_t);
#endif
#if USE_ALLOCATOR==0
	ret=malloc(size);
#elif USE_ALLOCATOR==1
	ret=mspace_malloc((mstate) mspace, size);
#endif
	if(!ret) return 0;
#if USE_MAGIC_HEADERS
	_ret=(size_t *) ret;
	ret=(void *)(_ret+3);
	if(alignment) ret=(void *)(((size_t) ret+alignment-1)&~(alignment-1));
	for(; _ret<(size_t *)ret-2; _ret++) *_ret=*(size_t *)"NEDMALOC";
	_ret[0]=(size_t) mspace;
	_ret[1]=size;
#endif
	return ret;
}

static FORCEINLINE void *CallCalloc(void *mspace, size_t no, size_t size, size_t alignment) THROWSPEC
{
	void *ret=0;
    UNREFERENCED_PARAMETER(alignment);
#if USE_MAGIC_HEADERS
	size_t *_ret=0;
	size+=alignment+3*sizeof(size_t);
#endif
#if USE_ALLOCATOR==0
	ret=calloc(no, size);
#elif USE_ALLOCATOR==1
	ret=mspace_calloc((mstate) mspace, no, size);
#endif
	if(!ret) return 0;
#if USE_MAGIC_HEADERS
	_ret=(size_t *) ret;
	ret=(void *)(_ret+3);
	if(alignment) ret=(void *)(((size_t) ret+alignment-1)&~(alignment-1));
	for(; _ret<(size_t *)ret-2; _ret++) *_ret=*(size_t *) "NEDMALOC";
	_ret[0]=(size_t) mspace;
	_ret[1]=size;
#endif
	return ret;
}

static FORCEINLINE void *CallRealloc(void *mspace, void *mem, size_t size) THROWSPEC
{
	void *ret=0;
#if USE_MAGIC_HEADERS
	mstate oldmspace=0;
	size_t *_ret=0, *_mem=(size_t *) mem-3, oldsize=0;
	if(_mem[0]!=*(size_t *) "NEDMALOC")
	{	/* Transfer */
		if((ret=CallMalloc(mspace, size, 0)))
		{	/* It's probably safe to copy size bytes from mem - can't do much different */
#if defined(DEBUG)
			printf("*** nedmalloc frees system allocated block %p\n", mem);
#endif
			memcpy(ret, mem, size);
			free(mem);
		}
		return ret;
	}
	size+=3*sizeof(size_t);
	oldmspace=(mstate) _mem[1];
	oldsize=_mem[2];
	for(; *_mem==*(size_t *) "NEDMALOC"; *_mem--=0);
	mem=(void *)(++_mem);
#endif
#if USE_ALLOCATOR==0
	ret=realloc(mem, size);
#elif USE_ALLOCATOR==1
	ret=mspace_realloc((mstate) mspace, mem, size);
#endif
	if(!ret)
	{	/* Put it back the way it was */
#if USE_MAGIC_HEADERS
		for(; *_mem==0; *_mem++=*(size_t *) "NEDMALOC");
#endif
		return 0;
	}
#if USE_MAGIC_HEADERS
	_ret=(size_t *) ret;
	ret=(void *)(_ret+3);
	for(; _ret<(size_t *)ret-2; _ret++) *_ret=*(size_t *) "NEDMALOC";
	_ret[0]=(size_t) mspace;
	_ret[1]=size;
#endif
	return ret;
}

static FORCEINLINE void CallFree(void *mspace, void *mem) THROWSPEC
{
#if USE_MAGIC_HEADERS
	mstate oldmspace=0;
	size_t *_mem=(size_t *) mem-3, oldsize=0;
	if(_mem[0]!=*(size_t *) "NEDMALOC")
	{
#if defined(DEBUG)
		printf("*** nedmalloc frees system allocated block %p\n", mem);
#endif
		free(mem);
		return;
	}
	oldmspace=(mstate) _mem[1];
	oldsize=_mem[2];
	for(; *_mem==*(size_t *) "NEDMALOC"; *_mem--=0);
	mem=(void *)(++_mem);
#endif
#if USE_ALLOCATOR==0
	free(mem);
#elif USE_ALLOCATOR==1
	mspace_free((mstate) mspace, mem);
#endif
}

size_t nedblksize(void *mem) THROWSPEC
{
	if(mem)
	{
#if USE_MAGIC_HEADERS
		size_t *_mem=(size_t *) mem-3;
		if(_mem[0]==*(size_t *) "NEDMALOC")
		{
//			mstate mspace=(mstate) _mem[1];
			size_t size=_mem[2];
			return size-3*sizeof(size_t);
		}
		else return 0;
#else
#if USE_ALLOCATOR==0
		/* Fail everything */
		return 0;
#elif USE_ALLOCATOR==1
#ifdef _MSC_VER
		__try
#endif
		{
			/* We try to return zero here if it isn't one of our own blocks, however
			the current block annotation scheme used by dlmalloc makes it impossible
			to be absolutely sure of avoiding a segfault.

			mchunkptr->prev_foot = mem-(2*size_t) = mstate ^ mparams.magic for PRECEDING block;
			mchunkptr->head      = mem-(1*size_t) = 8 multiple size of this block with bottom three bits = FLAG_BITS
			*/
			mchunkptr p=mem2chunk(mem);
			mstate fm=0;
			if(!is_inuse(p)) return 0;
			/* The following isn't safe but is probably true: unlikely to allocate
			a 2Gb block on a 32bit system or a 8Eb block on a 64 bit system */
			if(p->head & ((size_t)1)<<(SIZE_T_BITSIZE-SIZE_T_ONE)) return 0;
			/* We have now reduced our chances of being wrong to 0.5^4 = 6.25%.
			We could start comparing prev_foot's for similarity but it starts getting slow. */
			fm = get_mstate_for(p);
			assert(ok_magic(fm));	/* If this fails, someone tried to free a block twice */
			if(ok_magic(fm))
				return chunksize(p)-overhead_for(p);
		}
#ifdef _MSC_VER
		__except(1) { }
#endif
#endif
#endif
	}
	return 0;
}

void nedsetvalue(void *v) THROWSPEC					{ nedpsetvalue((nedpool *) 0, v); }
NEDMALLOCPTRATTR void * nedmalloc(size_t size) THROWSPEC				{ return nedpmalloc((nedpool *) 0, size); }
NEDMALLOCPTRATTR void * nedcalloc(size_t no, size_t size) THROWSPEC	{ return nedpcalloc((nedpool *) 0, no, size); }
NEDMALLOCPTRATTR void * nedrealloc(void *mem, size_t size) THROWSPEC	{ return nedprealloc((nedpool *) 0, mem, size); }
void   nedfree(void *mem) THROWSPEC					{ nedpfree((nedpool *) 0, mem); }
NEDMALLOCPTRATTR void * nedmemalign(size_t alignment, size_t bytes) THROWSPEC { return nedpmemalign((nedpool *) 0, alignment, bytes); }
#if !NO_MALLINFO
struct mallinfo nedmallinfo(void) THROWSPEC			{ return nedpmallinfo((nedpool *) 0); }
#endif
int    nedmallopt(int parno, int value) THROWSPEC	{ return nedpmallopt((nedpool *) 0, parno, value); }
int    nedmalloc_trim(size_t pad) THROWSPEC			{ return nedpmalloc_trim((nedpool *) 0, pad); }
void   nedmalloc_stats() THROWSPEC					{ nedpmalloc_stats((nedpool *) 0); }
size_t nedmalloc_footprint() THROWSPEC				{ return nedpmalloc_footprint((nedpool *) 0); }
NEDMALLOCPTRATTR void **nedindependent_calloc(size_t elemsno, size_t elemsize, void **chunks) THROWSPEC	{ return nedpindependent_calloc((nedpool *) 0, elemsno, elemsize, chunks); }
NEDMALLOCPTRATTR void **nedindependent_comalloc(size_t elems, size_t *sizes, void **chunks) THROWSPEC	{ return nedpindependent_comalloc((nedpool *) 0, elems, sizes, chunks); }

struct threadcacheblk_t;
typedef struct threadcacheblk_t threadcacheblk;
struct threadcacheblk_t
{	/* Keep less than 16 bytes on 32 bit systems and 32 bytes on 64 bit systems */
#ifdef FULLSANITYCHECKS
	unsigned int magic;
#endif
	unsigned int lastUsed, size;
	threadcacheblk *next, *prev;
};
typedef struct threadcache_t
{
#ifdef FULLSANITYCHECKS
	unsigned int magic1;
#endif
	int mymspace;						/* Last mspace entry this thread used */
	long threadid;
	unsigned int mallocs, frees, successes;
	size_t freeInCache;					/* How much free space is stored in this cache */
	threadcacheblk *bins[(THREADCACHEMAXBINS+1)*2];
#ifdef FULLSANITYCHECKS
	unsigned int magic2;
#endif
} threadcache;
struct nedpool_t
{
	MLOCK_T mutex;
	void *uservalue;
	int threads;						/* Max entries in m to use */
	threadcache *caches[THREADCACHEMAXCACHES];
	TLSVAR mycache;						/* Thread cache for this thread. 0 for unset, negative for use mspace-1 directly, otherwise is cache-1 */
	mstate m[MAXTHREADSINPOOL+1];		/* mspace entries for this pool */
};
static nedpool syspool;

static FORCEINLINE unsigned int size2binidx(size_t _size) THROWSPEC
{	/* 8=1000	16=10000	20=10100	24=11000	32=100000	48=110000	4096=1000000000000 */
	unsigned int topbit, size=(unsigned int)(_size>>4);
	/* 16=1		20=1	24=1	32=10	48=11	64=100	96=110	128=1000	4096=100000000 */

#if defined(__GNUC__)
        topbit = sizeof(size)*__CHAR_BIT__ - 1 - __builtin_clz(size);
#elif defined(_MSC_VER) && _MSC_VER>=1300
	{
            unsigned long bsrTopBit;

            _BitScanReverse(&bsrTopBit, size);

            topbit = bsrTopBit;
        }
#else
#if 0
	union {
		unsigned asInt[2];
		double asDouble;
	};
	int n;

	asDouble = (double)size + 0.5;
	topbit = (asInt[!FOX_BIGENDIAN] >> 20) - 1023;
#else
	{
		unsigned int x=size;
		x = x | (x >> 1);
		x = x | (x >> 2);
		x = x | (x >> 4);
		x = x | (x >> 8);
		x = x | (x >>16);
		x = ~x;
		x = x - ((x >> 1) & 0x55555555);
		x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
		x = (x + (x >> 4)) & 0x0F0F0F0F;
		x = x + (x << 8);
		x = x + (x << 16);
		topbit=31 - (x >> 24);
	}
#endif
#endif
	return topbit;
}


#ifdef FULLSANITYCHECKS
static void tcsanitycheck(threadcacheblk **ptr) THROWSPEC
{
	assert((ptr[0] && ptr[1]) || (!ptr[0] && !ptr[1]));
	if(ptr[0] && ptr[1])
	{
		assert(nedblksize(ptr[0])>=sizeof(threadcacheblk));
		assert(nedblksize(ptr[1])>=sizeof(threadcacheblk));
		assert(*(unsigned int *) "NEDN"==ptr[0]->magic);
		assert(*(unsigned int *) "NEDN"==ptr[1]->magic);
		assert(!ptr[0]->prev);
		assert(!ptr[1]->next);
		if(ptr[0]==ptr[1])
		{
			assert(!ptr[0]->next);
			assert(!ptr[1]->prev);
		}
	}
}
static void tcfullsanitycheck(threadcache *tc) THROWSPEC
{
	threadcacheblk **tcbptr=tc->bins;
	int n;
	for(n=0; n<=THREADCACHEMAXBINS; n++, tcbptr+=2)
	{
		threadcacheblk *b, *ob=0;
		tcsanitycheck(tcbptr);
		for(b=tcbptr[0]; b; ob=b, b=b->next)
		{
			assert(*(unsigned int *) "NEDN"==b->magic);
			assert(!ob || ob->next==b);
			assert(!ob || b->prev==ob);
		}
	}
}
#endif

static NOINLINE void RemoveCacheEntries(nedpool *p, threadcache *tc, unsigned int age) THROWSPEC
{
    UNREFERENCED_PARAMETER(p);
#ifdef FULLSANITYCHECKS
	tcfullsanitycheck(tc);
#endif
	if(tc->freeInCache)
	{
		threadcacheblk **tcbptr=tc->bins;
		int n;
		for(n=0; n<=THREADCACHEMAXBINS; n++, tcbptr+=2)
		{
			threadcacheblk **tcb=tcbptr+1;		/* come from oldest end of list */
			/*tcsanitycheck(tcbptr);*/
			for(; *tcb && tc->frees-(*tcb)->lastUsed>=age; )
			{
				threadcacheblk *f=*tcb;
				size_t blksize=f->size; /*nedblksize(f);*/
				assert(blksize<=nedblksize(f));
				assert(blksize);
#ifdef FULLSANITYCHECKS
				assert(*(unsigned int *) "NEDN"==(*tcb)->magic);
#endif
				*tcb=(*tcb)->prev;
				if(*tcb)
					(*tcb)->next=0;
				else
					*tcbptr=0;
				tc->freeInCache-=blksize;
				assert((long) tc->freeInCache>=0);
				CallFree(0, f);
				/*tcsanitycheck(tcbptr);*/
			}
		}
	}
#ifdef FULLSANITYCHECKS
	tcfullsanitycheck(tc);
#endif
}
static void DestroyCaches(nedpool *p) THROWSPEC
{
	if(p->caches)
	{
		threadcache *tc;
		int n;
		for(n=0; n<THREADCACHEMAXCACHES; n++)
		{
			if((tc=p->caches[n]))
			{
				tc->frees++;
				RemoveCacheEntries(p, tc, 0);
				assert(!tc->freeInCache);
				tc->mymspace=-1;
				tc->threadid=0;
				CallFree(0, tc);
				p->caches[n]=0;
			}
		}
	}
}

static NOINLINE threadcache *AllocCache(nedpool *p) THROWSPEC
{
	threadcache *tc=0;
	int n, end;
	ACQUIRE_LOCK(&p->mutex);
	for(n=0; n<THREADCACHEMAXCACHES && p->caches[n]; n++);
	if(THREADCACHEMAXCACHES==n)
	{	/* List exhausted, so disable for this thread */
		RELEASE_LOCK(&p->mutex);
		return 0;
	}
	tc=p->caches[n]=(threadcache *) CallCalloc(p->m[0], 1, sizeof(threadcache), 0);
	if(!tc)
	{
		RELEASE_LOCK(&p->mutex);
		return 0;
	}
#ifdef FULLSANITYCHECKS
	tc->magic1=*(unsigned int *)"NEDMALC1";
	tc->magic2=*(unsigned int *)"NEDMALC2";
#endif
	tc->threadid=(long)(size_t)CURRENT_THREAD;
	for(end=0; p->m[end]; end++);
	tc->mymspace=abs(tc->threadid) % end;
	RELEASE_LOCK(&p->mutex);
	if(TLSSET(p->mycache, (void *)(size_t)(n+1))) abort();
	return tc;
}

static void *threadcache_malloc(nedpool *p, threadcache *tc, size_t *size) THROWSPEC
{
	void *ret=0;
	unsigned int bestsize;
	unsigned int idx=size2binidx(*size);
	size_t blksize=0;
	threadcacheblk *blk, **binsptr;
    UNREFERENCED_PARAMETER(p);
#ifdef FULLSANITYCHECKS
	tcfullsanitycheck(tc);
#endif
	/* Calculate best fit bin size */
	bestsize=1<<(idx+4);
#ifdef FINEGRAINEDBINS
	/* Finer grained bin fit */
	idx<<=1;
	if(*size>bestsize)
	{
		idx++;
		bestsize+=bestsize>>1;
	}
	if(*size>bestsize)
	{
		idx++;
		bestsize=1<<(4+(idx>>1));
	}
#else
	if(*size>bestsize)
	{
		idx++;
		bestsize<<=1;
	}
#endif
	assert(bestsize>=*size);
	if(*size<bestsize) *size=bestsize;
	assert(*size<=THREADCACHEMAX);
	assert(idx<=THREADCACHEMAXBINS);
	binsptr=&tc->bins[idx*2];
	/* Try to match close, but move up a bin if necessary */
	blk=*binsptr;
	if(!blk || blk->size<*size)
	{	/* Bump it up a bin */
		if(idx<THREADCACHEMAXBINS)
		{
			idx++;
			binsptr+=2;
			blk=*binsptr;
		}
	}
	if(blk)
	{
		blksize=blk->size; /*nedblksize(blk);*/
		assert(nedblksize(blk)>=blksize);
		assert(blksize>=*size);
		if(blk->next)
			blk->next->prev=0;
		*binsptr=blk->next;
		if(!*binsptr)
			binsptr[1]=0;
#ifdef FULLSANITYCHECKS
		blk->magic=0;
#endif
		assert(binsptr[0]!=blk && binsptr[1]!=blk);
		assert(nedblksize(blk)>=sizeof(threadcacheblk) && nedblksize(blk)<=THREADCACHEMAX+CHUNK_OVERHEAD);
		/*printf("malloc: %p, %p, %p, %lu\n", p, tc, blk, (long) size);*/
		ret=(void *) blk;
	}
	++tc->mallocs;
	if(ret)
	{
		assert(blksize>=*size);
		++tc->successes;
		tc->freeInCache-=blksize;
		assert((long) tc->freeInCache>=0);
	}
#if defined(DEBUG) && 0
	if(!(tc->mallocs & 0xfff))
	{
		printf("*** threadcache=%u, mallocs=%u (%f), free=%u (%f), freeInCache=%u\n", (unsigned int) tc->threadid, tc->mallocs,
			(float) tc->successes/tc->mallocs, tc->frees, (float) tc->successes/tc->frees, (unsigned int) tc->freeInCache);
	}
#endif
#ifdef FULLSANITYCHECKS
	tcfullsanitycheck(tc);
#endif
	return ret;
}
static NOINLINE void ReleaseFreeInCache(nedpool *p, threadcache *tc, int mymspace) THROWSPEC
{
	unsigned int age=THREADCACHEMAXFREESPACE/8192;
	/*ACQUIRE_LOCK(&p->m[mymspace]->mutex);*/
    UNREFERENCED_PARAMETER(mymspace);
	while(age && tc->freeInCache>=THREADCACHEMAXFREESPACE)
	{
		RemoveCacheEntries(p, tc, age);
		/*printf("*** Removing cache entries older than %u (%u)\n", age, (unsigned int) tc->freeInCache);*/
		age>>=1;
	}
	/*RELEASE_LOCK(&p->m[mymspace]->mutex);*/
}
static void threadcache_free(nedpool *p, threadcache *tc, int mymspace, void *mem, size_t size) THROWSPEC
{
	unsigned int bestsize;
	unsigned int idx=size2binidx(size);
	threadcacheblk **binsptr, *tck=(threadcacheblk *) mem;
	assert(size>=sizeof(threadcacheblk) && size<=THREADCACHEMAX+CHUNK_OVERHEAD);
#ifdef DEBUG
	/* Make sure this is a valid memory block */
	assert(nedblksize(mem));
#endif
#ifdef FULLSANITYCHECKS
	tcfullsanitycheck(tc);
#endif
	/* Calculate best fit bin size */
	bestsize=1<<(idx+4);
#ifdef FINEGRAINEDBINS
	/* Finer grained bin fit */
	idx<<=1;
	if(size>bestsize)
	{
		unsigned int biggerbestsize=bestsize+(bestsize<<1);
		if(size>=biggerbestsize)
		{
			idx++;
			bestsize=biggerbestsize;
		}
	}
#endif
	if(bestsize!=size)	/* dlmalloc can round up, so we round down to preserve indexing */
		size=bestsize;
	binsptr=&tc->bins[idx*2];
	assert(idx<=THREADCACHEMAXBINS);
	if(tck==*binsptr)
	{
		fprintf(stderr, "nedmalloc: Attempt to free already freed memory block %p - aborting!\n", tck);
		abort();
	}
#ifdef FULLSANITYCHECKS
	tck->magic=*(unsigned int *) "NEDN";
#endif
	tck->lastUsed=++tc->frees;
	tck->size=(unsigned int) size;
	tck->next=*binsptr;
	tck->prev=0;
	if(tck->next)
		tck->next->prev=tck;
	else
		binsptr[1]=tck;
	assert(!*binsptr || (*binsptr)->size==tck->size);
	*binsptr=tck;
	assert(tck==tc->bins[idx*2]);
	assert(tc->bins[idx*2+1]==tck || binsptr[0]->next->prev==tck);
	/*printf("free: %p, %p, %p, %lu\n", p, tc, mem, (long) size);*/
	tc->freeInCache+=size;
#ifdef FULLSANITYCHECKS
	tcfullsanitycheck(tc);
#endif
#if 1
	if(tc->freeInCache>=THREADCACHEMAXFREESPACE)
		ReleaseFreeInCache(p, tc, mymspace);
#endif
}




static NOINLINE int InitPool(nedpool *p, size_t capacity, int threads) THROWSPEC
{	/* threads is -1 for system pool */
	ensure_initialization();
	ACQUIRE_MALLOC_GLOBAL_LOCK();
	if(p->threads) goto done;
	if(INITIAL_LOCK(&p->mutex)) goto err;
	if(TLSALLOC(&p->mycache)) goto err;
#if USE_ALLOCATOR==0
	p->m[0]=(mstate) mspacecounter++;
#elif USE_ALLOCATOR==1
	if(!(p->m[0]=(mstate) create_mspace(capacity, 1))) goto err;
	p->m[0]->extp=p;
#endif
	p->threads=(threads<1 || threads>MAXTHREADSINPOOL) ? MAXTHREADSINPOOL : threads;
done:
	RELEASE_MALLOC_GLOBAL_LOCK();
	return 1;
err:
	if(threads<0)
		abort();			/* If you can't allocate for system pool, we're screwed */
	DestroyCaches(p);
	if(p->m[0])
	{
#if USE_ALLOCATOR==1
		destroy_mspace(p->m[0]);
#endif
		p->m[0]=0;
	}
	if(p->mycache)
	{
		if(TLSFREE(p->mycache)) abort();
		p->mycache=0;
	}
	RELEASE_MALLOC_GLOBAL_LOCK();
	return 0;
}
static NOINLINE mstate FindMSpace(nedpool *p, threadcache *tc, int *lastUsed, size_t size) THROWSPEC
{	/* Gets called when thread's last used mspace is in use. The strategy
	is to run through the list of all available mspaces looking for an
	unlocked one and if we fail, we create a new one so long as we don't
	exceed p->threads */
	int n, end;
	for(n=end=*lastUsed+1; p->m[n]; end=++n)
	{
		if(TRY_LOCK(&p->m[n]->mutex)) goto found;
	}
	for(n=0; n<*lastUsed && p->m[n]; n++)
	{
		if(TRY_LOCK(&p->m[n]->mutex)) goto found;
	}
	if(end<p->threads)
	{
		mstate temp;
#if USE_ALLOCATOR==0
		temp=(mstate) mspacecounter++;
#elif USE_ALLOCATOR==1
		if(!(temp=(mstate) create_mspace(size, 1)))
			goto badexit;
#endif
		/* Now we're ready to modify the lists, we lock */
		ACQUIRE_LOCK(&p->mutex);
		while(p->m[end] && end<p->threads)
			end++;
		if(end>=p->threads)
		{	/* Drat, must destroy it now */
			RELEASE_LOCK(&p->mutex);
#if USE_ALLOCATOR==1
			destroy_mspace((mstate) temp);
#endif
			goto badexit;
		}
		/* We really want to make sure this goes into memory now but we
		have to be careful of breaking aliasing rules, so write it twice */
		*((volatile struct malloc_state **) &p->m[end])=p->m[end]=temp;
		ACQUIRE_LOCK(&p->m[end]->mutex);
		/*printf("Created mspace idx %d\n", end);*/
		RELEASE_LOCK(&p->mutex);
		n=end;
		goto found;
	}
	/* Let it lock on the last one it used */
badexit:
	ACQUIRE_LOCK(&p->m[*lastUsed]->mutex);
	return p->m[*lastUsed];
found:
	*lastUsed=n;
	if(tc)
		tc->mymspace=n;
	else
	{
		if(TLSSET(p->mycache, (void *)(size_t)(-(n+1)))) abort();
	}
	return p->m[n];
}

NEDMALLOCPTRATTR nedpool *nedcreatepool(size_t capacity, int threads) THROWSPEC
{
	nedpool *ret;
	if(!(ret=(nedpool *) nedpcalloc(0, 1, sizeof(nedpool)))) return 0;
	if(!InitPool(ret, capacity, threads))
	{
		nedpfree(0, ret);
		return 0;
	}
	return ret;
}
void neddestroypool(nedpool *p) THROWSPEC
{
	int n;
	ACQUIRE_LOCK(&p->mutex);
	DestroyCaches(p);
	for(n=0; p->m[n]; n++)
	{
#if USE_ALLOCATOR==1
		destroy_mspace(p->m[n]);
#endif
		p->m[n]=0;
	}
	RELEASE_LOCK(&p->mutex);
	if(TLSFREE(p->mycache)) abort();
	nedpfree(0, p);
}
void neddestroysyspool() THROWSPEC
{
	nedpool *p=&syspool;
	int n;
	ACQUIRE_LOCK(&p->mutex);
	DestroyCaches(p);
	for(n=0; p->m[n]; n++)
	{
#if USE_ALLOCATOR==1
		destroy_mspace(p->m[n]);
#endif
		p->m[n]=0;
	}
	/* Render syspool unusable */
	for(n=0; n<THREADCACHEMAXCACHES; n++)
		p->caches[n]=(threadcache *)0xdeadbeef;
	for(n=0; n<MAXTHREADSINPOOL+1; n++)
		p->m[n]=(mstate)0xdeadbeef;
	if(TLSFREE(p->mycache)) abort();
	RELEASE_LOCK(&p->mutex);
}

void nedpsetvalue(nedpool *p, void *v) THROWSPEC
{
	if(!p) { p=&syspool; if(!syspool.threads) InitPool(&syspool, 0, -1); }
	p->uservalue=v;
}

void *nedgetvalue(nedpool **p, void *mem) THROWSPEC
{
	nedpool *np=0;
	mchunkptr mcp=mem2chunk(mem);
	mstate fm;
	if(!(is_aligned(chunk2mem(mcp))) && mcp->head != FENCEPOST_HEAD) return 0;
	if(!cinuse(mcp)) return 0;
	if(!next_pinuse(mcp)) return 0;
	if(!is_mmapped(mcp) && !pinuse(mcp))
	{
		if(next_chunk(prev_chunk(mcp))!=mcp) return 0;
	}
	fm=get_mstate_for(mcp);
	if(!ok_magic(fm)) return 0;
	if(!ok_address(fm, mcp)) return 0;
	if(!fm->extp) return 0;
	np=(nedpool *) fm->extp;
	if(p) *p=np;
	return np->uservalue;
}

void nedtrimthreadcache(nedpool *p, int disable) THROWSPEC
{
	int mycache;
	if(!p)
	{
		p=&syspool;
		if(!syspool.threads) InitPool(&syspool, 0, -1);
	}
	mycache=(int)(size_t) TLSGET(p->mycache);
	if(!mycache)
	{	/* Set to mspace 0 */
		if(disable && TLSSET(p->mycache, (void *)(size_t)-1)) abort();
	}
	else if(mycache>0)
	{	/* Set to last used mspace */
		threadcache *tc=p->caches[mycache-1];
#if defined(DEBUG)
		printf("Threadcache utilisation: %lf%% in cache with %lf%% lost to other threads\n",
			100.0*tc->successes/tc->mallocs, 100.0*((double) tc->mallocs-tc->frees)/tc->mallocs);
#endif
		if(disable && TLSSET(p->mycache, (void *)(size_t)(-tc->mymspace))) abort();
		tc->frees++;
		RemoveCacheEntries(p, tc, 0);
		assert(!tc->freeInCache);
		if(disable)
		{
			tc->mymspace=-1;
			tc->threadid=0;
			CallFree(0, p->caches[mycache-1]);
			p->caches[mycache-1]=0;
		}
	}
}
void neddisablethreadcache(nedpool *p) THROWSPEC
{
	nedtrimthreadcache(p, 1);
}

#define GETMSPACE(m,p,tc,ms,s,action)                 \
  do                                                  \
  {                                                   \
    mstate m = GetMSpace((p),(tc),(ms),(s));          \
    action;                                           \
	if(USE_ALLOCATOR==1) { RELEASE_LOCK(&m->mutex); } \
  } while (0)

static FORCEINLINE mstate GetMSpace(nedpool *p, threadcache *tc, int mymspace, size_t size) THROWSPEC
{	/* Returns a locked and ready for use mspace */
	mstate m=p->m[mymspace];
	assert(m);
#if USE_ALLOCATOR==1
	if(!TRY_LOCK(&p->m[mymspace]->mutex)) m=FindMSpace(p, tc, &mymspace, size);
	/*assert(IS_LOCKED(&p->m[mymspace]->mutex));*/
#endif
	return m;
}
static FORCEINLINE void GetThreadCache(nedpool **p, threadcache **tc, int *mymspace, size_t *size) THROWSPEC
{
	int mycache;
	if(size && *size<sizeof(threadcacheblk)) *size=sizeof(threadcacheblk);
	if(!*p)
	{
		*p=&syspool;
		if(!syspool.threads) InitPool(&syspool, 0, -1);
	}
	mycache=(int)(size_t) TLSGET((*p)->mycache);
	if(mycache>0)
	{	/* Already have a cache */
		*tc=(*p)->caches[mycache-1];
		*mymspace=(*tc)->mymspace;
	}
	else if(!mycache)
	{	/* Need to allocate a new cache */
		*tc=AllocCache(*p);
		if(!*tc)
		{	/* Disable */
			if(TLSSET((*p)->mycache, (void *)(size_t)-1)) abort();
			*mymspace=0;
		}
		else
			*mymspace=(*tc)->mymspace;
	}
	else
	{	/* Cache disabled, but we do have an assigned thread pool */
		*tc=0;
		*mymspace=-mycache-1;
	}
	assert(*mymspace>=0);
	assert(!(*tc) || (long)(size_t)CURRENT_THREAD==(*tc)->threadid);
#ifdef FULLSANITYCHECKS
	if(*tc)
	{
		if(*(unsigned int *)"NEDMALC1"!=(*tc)->magic1 || *(unsigned int *)"NEDMALC2"!=(*tc)->magic2)
		{
			abort();
		}
	}
#endif
}

NEDMALLOCPTRATTR void * nedpmalloc(nedpool *p, size_t size) THROWSPEC
{
	void *ret=0;
	threadcache *tc;
	int mymspace;
	GetThreadCache(&p, &tc, &mymspace, &size);
#if THREADCACHEMAX
	if(tc && size<=THREADCACHEMAX)
	{	/* Use the thread cache */
		ret=threadcache_malloc(p, tc, &size);
	}
#endif
	if(!ret)
	{	/* Use this thread's mspace */
        GETMSPACE(m, p, tc, mymspace, size,
                  ret=CallMalloc(m, size, 0));
	}
	return ret;
}
NEDMALLOCPTRATTR void * nedpcalloc(nedpool *p, size_t no, size_t size) THROWSPEC
{
	size_t rsize=size*no;
	void *ret=0;
	threadcache *tc;
	int mymspace;
	GetThreadCache(&p, &tc, &mymspace, &rsize);
#if THREADCACHEMAX
	if(tc && rsize<=THREADCACHEMAX)
	{	/* Use the thread cache */
		if((ret=threadcache_malloc(p, tc, &rsize)))
			memset(ret, 0, rsize);
	}
#endif
	if(!ret)
	{	/* Use this thread's mspace */
        GETMSPACE(m, p, tc, mymspace, rsize,
                  ret=CallCalloc(m, 1, rsize, 0));
	}
	return ret;
}
NEDMALLOCPTRATTR void * nedprealloc(nedpool *p, void *mem, size_t size) THROWSPEC
{
	void *ret=0;
	threadcache *tc;
	int mymspace;
	if(!mem) return nedpmalloc(p, size);
	GetThreadCache(&p, &tc, &mymspace, &size);
#if THREADCACHEMAX
	if(tc && size && size<=THREADCACHEMAX)
	{	/* Use the thread cache */
		size_t memsize=nedblksize(mem);
#if !USE_MAGIC_HEADERS
		assert(memsize);
		if(!memsize)
		{
			fprintf(stderr, "nedmalloc: nedprealloc() called with a block not created by nedmalloc!\n");
			abort();
		}
#endif
		if((ret=threadcache_malloc(p, tc, &size)))
		{
			memcpy(ret, mem, memsize<size ? memsize : size);
			if(memsize && memsize<=THREADCACHEMAX)
				threadcache_free(p, tc, mymspace, mem, memsize);
			else
				CallFree(0, mem);
		}
	}
#endif
	if(!ret)
	{	/* Reallocs always happen in the mspace they happened in, so skip
		locking the preferred mspace for this thread */
		ret=CallRealloc(0, mem, size);
	}
	return ret;
}
void   nedpfree(nedpool *p, void *mem) THROWSPEC
{	/* Frees always happen in the mspace they happened in, so skip
	locking the preferred mspace for this thread */
	threadcache *tc;
	int mymspace;
	size_t memsize;
	if(!mem)
	{	/* You'd be surprised the number of times this happens as so many
		allocators are non-conformant here */
//		fprintf(stderr, "nedmalloc: WARNING nedpfree() called with zero. This is not portable behaviour!\n");
		return;
	}
	GetThreadCache(&p, &tc, &mymspace, 0);
#if THREADCACHEMAX
	memsize=nedblksize(mem);
#if !USE_MAGIC_HEADERS
	assert(memsize);
	if(!memsize)
	{
		fprintf(stderr, "nedmalloc: nedpfree() called with a block not created by nedmalloc!\n");
		abort();
	}
#endif
	if(mem && tc && memsize && memsize<=(THREADCACHEMAX+CHUNK_OVERHEAD))
		threadcache_free(p, tc, mymspace, mem, memsize);
	else
#endif
		CallFree(0, mem);
}
NEDMALLOCPTRATTR void * nedpmemalign(nedpool *p, size_t alignment, size_t bytes) THROWSPEC
{
	void *ret;
	threadcache *tc;
	int mymspace;
	GetThreadCache(&p, &tc, &mymspace, &bytes);
	{	/* Use this thread's mspace */
        GETMSPACE(m, p, tc, mymspace, bytes,
                  ret=CallMalloc(m, bytes, alignment));
	}
	return ret;
}
#if !NO_MALLINFO
struct mallinfo nedpmallinfo(nedpool *p) THROWSPEC
{
	int n;
	struct mallinfo ret={0,0,0,0,0,0,0,0,0,0};
	if(!p) { p=&syspool; if(!syspool.threads) InitPool(&syspool, 0, -1); }
	for(n=0; p->m[n]; n++)
	{
#if USE_ALLOCATOR==1
		struct mallinfo t=mspace_mallinfo(p->m[n]);
		ret.arena+=t.arena;
		ret.ordblks+=t.ordblks;
		ret.hblkhd+=t.hblkhd;
		ret.usmblks+=t.usmblks;
		ret.uordblks+=t.uordblks;
		ret.fordblks+=t.fordblks;
		ret.keepcost+=t.keepcost;
#endif
	}
	return ret;
}
#endif
int    nedpmallopt(nedpool *p, int parno, int value) THROWSPEC
{
    UNREFERENCED_PARAMETER(p);
#if USE_ALLOCATOR==1
	return mspace_mallopt(parno, value);
#else
	return 0;
#endif
}
void*  nedmalloc_internals(size_t *granularity, size_t *magic) THROWSPEC
{
#if USE_ALLOCATOR==1
	if(granularity) *granularity=mparams.granularity;
	if(magic) *magic=mparams.magic;
	return (void *) &syspool;
#else
	if(granularity) *granularity=0;
	if(magic) *magic=0;
	return 0;
#endif
}
int    nedpmalloc_trim(nedpool *p, size_t pad) THROWSPEC
{
	int n, ret=0;
	if(!p) { p=&syspool; if(!syspool.threads) InitPool(&syspool, 0, -1); }
	for(n=0; p->m[n]; n++)
	{
#if USE_ALLOCATOR==1
		ret+=mspace_trim(p->m[n], pad);
#endif
	}
	return ret;
}
void   nedpmalloc_stats(nedpool *p) THROWSPEC
{
	int n;
	if(!p) { p=&syspool; if(!syspool.threads) InitPool(&syspool, 0, -1); }
	for(n=0; p->m[n]; n++)
	{
#if USE_ALLOCATOR==1
		mspace_malloc_stats(p->m[n]);
#endif
	}
}
size_t nedpmalloc_footprint(nedpool *p) THROWSPEC
{
	size_t ret=0;
	int n;
	if(!p) { p=&syspool; if(!syspool.threads) InitPool(&syspool, 0, -1); }
	for(n=0; p->m[n]; n++)
	{
#if USE_ALLOCATOR==1
		ret+=mspace_footprint(p->m[n]);
#endif
	}
	return ret;
}
NEDMALLOCPTRATTR void **nedpindependent_calloc(nedpool *p, size_t elemsno, size_t elemsize, void **chunks) THROWSPEC
{
	void **ret;
	threadcache *tc;
	int mymspace;
	GetThreadCache(&p, &tc, &mymspace, &elemsize);
#if USE_ALLOCATOR==0
    GETMSPACE(m, p, tc, mymspace, elemsno*elemsize,
              ret=unsupported_operation("independent_calloc"));
#elif USE_ALLOCATOR==1
    GETMSPACE(m, p, tc, mymspace, elemsno*elemsize,
              ret=mspace_independent_calloc(m, elemsno, elemsize, chunks));
#endif
	return ret;
}
NEDMALLOCPTRATTR void **nedpindependent_comalloc(nedpool *p, size_t elems, size_t *sizes, void **chunks) THROWSPEC
{
	void **ret;
	threadcache *tc;
	int mymspace;
    size_t i, *adjustedsizes=(size_t *) alloca(elems*sizeof(size_t));
    if(!adjustedsizes) return 0;
    for(i=0; i<elems; i++)
        adjustedsizes[i]=sizes[i]<sizeof(threadcacheblk) ? sizeof(threadcacheblk) : sizes[i];
	GetThreadCache(&p, &tc, &mymspace, 0);
#if USE_ALLOCATOR==0
	GETMSPACE(m, p, tc, mymspace, 0,
              ret=unsupported_operation("independent_comalloc"));
#elif USE_ALLOCATOR==1
	GETMSPACE(m, p, tc, mymspace, 0,
              ret=mspace_independent_comalloc(m, elems, adjustedsizes, chunks));
#endif
	return ret;
}

// cheap replacement for strdup so we aren't feeding system allocated blocks into nedmalloc

NEDMALLOCPTRATTR char *nedstrdup(const char *str) THROWSPEC
{
    int n = strlen(str) + 1;
    char *dup = nedmalloc(n);
    return dup ? memcpy(dup, str, n) : NULL;
}

#if defined(__cplusplus)
}
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif
