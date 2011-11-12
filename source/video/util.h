
#ifndef	__UTIL_H__
#define	__UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ppu_thread.h>
#include <sys/synchronization.h>

#define EMSG0(x, ...)	printf(x, __func__, ##__VA_ARGS__)
#define EMSG(...)		EMSG0("* %s: ERROR! " __VA_ARGS__)

#define EINFO(x)		EMSG("exit. (ret=0x%08X, file=%s:%d)\n", (unsigned int)x, __FILE__, __LINE__)

#define DP0(x, ...)		printf(x, __func__, ##__VA_ARGS__)
#define DP(...)			DP0("* %s: " __VA_ARGS__)

#define ALIGN_128BYTE	(128)
#define ALIGN_1MBYTE	(0x100000)
#define ROUNDUP(x, a) (((x)+((a)-1))&(~((a)-1)))
#define STRUCT_OFFSET(type, member) ((uintptr_t)&(((type*)NULL)->member))

static inline void* AlignedAlloc(unsigned int nReqSize, unsigned int nAlignByte){
	if(nAlignByte < sizeof(void*)){
		EMSG("AlignedAlloc() failed. (too small nAlignByte)\n");
		return NULL;
	}
	const unsigned int	nAdditionalByte = nAlignByte * 2 - 1;
	const unsigned int	nSize = nReqSize + nAdditionalByte;
	unsigned char*	pOrg = memalign(16, nSize);
	if(pOrg == NULL){
		EMSG("AlignedAlloc() failed. (malloc() return NULL)\n");
		return NULL;
	}
	unsigned char*	pAligned = (unsigned char*)((uintptr_t)(pOrg + nAdditionalByte) & ~((uintptr_t)nAlignByte-1));
	void**			ppOrgSave = (void**)(pAligned - sizeof(void*));
	*ppOrgSave = pOrg;
	return (void*)pAligned;
}

static inline void AlignedFree(void* p){
	if(p == NULL){
		return;
	}
	unsigned char*	pAligned = (unsigned char*)p;
	void**			ppOrgSave = (void**)(pAligned - sizeof(void*));
	unsigned char*	pOrg = (unsigned char*)*ppOrgSave;
	free(pOrg);
}


typedef struct {
	sys_mutex_t	mutex;
	sys_cond_t	cond;
}UtilMonitor;

int32_t utilMonitorInit(UtilMonitor* pMonitor);
int32_t utilMonitorFin(UtilMonitor* pMonitor);
int32_t utilMonitorLock(UtilMonitor* pMonitor, usecond_t timeout);
int32_t utilMonitorUnlock(UtilMonitor* pMonitor);
int32_t utilMonitorWait(UtilMonitor* pMonitor, usecond_t timeout);
int32_t utilMonitorSignal(UtilMonitor* pMonitor);


typedef sys_lwmutex_t	UtilLWMutex;

static inline int32_t utilLWMutexInit(UtilLWMutex* pLWMutex){
	sys_lwmutex_attribute_t	attr;
	sys_lwmutex_attribute_initialize(attr);
	return sys_lwmutex_create(pLWMutex, &attr);
}

static inline int32_t utilLWMutexFin(UtilLWMutex* pLWMutex){
	return sys_lwmutex_destroy(pLWMutex);
}

static inline int32_t utilLWMutexLock(UtilLWMutex* pLWMutex, usecond_t timeout){
	return sys_lwmutex_lock(pLWMutex, timeout);
}

static inline int32_t utilLWMutexUnlock(UtilLWMutex* pLWMutex){
	return sys_lwmutex_unlock(pLWMutex);
}


typedef struct {
	void**		MemArray;
	uint32_t	validNum;

	void*		pTop;
	uint32_t	totalByte;
	uint32_t	size;
	UtilMonitor	monitor;
}UtilMemPool;

int32_t utilMemPoolInit(UtilMemPool* pMemPool, size_t unit, uint32_t count, uint32_t nAlignByte);
int32_t utilMemPoolImport(UtilMemPool* pMemPool, void* pTop, size_t unit, uint32_t count);
int32_t utilMemPoolFin(UtilMemPool* pMemPool);
int32_t utilMemPoolPop(UtilMemPool* pMemPool, void** ppMem, bool bWait);
int32_t utilMemPoolPush(UtilMemPool* pMemPool, void* pMem);
int32_t utilMemPoolSize(UtilMemPool* pMemPool, uint32_t* pSize);

static inline int32_t utilMemPoolPrint(UtilMemPool* pMemPool){
	printf("[utilMemPool] size=%u, validNum=%u\n", pMemPool->size, pMemPool->validNum);
	return 0;
}

typedef struct {
	uint8_t*		QueueArray;
	uint32_t		validNum;
	uint32_t		readIndex;
	uint32_t		writeIndex;

	uint32_t		size;
	size_t			unit;
	UtilMonitor*	pMonitor;
}UtilQueue;

int32_t utilQueueInit(UtilQueue* pQueue, UtilMonitor* pMonitor, size_t unit, uint32_t count);
int32_t utilQueueFin(UtilQueue* pQueue);
int32_t utilQueuePop(UtilQueue* pQueue, void* pMem, bool bWait);
int32_t utilQueuePush(UtilQueue* pQueue, void* pMem, bool bWait);
int32_t utilQueuePeek(UtilQueue* pQueue, void** ppMem, uint32_t* pNum);


static inline int32_t utilQueuePrint(UtilQueue* pQueue){
	printf("[utilQueue] size=%u, validNum=%u, readIndex=%u, writeIndex=%u\n",
		pQueue->size, pQueue->validNum, pQueue->readIndex, pQueue->writeIndex);
	return 0;
}


static inline unsigned short utilSwap16(unsigned short x){
	return (unsigned short)(x << 8 | x >> 8);
}

static inline unsigned int utilSwap32(unsigned int x){
	return ((unsigned int)utilSwap16(x) << 16) | utilSwap16(x >> 16);
}

static inline unsigned long long utilSwap64(unsigned long long x){
	return ((unsigned long long)utilSwap32((unsigned int)x) << 32) |
			utilSwap32((unsigned int)(x >> 32));
}


typedef struct {
	int				fileDesc;
	long long		filePos;
	long long		fileSize;

	unsigned char	buffer[1024 * 2];
	unsigned char*	pBuffer;
	unsigned int	poolBytes;
	bool			bEof;
	bool			bUnbufferedMode;
}UtilBufferedFileReader;

int utilBfrOpen(UtilBufferedFileReader* handle, const char* pcszFilePath);
long long utilBfrGetFileSize(UtilBufferedFileReader* handle);
int utilBfrClose(UtilBufferedFileReader* handle);
int utilBfrRead(UtilBufferedFileReader* handle, void* pBuffer, unsigned int bytes);
long long utilBfrGetPos(UtilBufferedFileReader* handle);
int utilBfrSeek(UtilBufferedFileReader* handle, long long offset, int whence);
int utilBfrSetUnbufferedMode(UtilBufferedFileReader* handle, bool mode);
int utilBfrUnbufferedRead(UtilBufferedFileReader* handle, void* pBuffer, unsigned int bytes);

#endif /* __UTIL_H__ */
