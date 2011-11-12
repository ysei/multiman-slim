#include "util.h"

/*
 * Monitor
 */
int32_t
utilMonitorInit(UtilMonitor* pMonitor)
{
	sys_mutex_attribute_t mutexAttr;
	sys_mutex_attribute_initialize(mutexAttr);
	int32_t ret = sys_mutex_create(&pMonitor->mutex, &mutexAttr);
	if(ret < CELL_OK){
		EMSG("sys_mutex_create() failed. %d\n", ret);
		return ret;
	}
	sys_cond_attribute_t condAttr;
	sys_cond_attribute_initialize(condAttr);
	ret = sys_cond_create(&pMonitor->cond,
						  pMonitor->mutex,
						  &condAttr);
	if(ret < CELL_OK){
		EMSG("sys_cond_create() failed. (%d)\n", ret);
		(void)sys_mutex_destroy(pMonitor->mutex);
		return ret;
	}
	return ret;
}

int32_t
utilMonitorFin(UtilMonitor* pMonitor)
{
	int32_t ret;
	ret = sys_cond_destroy(pMonitor->cond);
	if(ret < CELL_OK){
		EMSG("sys_cond_destroy() failed. (%d)\n", ret);
	}
	ret = sys_mutex_destroy(pMonitor->mutex);
	if(ret < CELL_OK){
		EMSG("sys_mutec_destroy() failed. (%d)\n", ret);
	}
	return ret;
}

int32_t
utilMonitorLock(UtilMonitor* pMonitor, usecond_t timeout)
{
	int32_t ret;
	ret = sys_mutex_lock(pMonitor->mutex, timeout);
	if(ret < CELL_OK && (int)ETIMEDOUT != ret){
		EMSG("sys_mutex_lock() failed. (%d)\n", ret);
	}
	return ret;
}

int32_t
utilMonitorUnlock(UtilMonitor* pMonitor)
{
	int32_t ret;
	ret = sys_mutex_unlock(pMonitor->mutex);
	if(ret < CELL_OK){
		EMSG("sys_mutex_unlock() failed. (%d)\n", ret);
	}
	return ret;
}

int32_t
utilMonitorWait(UtilMonitor* pMonitor, usecond_t timeout)
{
	int32_t ret;
	ret = sys_cond_wait(pMonitor->cond, timeout);
	if(ret < CELL_OK && (int)ETIMEDOUT != ret){
		EMSG("sys_cond_wait() failed. (%d)\n", ret);
	}
	return ret;
}

int32_t
utilMonitorSignal(UtilMonitor* pMonitor)
{
	int32_t ret;
	ret = sys_cond_signal(pMonitor->cond);
	if(ret < CELL_OK){
		EMSG("sys_cond_signal() failed. (%d)\n", ret);
	}
	return ret;
}

/*
 * Memory Pool
 */
int32_t utilMemPoolInit(UtilMemPool* pMemPool, size_t unit, uint32_t count, uint32_t nAlignByte){
	int32_t		ret;
	ret = utilMonitorInit(&pMemPool->monitor);
	if(ret < CELL_OK){
		return ret;
	}
	pMemPool->totalByte = unit * count;
	pMemPool->pTop = AlignedAlloc(pMemPool->totalByte, nAlignByte);
	if(pMemPool->pTop == NULL){
		utilMonitorFin(&pMemPool->monitor);
		return -1;
	}
	pMemPool->MemArray = memalign(16, sizeof(void*) * count);
	if(pMemPool->MemArray == NULL){
		AlignedFree(pMemPool->pTop);
		utilMonitorFin(&pMemPool->monitor);
		return -1;
	}
	uint8_t*	pMem = (uint8_t*)pMemPool->pTop;
	uint32_t	index;
	for(index = 0; index < count; ++index){
		pMemPool->MemArray[index] = pMem;
		pMem += unit;
	}
	pMemPool->size = pMemPool->validNum = count;
	return CELL_OK;
}

int32_t utilMemPoolImport(UtilMemPool* pMemPool, void* pTop, size_t unit, uint32_t count){
	int32_t		ret;
	ret = utilMonitorInit(&pMemPool->monitor);
	if(ret < CELL_OK){
		return ret;
	}
	pMemPool->totalByte = unit * count;
	pMemPool->pTop = NULL;
	pMemPool->MemArray = memalign(16, sizeof(void*) * count);
	if(pMemPool->MemArray == NULL){
		utilMonitorFin(&pMemPool->monitor);
		return -1;
	}
	uint8_t*	pMem = (uint8_t*)pTop;
	uint32_t	index;
	for(index = 0; index < count; ++index){
		pMemPool->MemArray[index] = pMem;
		pMem += unit;
	}
	pMemPool->size = pMemPool->validNum = count;
	return CELL_OK;
}

int32_t utilMemPoolFin(UtilMemPool* pMemPool){
	free(pMemPool->MemArray);
	if(pMemPool->pTop){
		AlignedFree(pMemPool->pTop);
	}
	return utilMonitorFin(&pMemPool->monitor);
}

int32_t utilMemPoolPop(UtilMemPool* pMemPool, void** ppMem, bool bWait){
	int32_t	ret = -1;
	utilMonitorLock(&pMemPool->monitor, 0);
	while(pMemPool->validNum == 0 && bWait){
		utilMonitorWait(&pMemPool->monitor, 0);
	}
	if(pMemPool->validNum){
		*ppMem = pMemPool->MemArray[--pMemPool->validNum];
		ret = CELL_OK;
	}
	utilMonitorUnlock(&pMemPool->monitor);
	return ret;
}

int32_t utilMemPoolPush(UtilMemPool* pMemPool, void* pMem){
	int32_t	ret = -1;
	utilMonitorLock(&pMemPool->monitor, 0);
	if(pMemPool->validNum < pMemPool->size){
		pMemPool->MemArray[pMemPool->validNum++] = pMem;
		ret = CELL_OK;
		utilMonitorSignal(&pMemPool->monitor);
	}
	utilMonitorUnlock(&pMemPool->monitor);
	return ret;
}

int32_t utilMemPoolSize(UtilMemPool* pMemPool, uint32_t* pSize){
	int32_t	ret = CELL_OK;
	utilMonitorLock(&pMemPool->monitor, 0);
	*pSize = pMemPool->validNum;
	utilMonitorUnlock(&pMemPool->monitor);
	return ret;
}

/*
 * Queue
 */
int32_t utilQueueInit(UtilQueue* pQueue, UtilMonitor* pMonitor, size_t unit, uint32_t count){
	if(pMonitor == NULL){
		return -1;
	}
	pQueue->pMonitor = pMonitor;
	pQueue->QueueArray = memalign(16, unit * count);
	if(pQueue->QueueArray == NULL){
		return -1;
	}
	pQueue->unit		= unit;
	pQueue->size		= count;
	pQueue->validNum	= pQueue->readIndex = pQueue->writeIndex = 0;
	return CELL_OK;
}

int32_t utilQueueFin(UtilQueue* pQueue){
	free(pQueue->QueueArray);
	pQueue->pMonitor = NULL;
	return CELL_OK;
}

int32_t utilQueuePop(UtilQueue* pQueue, void* pMem, bool bWait){
	int32_t	ret = -1;
	utilMonitorLock(pQueue->pMonitor, 0);
	while(pQueue->validNum == 0 && bWait){
		utilMonitorWait(pQueue->pMonitor, 0);
	}
	if(pQueue->validNum){
		if(pMem){
			memcpy(pMem, pQueue->QueueArray+pQueue->readIndex*pQueue->unit, pQueue->unit);
		}
		pQueue->readIndex = (pQueue->readIndex+1) % pQueue->size;
		--pQueue->validNum;
		ret = CELL_OK;
		utilMonitorSignal(pQueue->pMonitor);
	}
	utilMonitorUnlock(pQueue->pMonitor);
	return ret;
}

int32_t utilQueuePush(UtilQueue* pQueue, void* pMem, bool bWait){
	int32_t	ret = -1;
	utilMonitorLock(pQueue->pMonitor, 0);
	while(pQueue->validNum >= pQueue->size && bWait){
		utilMonitorWait(pQueue->pMonitor, 0);
	}
	if(pQueue->validNum < pQueue->size){
		memcpy(pQueue->QueueArray+pQueue->writeIndex*pQueue->unit, pMem, pQueue->unit);
		pQueue->writeIndex = (pQueue->writeIndex+1) % pQueue->size;
		++pQueue->validNum;
		ret = CELL_OK;
		utilMonitorSignal(pQueue->pMonitor);
	}
	utilMonitorUnlock(pQueue->pMonitor);
	return ret;
}

int32_t utilQueuePeek(UtilQueue* pQueue, void** ppMem, uint32_t* pNum){
	utilMonitorLock(pQueue->pMonitor, 0);
	if(pQueue->validNum && ppMem){
		*ppMem = pQueue->QueueArray + pQueue->readIndex * pQueue->unit;
	}
	*pNum = pQueue->validNum;
	utilMonitorUnlock(pQueue->pMonitor);
	return CELL_OK;
}

/*
 * Buffered file reader
 */
int utilBfrOpen(UtilBufferedFileReader* handle, const char* pcszFilePath){
	handle->filePos = 0LL;
	handle->pBuffer = handle->buffer;
	handle->poolBytes = 0;
	handle->bEof = false;
	handle->bUnbufferedMode = false;
	handle->fileDesc = open(pcszFilePath, O_RDONLY);
	if(handle->fileDesc < 0){
		return -1;
	}

	struct stat	fileStat;
	int	ret = fstat(handle->fileDesc, &fileStat);
	if(ret < CELL_OK){
		close(handle->fileDesc);
		return -1;
	}
	handle->fileSize = fileStat.st_size;
	return 0;
}

long long utilBfrGetFileSize(UtilBufferedFileReader* handle){
	return handle->fileSize;
}

int utilBfrClose(UtilBufferedFileReader* handle){
	if(handle->fileDesc < 0 || close(handle->fileDesc)){
		return -1;
	}
	handle->fileDesc = -1;
	return 0;
}

int utilBfrRead(UtilBufferedFileReader* handle, void* pBuffer, unsigned int bytes){
	if(handle->fileDesc < 0 || handle->bUnbufferedMode){
		return -1;
	}

	int				ret = 0;
	unsigned char*	pByte = (unsigned char*)pBuffer;
	while(bytes){
		if(!handle->poolBytes){
			if(handle->bEof){
				ret = -1;
				break;
			}
			ssize_t readSize = read(handle->fileDesc, handle->buffer, sizeof(handle->buffer));
			if(readSize < 0){
				handle->bEof = true;
				ret = -1;
				break;
			}
			handle->poolBytes = readSize;
			handle->pBuffer = handle->buffer;
			if(readSize <= 0 || handle->poolBytes != sizeof(handle->buffer)){
				handle->bEof = true;
			}
		}

		unsigned int	copyBytes = (bytes > handle->poolBytes)? handle->poolBytes: bytes;
		memcpy(pByte, handle->pBuffer, copyBytes);
		handle->filePos		+= copyBytes;
		pByte				+= copyBytes;
		handle->pBuffer		+= copyBytes;
		bytes				-= copyBytes;
		handle->poolBytes	-= copyBytes;
	}
	return ret;
}

long long utilBfrGetPos(UtilBufferedFileReader* handle){
	return handle->filePos;
}

int utilBfrSeek(UtilBufferedFileReader* handle, long long offset, int whence){
	if(handle->fileDesc < 0
	|| !(whence == SEEK_SET || whence == SEEK_END || whence == SEEK_CUR)){
		return -1;
	}

	long long	pos = 0LL;
	if(whence == SEEK_SET){
		pos = offset;
	}else if(whence == SEEK_END){
		pos = handle->fileSize + offset;
	}else if(whence == SEEK_CUR){
		pos = handle->filePos + offset;
	}
	if(handle->fileDesc < 0 || pos < 0LL || pos > handle->fileSize){
		return -1;
	}

	int		ret = 0;
	bool	bFileSeek = handle->bUnbufferedMode;
	if(!bFileSeek){
		unsigned int	nReadedByte = handle->pBuffer - handle->buffer;
		unsigned int	nReadableByte = handle->poolBytes;
		if(pos == handle->filePos){
			// do nothing.
		}else if(pos < handle->filePos && nReadedByte >= handle->filePos - pos){
			unsigned int	bytes = handle->filePos - pos;
			handle->filePos		-= bytes;
			handle->pBuffer		-= bytes;
			handle->poolBytes	+= bytes;
			handle->bEof		 = false;
		}else if(pos > handle->filePos && nReadableByte >= pos - handle->filePos){
			unsigned int	bytes = pos - handle->filePos;
			handle->filePos		+= bytes;
			handle->pBuffer		+= bytes;
			handle->poolBytes	-= bytes;
			handle->bEof		 = false;
		}else{
			bFileSeek = true;
		}
	}
	if(bFileSeek){
		if(lseek64(handle->fileDesc, pos, SEEK_SET) < 0){
			handle->bEof = true;
			ret = -1;
		}else{
			handle->bEof = false;
			handle->filePos = pos;
		}
		handle->pBuffer = handle->buffer;
		handle->poolBytes = 0;
	}
	return ret;
}

int utilBfrSetUnbufferedMode(UtilBufferedFileReader* handle, bool mode){
	if(handle->bUnbufferedMode != mode){
		handle->pBuffer = handle->buffer;
		handle->poolBytes = 0;
		handle->bUnbufferedMode = mode;
	}
	return 0;
}

int utilBfrUnbufferedRead(UtilBufferedFileReader* handle, void* pBuffer, unsigned int bytes){
	if(handle->fileDesc < 0 || handle->poolBytes || handle->bUnbufferedMode == false){
		return -1;
	}
	if(bytes == 0){
		return 0;
	}
	ssize_t readSize = read(handle->fileDesc, pBuffer, bytes);
	if(readSize <= 0){
		return -1;
	}
	handle->filePos += readSize;
	return ((unsigned int)readSize == bytes)? 0: -1;
}

