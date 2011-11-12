#include "avidmux.h"

extern bool mm_shutdown;
extern u8 is_bg_video;

/* AVI file parser */

/* for AVI 1.0 */
typedef struct{
	unsigned int		dwFourCC;
	unsigned int		dwSize;
	unsigned int		data;
}SAviDmuxChunk;

typedef struct{
	unsigned int		dwList;
	unsigned int		dwSize;
	unsigned int		dwFourCC;
}SAviDmuxList;

typedef struct{
	unsigned int		ckid;
	unsigned int		dwFlags;
	unsigned int		dwChunkOffset;
	unsigned int		dwChunkLength;
}SAviDmuxOldIndexEntry;

/* for OpenDML */
typedef struct{
	unsigned int		dwTotalFrames;
}SAviDmuxExtendedAviHeader;

typedef struct{
	unsigned short		wLongsPerEntry;
	unsigned char		bIndexSubType;
	unsigned char		bIndexType;
	unsigned int		nEntriesInUse;
	unsigned int		dwChunkId;
	unsigned int		dwReserved[3];
}SAviDmuxIndexWoCh;

typedef struct{
	unsigned long long	qwOffset;
	unsigned int		dwSize;
	unsigned int		dwDuration;
}SAviDmuxSpuerIndexEntry;

typedef struct{
	unsigned int		dwOffset;
	unsigned int		dwSize;
}SAviDmuxStdIndexEntry;

typedef struct{
	unsigned int		dwOffset;
	unsigned int		dwSize;
	unsigned int		dwOffsetField2;
}SAviDmuxFieldIndexEntry;

typedef union{
	unsigned int		dw;
	char				fcc[4];
}UAviDmuxFcc;

static unsigned int aviDmuxFccToLower(unsigned int fcc);
static int aviDmuxReadRiffAviChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo);
static bool aviDmuxReadRiffAviHeader(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxChunk* pRiffHeader);
static bool aviDmuxReadNextFcc(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int* pFcc);
static bool aviDmuxReadListHeader(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxList* pList);
static bool aviDmuxReadChunkHeader(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxChunk* pChunk);
static bool aviDmuxSkip(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int size);
static bool aviDmuxReadData(SAviDmuxCtlInfo* pAviDmuxCtlInfo, void* pData, unsigned int size, unsigned int* pReadSize);
static void aviDmuxCheckOdmlIndex(SAviDmuxCtlInfo* pAviDmuxCtlInfo);
static bool aviDmuxReadAvihChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int* pSkipSize);
static bool aviDmuxReadDmlhChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int* pSkipSize);
static bool aviDmuxReadSuperIndexChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int csi, unsigned int* pSkipSize);
static bool aviDmuxReadStrhChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int csi, unsigned int* pSkipSize);
static bool aviDmuxReadStrfChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxStreamInfo* pStreamInfo, unsigned int* pSkipSize);
static bool aviDmuxReadIdx1Chunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int streamOffset, unsigned int* pSkipSize);
static bool aviDmuxReadStdFieldIndex(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int streamIndex, unsigned int* pMaxDataSize);
static bool aviDmuxReadOpenDmlIndex(SAviDmuxCtlInfo* pAviDmuxCtlInfo);
static int aviDmuxClearIndexInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo);


/* AVI demuxer  */

static void aviDmuxThread(uint64_t arg);
static void aviDmuxWaitEvent(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	SAviDmuxStatus* pAviDmuxThreadStatus);
static bool aviDmuxSearchTargetStream(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	uint32_t validStreamNum, long long fileSize, uint32_t* pStreamId);

static int aviDmuxParseDefaultEs(UtilBufferedFileReader* pBfr,
	SAviDmuxStreamInfo* pStreamInfo, SAviDmuxEsCtlInfo* pEsCtlInfo,
	SAviDmuxPosition* pPosition);
static int aviDmuxParseMpegAudioEs(UtilBufferedFileReader* pBfr,
	SAviDmuxStreamInfo* pStreamInfo, SAviDmuxEsCtlInfo* pEsCtlInfo,
	SAviDmuxPosition* pPosition);
static unsigned int aviDmuxGetMpegAudioAuLength(unsigned int nID,
	unsigned int nLayer, unsigned int nBitrate, unsigned int nFs);
static int aviDmuxParseAc3AudioEs(UtilBufferedFileReader* pBfr,
	SAviDmuxStreamInfo* pStreamInfo, SAviDmuxEsCtlInfo* pEsCtlInfo,
	SAviDmuxPosition* pPosition);
static unsigned int aviDmuxGetAc3AudioAuLength(
	unsigned int nFs, unsigned int nFrmsizecod);

static void aviDmuxClearParseInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo);



/* public functions  */

int aviDmuxSetParam(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	SCommonCtlInfo* pCommonCtlInfo)
{
	pAviDmuxCtlInfo->pCommonCtlInfo = pCommonCtlInfo;
	return CELL_OK;
}

int aviDmuxOpen(SAviDmuxCtlInfo* pAviDmuxCtlInfo, const char* filePath){
	int32_t			ret;

	if(utilMonitorInit(&pAviDmuxCtlInfo->umInput)){
		ret = RET_CODE_ERR_FATAL;
		return ret;
	}

	/* initialize control information */
	memset(&pAviDmuxCtlInfo->fileInfo, 0,
		sizeof(SAviDmuxCtlInfo) - STRUCT_OFFSET(SAviDmuxCtlInfo, fileInfo));
	for(uint32_t streamIndex = 0; streamIndex < AVIDMUX_MAX_STREAM_NUM; ++streamIndex){
		pAviDmuxCtlInfo->esCtlInfo[streamIndex].streamIndex = streamIndex;
	}

	/* open stream file */
	ret = utilBfrOpen(&pAviDmuxCtlInfo->bfr, filePath);
	if(ret < CELL_OK){
		EMSG("stream file open failed.\n");
		ret = RET_CODE_ERR_FATAL;
		return ret;
	}

	/* parse avi file */
	ret = aviDmuxReadRiffAviChunk(pAviDmuxCtlInfo);
	if(ret < CELL_OK){
		EMSG("stream parse failed.\n");
		return ret;
	}

	utilBfrSetUnbufferedMode(&pAviDmuxCtlInfo->bfr, true);

	return CELL_OK;
}

int aviDmuxClose(SAviDmuxCtlInfo* pAviDmuxCtlInfo){
	int32_t	ret = CELL_OK;

	const uint32_t	validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
	for(uint32_t streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
		if(pAviDmuxCtlInfo->esCtlInfo[streamIndex].pHandle){
			ret |= aviDmuxDisableEs((void*)&pAviDmuxCtlInfo->esCtlInfo[streamIndex]);
		}
	}

	ret |= aviDmuxClearIndexInfo(pAviDmuxCtlInfo);

	if(utilBfrClose(&pAviDmuxCtlInfo->bfr)){
		ret |= RET_CODE_ERR_FATAL;
	}

	if(utilMonitorFin(&pAviDmuxCtlInfo->umInput)){
		ret |= RET_CODE_ERR_FATAL;
	}

	pAviDmuxCtlInfo->fileInfo.validStreamNum = 0;

	return ret;
}

int aviDmuxGetFileInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	const SAviDmuxFileInfo** ppFileInfo)
{
	if(pAviDmuxCtlInfo->fileInfo.validStreamNum == 0){
		return RET_CODE_ERR_SEQ;
	}

	*ppFileInfo = &pAviDmuxCtlInfo->fileInfo;

	return CELL_OK;
}

int aviDmuxFindVideoStream(SAviDmuxCtlInfo* pAviDmuxCtlInfo, uint32_t fccHandler,
	uint32_t biCompression, uint32_t startIndex, uint32_t* pStreamIndex)
{
	if(pAviDmuxCtlInfo->fileInfo.validStreamNum == 0){
		return RET_CODE_ERR_SEQ;
	}

	uint32_t _fccHandler = aviDmuxFccToLower(fccHandler);
	uint32_t _biCompression = aviDmuxFccToLower(biCompression);
	const uint32_t	validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
	for(uint32_t streamIndex = startIndex; streamIndex < validStreamNum; ++streamIndex){
		const SAviDmuxStreamInfo* pStreamInfo = &pAviDmuxCtlInfo->streamInfo[streamIndex];
		if(pStreamInfo->fccType != AVIDMUX_FCC_vids){
			continue;
		}
		uint32_t streamFccHandler = aviDmuxFccToLower(pStreamInfo->fccHandler);
		uint32_t streamBiCompression = aviDmuxFccToLower(
			pStreamInfo->specInfo.bmpInfoHeader.biCompression);
		if((_fccHandler == AVIDMUX_CODEC_TYPE_ANY && _biCompression == AVIDMUX_CODEC_TYPE_ANY)
		|| (_fccHandler == streamFccHandler       && _biCompression == AVIDMUX_CODEC_TYPE_ANY)
		|| (_fccHandler == AVIDMUX_CODEC_TYPE_ANY && _biCompression == streamBiCompression   )
		|| (_fccHandler == streamFccHandler       && _biCompression == streamBiCompression   ))

		{
			*pStreamIndex = streamIndex;
			return CELL_OK;
		}
	}
	return RET_CODE_ERR_NOT_FOUND;
}

int aviDmuxFindAudioStream(SAviDmuxCtlInfo* pAviDmuxCtlInfo, uint32_t wFormatTag,
	uint32_t startIndex, uint32_t* pStreamIndex)
{
	if(pAviDmuxCtlInfo->fileInfo.validStreamNum == 0){
		return RET_CODE_ERR_SEQ;
	}

	const uint32_t	validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
	for(uint32_t streamIndex = startIndex; streamIndex < validStreamNum; ++streamIndex){
		const SAviDmuxStreamInfo* pStreamInfo = &pAviDmuxCtlInfo->streamInfo[streamIndex];
		if(pStreamInfo->fccType != AVIDMUX_FCC_auds){
			continue;
		}
		const SAviDmuxWaveFormatEx* pWaveFormatEx = &pStreamInfo->specInfo.waveFormatEx;
		if(wFormatTag == AVIDMUX_CODEC_TYPE_ANY || wFormatTag == pWaveFormatEx->wFormatTag){
			*pStreamIndex = streamIndex;
			return CELL_OK;
		}
	}
	return RET_CODE_ERR_NOT_FOUND;
}


int aviDmuxGetStreamInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo, uint32_t streamIndex,
	const SAviDmuxStreamInfo** ppStreamInfo)
{
	if(pAviDmuxCtlInfo->fileInfo.validStreamNum == 0){
		return RET_CODE_ERR_SEQ;
	}
	if(streamIndex >= pAviDmuxCtlInfo->fileInfo.validStreamNum
	|| pAviDmuxCtlInfo->streamInfo[streamIndex].indexNum == 0 ){
		return RET_CODE_ERR_ARG;
	}

	*ppStreamInfo = &pAviDmuxCtlInfo->streamInfo[streamIndex];

	return CELL_OK;
}


int aviDmuxStart(SAviDmuxCtlInfo* pAviDmuxCtlInfo, int prio, size_t stacksize){
	pAviDmuxCtlInfo->errorCode = RET_CODE_ERR_INVALID;
	pAviDmuxCtlInfo->recvStatus.bReleaseDone = true;

	int	ret = sys_ppu_thread_create(&pAviDmuxCtlInfo->threadId, aviDmuxThread, (uintptr_t)pAviDmuxCtlInfo,
		prio, stacksize, SYS_PPU_THREAD_CREATE_JOINABLE, __func__);
	if(ret < CELL_OK){
		return ret;
	}

	return CELL_OK;
}

int aviDmuxEnd(SAviDmuxCtlInfo* pAviDmuxCtlInfo){
	uint64_t thrStat;
	int	ret = sys_ppu_thread_join(pAviDmuxCtlInfo->threadId, &thrStat);
	if(ret < CELL_OK){
		return ret;
	}

	return pAviDmuxCtlInfo->errorCode;
}


int aviDmuxEnableEs(SAviDmuxCtlInfo* pAviDmuxCtlInfo, const SAviDmuxStreamInfo* pStreamInfo,
	aviDmuxCbFunc cbNotifyFunc, void* cbNotifyArg, AviDmuxEsHandle* pEsHandle)
{
	const uint32_t	validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
	if(pStreamInfo < pAviDmuxCtlInfo->streamInfo
	|| pStreamInfo >= pAviDmuxCtlInfo->streamInfo + validStreamNum){
		return RET_CODE_ERR_ARG;
	}

	uint32_t	streamIndex = (uint32_t)(pStreamInfo - pAviDmuxCtlInfo->streamInfo);
	SAviDmuxEsCtlInfo*	pEsCtlInfo = &pAviDmuxCtlInfo->esCtlInfo[streamIndex];
	if(pEsCtlInfo->pHandle){
		return RET_CODE_ERR_ARG;
	}

	aviDmuxEsParseFunc*	ppEsParseFunc = &pAviDmuxCtlInfo->esParseFunc[streamIndex];
	uint32_t			auBufferNum = 0;
	uint32_t			maxAuSize = 0;
	uint32_t			parseBufferSize = 0;
	if(pStreamInfo->fccType == AVIDMUX_FCC_auds){
		switch(pStreamInfo->specInfo.waveFormatEx.wFormatTag){
		case AVIDMUX_WAVE_FORMAT_MPEGLAYER3:
		case AVIDMUX_WAVE_FORMAT_MPEG:
			*ppEsParseFunc = &aviDmuxParseMpegAudioEs;
			auBufferNum = 48000 / 1152 + 2;
			maxAuSize = 2048;
			parseBufferSize = pStreamInfo->maxDataSize * 2;
			break;
		case AVIDMUX_WAVE_FORMAT_AC3:
			*ppEsParseFunc = &aviDmuxParseAc3AudioEs;
			auBufferNum = 48000 / 1536 + 2;
			maxAuSize = 3840;
			parseBufferSize = pStreamInfo->maxDataSize * 2;
			break;
		}
	}
	if(*ppEsParseFunc == NULL){
		*ppEsParseFunc = &aviDmuxParseDefaultEs;
		auBufferNum = pStreamInfo->dwRate / pStreamInfo->dwScale + 2;
		maxAuSize = ROUNDUP(pStreamInfo->maxDataSize, ALIGN_128BYTE);
		parseBufferSize = 0;
	}
	DP("streamIndex: %u, auBufferNum: %u\n", streamIndex, auBufferNum);

	pEsCtlInfo->positionIndex	= 0;
	pEsCtlInfo->cbNotifyFunc	= cbNotifyFunc;
	pEsCtlInfo->cbNotifyArg		= cbNotifyArg;
	pEsCtlInfo->pAuInfo			= (SAviDmuxAuInfo*)malloc(sizeof(SAviDmuxAuInfo) * auBufferNum);
	pEsCtlInfo->pAuBuffer		= AlignedAlloc(maxAuSize * auBufferNum, ALIGN_128BYTE);
	pEsCtlInfo->auBufferNum		= auBufferNum;
	pEsCtlInfo->maxAuSize		= maxAuSize;
	pEsCtlInfo->parseBufferSize	= parseBufferSize;
	if(parseBufferSize){
		pEsCtlInfo->pParseBuffer= (uint8_t*)malloc(parseBufferSize);
	}else{
		pEsCtlInfo->pParseBuffer= NULL;
	}

	if(utilMonitorInit(&pEsCtlInfo->monitor)){
		free(pEsCtlInfo->pAuInfo);
		AlignedFree(pEsCtlInfo->pAuBuffer);
		if(parseBufferSize){
			free(pEsCtlInfo->pParseBuffer);
		}
		return RET_CODE_ERR_FATAL;
	}
	utilQueueInit(&pEsCtlInfo->auWriteQueue, &pEsCtlInfo->monitor,
		sizeof(SAviDmuxAuInfo*), auBufferNum);
	utilQueueInit(&pEsCtlInfo->auReadQueue, &pEsCtlInfo->monitor,
		sizeof(SAviDmuxAuInfo*), auBufferNum);
	utilQueueInit(&pEsCtlInfo->auReleaseQueue, &pEsCtlInfo->monitor,
		sizeof(SAviDmuxAuInfo*), auBufferNum);

	for(uint32_t auIndex = 0; auIndex < auBufferNum; ++auIndex){
		pEsCtlInfo->pAuInfo[auIndex].auAddr =
			pEsCtlInfo->pAuBuffer + auIndex * maxAuSize;
		SAviDmuxAuInfo*	pAuInfo = &pEsCtlInfo->pAuInfo[auIndex];
		utilQueuePush(&pEsCtlInfo->auWriteQueue, &pAuInfo, false);
	}

	*pEsHandle = (void*)pEsCtlInfo;

	pEsCtlInfo->pHandle = pAviDmuxCtlInfo;

	return CELL_OK;
}

int aviDmuxDisableEs(AviDmuxEsHandle esHandle){
	int32_t	ret;
	if(esHandle == NULL){
		return RET_CODE_ERR_ARG;
	}

	SAviDmuxEsCtlInfo*	pEsCtlInfo = (SAviDmuxEsCtlInfo*)esHandle;
	if(pEsCtlInfo->pHandle == NULL){
		return RET_CODE_ERR_ARG;
	}

	utilQueueFin(&pEsCtlInfo->auReleaseQueue);
	utilQueueFin(&pEsCtlInfo->auReadQueue);
	utilQueueFin(&pEsCtlInfo->auWriteQueue);
	ret = utilMonitorFin(&pEsCtlInfo->monitor);

	if(pEsCtlInfo->pParseBuffer){
		free(pEsCtlInfo->pParseBuffer);
	}
	free(pEsCtlInfo->pAuInfo);
	AlignedFree(pEsCtlInfo->pAuBuffer);

	pEsCtlInfo->pHandle = NULL;

	return ret;
}

int aviDmuxGetAu(AviDmuxEsHandle esHandle, const SAviDmuxAuInfo **auInfo,
	void **auSpecificInfo)
{
	if(esHandle == NULL){
		return RET_CODE_ERR_ARG;
	}
	SAviDmuxEsCtlInfo*	pEsCtlInfo = (SAviDmuxEsCtlInfo*)esHandle;

	SAviDmuxAuInfo*	pAuInfo;
	if(utilQueuePop(&pEsCtlInfo->auReadQueue, &pAuInfo, false)){
		return RET_CODE_ERR_EMPTY;
	}

	*auInfo = pAuInfo;
	if(auSpecificInfo)
		*auSpecificInfo = NULL;

	utilQueuePush(&pEsCtlInfo->auReleaseQueue, &pAuInfo, true);

	return CELL_OK;
}

int aviDmuxReleaseAu(AviDmuxEsHandle esHandle){
	if(esHandle == NULL){
		return RET_CODE_ERR_ARG;
	}
	SAviDmuxEsCtlInfo*	pEsCtlInfo = (SAviDmuxEsCtlInfo*)esHandle;

	SAviDmuxAuInfo*	pAuInfo;
	if(utilQueuePop(&pEsCtlInfo->auReleaseQueue, &pAuInfo, false)){
		return RET_CODE_ERR_SEQ;
	}

	utilQueuePush(&pEsCtlInfo->auWriteQueue, &pAuInfo, true);

	SAviDmuxCtlInfo*	pAviDmuxCtlInfo = (SAviDmuxCtlInfo*)pEsCtlInfo->pHandle;
	utilMonitorLock(&pAviDmuxCtlInfo->umInput, 0);
	pAviDmuxCtlInfo->recvStatus.bReleaseDone = true;
	utilMonitorSignal(&pAviDmuxCtlInfo->umInput);
	utilMonitorUnlock(&pAviDmuxCtlInfo->umInput);

	return CELL_OK;
}



/* internal functions  */

/* AVI file parser */

static unsigned int aviDmuxFccToLower(unsigned int fcc){
	UAviDmuxFcc		srcFcc;
	UAviDmuxFcc		destFcc;
	if(fcc == AVIDMUX_CODEC_TYPE_ANY){
		destFcc.dw = fcc;
	}else{
		srcFcc.dw = fcc;
		destFcc.fcc[0] = tolower(srcFcc.fcc[0]);
		destFcc.fcc[1] = tolower(srcFcc.fcc[1]);
		destFcc.fcc[2] = tolower(srcFcc.fcc[2]);
		destFcc.fcc[3] = tolower(srcFcc.fcc[3]);
	}
	return destFcc.dw;
}

static int aviDmuxReadRiffAviChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo){
	SAviDmuxChunk		riffHeader;
	if(!aviDmuxReadRiffAviHeader(pAviDmuxCtlInfo, &riffHeader)){
		EINFO(RET_CODE_ERR_ARG);
		return RET_CODE_ERR_ARG;
	}

	SAviDmuxChunk		chunk;
	SAviDmuxList		list;
	bool				bRet = true;
	int					csi = -1;
	unsigned int		streamOffset = 0;
	const long long 	fileSize = utilBfrGetFileSize(&pAviDmuxCtlInfo->bfr);
	SAviDmuxFileInfo*	pFileInfo = &pAviDmuxCtlInfo->fileInfo;
	pFileInfo->validStreamNum = 0;
	pFileInfo->odmlTotalFrames = 0;

	while(bRet && utilBfrGetPos(&pAviDmuxCtlInfo->bfr) < fileSize &&
		  aviDmuxReadNextFcc(pAviDmuxCtlInfo, &chunk.dwFourCC))
	{
		unsigned int	skipSize = 0;
		unsigned int	type = chunk.dwFourCC;
		if(type & 0x80808080){
			EMSG("invalid fcc found, at 0x%llx.(fcc=0x%08x)\n",
				utilBfrGetPos(&pAviDmuxCtlInfo->bfr) - 4LL, type);
			break;
		}

		if(type == AVIDMUX_FCC_RIFF){
			/* No check AVIX Chunk */
			break;
		}else if(type == AVIDMUX_FCC_LIST){
			list.dwList = chunk.dwFourCC;
			bRet = aviDmuxReadListHeader(pAviDmuxCtlInfo, &list);
			if(!bRet)	break;

			type = list.dwFourCC;
			switch(type){
			case AVIDMUX_FCC_hdrl:
				break;
			case AVIDMUX_FCC_strl:
				++csi;
				break;
			case AVIDMUX_FCC_movi:
				pFileInfo->validStreamNum = (csi >= (int)AVIDMUX_MAX_STREAM_NUM)
					? AVIDMUX_MAX_STREAM_NUM: csi + 1;
				aviDmuxCheckOdmlIndex(pAviDmuxCtlInfo);
				if(pFileInfo->odmlIndexValid == false){	/* for AVI 1.0 */
					streamOffset = utilBfrGetPos(&pAviDmuxCtlInfo->bfr) - 4;
				}
				aviDmuxSkip(pAviDmuxCtlInfo, list.dwSize - 4);
				break;
			case AVIDMUX_FCC_odml:
				break;
			default:
				break;
			}
		}else{
			bRet = aviDmuxReadChunkHeader(pAviDmuxCtlInfo, &chunk);
			if(!bRet)	break;

			skipSize = chunk.dwSize;
			switch(type){
			case AVIDMUX_FCC_avih:
				bRet = aviDmuxReadAvihChunk(pAviDmuxCtlInfo, &skipSize);
				break;
			case AVIDMUX_FCC_strh:
				if(csi >= (int)AVIDMUX_MAX_STREAM_NUM){
					break;
				}
				bRet = aviDmuxReadStrhChunk(pAviDmuxCtlInfo, csi, &skipSize);
				break;
			case AVIDMUX_FCC_strf:
				bRet = aviDmuxReadStrfChunk(pAviDmuxCtlInfo, &pAviDmuxCtlInfo->streamInfo[csi], &skipSize);
				break;
			case AVIDMUX_FCC_idx1:
				if(pFileInfo->odmlIndexValid == false){	/* for AVI 1.0 */
					bRet = aviDmuxReadIdx1Chunk(pAviDmuxCtlInfo, streamOffset, &skipSize);
				}
				break;
			case AVIDMUX_FCC_indx:
				bRet = aviDmuxReadSuperIndexChunk(pAviDmuxCtlInfo, csi, &skipSize);
				break;
			case AVIDMUX_FCC_dmlh:
				bRet = aviDmuxReadDmlhChunk(pAviDmuxCtlInfo, &skipSize);
				break;
			case AVIDMUX_FCC_JUNK:
			default:
				break;
			}
			if(skipSize){	/* Skip unnecessary chunk */
				aviDmuxSkip(pAviDmuxCtlInfo, skipSize);
			}
		}
	}

	if(bRet && pFileInfo->validStreamNum == pFileInfo->dwStreams){
		bRet = aviDmuxReadOpenDmlIndex(pAviDmuxCtlInfo);
	}else{
		bRet = false;
	}

	return (bRet)? CELL_OK: RET_CODE_ERR_FATAL;
}

static bool aviDmuxReadRiffAviHeader(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxChunk* pRiffHeader){
	if(utilBfrRead(&pAviDmuxCtlInfo->bfr, pRiffHeader, sizeof(SAviDmuxChunk))
	|| pRiffHeader->dwFourCC != AVIDMUX_FCC_RIFF
	|| pRiffHeader->data != AVIDMUX_FCC_AVI){
		return false;
	}
	pRiffHeader->dwSize = utilSwap32(pRiffHeader->dwSize);
	return true;
}

static bool aviDmuxReadNextFcc(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int* pFcc){
	const unsigned int	readSize = 4;
	if(utilBfrRead(&pAviDmuxCtlInfo->bfr, pFcc, readSize)){
		return false;
	}
	return true;
}

static bool aviDmuxReadListHeader(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxList* pList){
	const unsigned int	readSize = 8;
	if(utilBfrRead(&pAviDmuxCtlInfo->bfr, &pList->dwSize, readSize)){
		return false;
	}
	pList->dwSize = utilSwap32(pList->dwSize);
	return true;
}

static bool aviDmuxReadChunkHeader(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxChunk* pChunk){
	const unsigned int	readSize = 4;
	if(utilBfrRead(&pAviDmuxCtlInfo->bfr, &pChunk->dwSize, readSize)){
		return false;
	}
	pChunk->dwSize = utilSwap32(pChunk->dwSize);
	return true;
}

static bool aviDmuxSkip(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int size){
	size = (size + 1) & ~1;
	if(utilBfrSeek(&pAviDmuxCtlInfo->bfr, size, SEEK_CUR)){
		return false;
	}
	return true;
}

static bool aviDmuxReadData(SAviDmuxCtlInfo* pAviDmuxCtlInfo, void* pData, unsigned int size, unsigned int* pReadSize){
	if(utilBfrRead(&pAviDmuxCtlInfo->bfr, pData, size)){
		return false;
	}
	if(pReadSize)	*pReadSize = size;
	return true;
}

static void aviDmuxCheckOdmlIndex(SAviDmuxCtlInfo* pAviDmuxCtlInfo){
	SAviDmuxFileInfo*	pFileInfo = &pAviDmuxCtlInfo->fileInfo;
	pFileInfo->odmlIndexValid = false;
	if(pFileInfo->odmlTotalFrames >= pFileInfo->dwTotalFrames){
		unsigned int	odmlIndexValidCount = 0;
		for(unsigned int streamIndex = 0; streamIndex < pFileInfo->validStreamNum; ++streamIndex){
			if(pAviDmuxCtlInfo->superIndex[streamIndex]){
				++odmlIndexValidCount;
			}
		}
		if(odmlIndexValidCount == pFileInfo->validStreamNum){
			pFileInfo->odmlIndexValid = true;;
		}
	}
}

static bool aviDmuxReadAvihChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int* pSkipSize){
	bool				bRet = true;
	unsigned int		readSize = 0;
	SAviDmuxFileInfo	header;
	bRet = aviDmuxReadData(pAviDmuxCtlInfo, &header, 56, &readSize);
	*pSkipSize -= readSize;
	if(bRet){
		SAviDmuxFileInfo*	pFileInfo = &pAviDmuxCtlInfo->fileInfo;
		pFileInfo->dwMicroSecPerFrame = utilSwap32(header.dwMicroSecPerFrame);
		pFileInfo->dwMaxBytesPerSec = utilSwap32(header.dwMaxBytesPerSec);
		pFileInfo->dwPaddingGranularity = utilSwap32(header.dwPaddingGranularity);
		pFileInfo->dwFlags = utilSwap32(header.dwFlags);
		pFileInfo->dwTotalFrames = utilSwap32(header.dwTotalFrames);
		pFileInfo->dwInitialFrames = utilSwap32(header.dwInitialFrames);
		pFileInfo->dwStreams = utilSwap32(header.dwStreams);
		pFileInfo->dwSuggestedBufferSize = utilSwap32(header.dwSuggestedBufferSize);
		pFileInfo->dwWidth = utilSwap32(header.dwWidth);
		pFileInfo->dwHeight = utilSwap32(header.dwHeight);
	}
	return bRet;
}

static bool aviDmuxReadDmlhChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int* pSkipSize){
	bool						bRet = true;
	unsigned int				readSize = 0;
	SAviDmuxExtendedAviHeader	header;
	bRet = aviDmuxReadData(pAviDmuxCtlInfo, &header, sizeof(header), &readSize);
	*pSkipSize -= readSize;
	if(bRet){
		pAviDmuxCtlInfo->fileInfo.odmlTotalFrames = utilSwap32(header.dwTotalFrames);
	}
	return bRet;
}

static bool aviDmuxReadStrhChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int csi, unsigned int* pSkipSize){
	bool				bRet = true;
	unsigned int		readSize = 0;
	SAviDmuxStreamInfo	header;
	bRet = aviDmuxReadData(pAviDmuxCtlInfo, &header, 56, &readSize);
	*pSkipSize -= readSize;
	if(bRet){
		SAviDmuxStreamInfo*	pStreamInfo = &pAviDmuxCtlInfo->streamInfo[csi];
		pStreamInfo->fccType = header.fccType;
		pStreamInfo->fccHandler = header.fccHandler;
		pStreamInfo->dwFlags = utilSwap32(header.dwFlags);
		pStreamInfo->wPriority = utilSwap16(header.wPriority);
		pStreamInfo->wLanguage = utilSwap16(header.wLanguage);
		pStreamInfo->dwInitialFrames = utilSwap32(header.dwInitialFrames);
		pStreamInfo->dwScale = utilSwap32(header.dwScale);
		pStreamInfo->dwRate = utilSwap32(header.dwRate);
		pStreamInfo->dwStart = utilSwap32(header.dwStart);
		pStreamInfo->dwLength = utilSwap32(header.dwLength);
		pStreamInfo->dwSuggestedBufferSize = utilSwap32(header.dwSuggestedBufferSize);
		pStreamInfo->dwQuality = utilSwap32(header.dwQuality);
		pStreamInfo->dwSampleSize = utilSwap32(header.dwSampleSize);
		pStreamInfo->rcFrame.left = utilSwap16(header.rcFrame.left);
		pStreamInfo->rcFrame.top = utilSwap16(header.rcFrame.top);
		pStreamInfo->rcFrame.right = utilSwap16(header.rcFrame.right);
		pStreamInfo->rcFrame.bottom = utilSwap16(header.rcFrame.bottom);
	}
	return bRet;
}

static bool aviDmuxReadStrfChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, SAviDmuxStreamInfo* pStreamInfo, unsigned int* pSkipSize){
	bool			bRet = true;
	unsigned int	readSize = 0;
	UAviDmuxStreamSpecInfo	specInfo;
	UAviDmuxStreamSpecInfo*	pDstSpecInfo = &pStreamInfo->specInfo;

	switch(pStreamInfo->fccType){

	case AVIDMUX_FCC_vids:
		bRet = aviDmuxReadData(pAviDmuxCtlInfo, &specInfo.bmpInfoHeader, sizeof(specInfo.bmpInfoHeader), &readSize);
		if(!bRet)	break;
		*pSkipSize -= readSize;
		pDstSpecInfo->bmpInfoHeader.biSize = utilSwap32(specInfo.bmpInfoHeader.biSize);
		pDstSpecInfo->bmpInfoHeader.biWidth = utilSwap32(specInfo.bmpInfoHeader.biWidth);
		pDstSpecInfo->bmpInfoHeader.biHeight = utilSwap32(specInfo.bmpInfoHeader.biHeight);
		pDstSpecInfo->bmpInfoHeader.biPlanes = utilSwap16(specInfo.bmpInfoHeader.biPlanes);
		pDstSpecInfo->bmpInfoHeader.biBitCount = utilSwap16(specInfo.bmpInfoHeader.biBitCount);
		pDstSpecInfo->bmpInfoHeader.biCompression = specInfo.bmpInfoHeader.biCompression;
		pDstSpecInfo->bmpInfoHeader.biSizeImage = utilSwap32(specInfo.bmpInfoHeader.biSizeImage);
		pDstSpecInfo->bmpInfoHeader.biXPelsPerMeter = utilSwap32(specInfo.bmpInfoHeader.biXPelsPerMeter);
		pDstSpecInfo->bmpInfoHeader.biYPelsPerMeter = utilSwap32(specInfo.bmpInfoHeader.biYPelsPerMeter);
		pDstSpecInfo->bmpInfoHeader.biClrUsed = utilSwap32(specInfo.bmpInfoHeader.biClrUsed);
		pDstSpecInfo->bmpInfoHeader.biClrImportant = utilSwap32(specInfo.bmpInfoHeader.biClrImportant);
		break;

	case AVIDMUX_FCC_auds:
		bRet = aviDmuxReadData(pAviDmuxCtlInfo, &specInfo.waveFormatEx, 16, &readSize);
		if(!bRet)	break;
		*pSkipSize -= readSize;
		pDstSpecInfo->waveFormatEx.wFormatTag = utilSwap16(specInfo.waveFormatEx.wFormatTag);
		pDstSpecInfo->waveFormatEx.nChannels = utilSwap16(specInfo.waveFormatEx.nChannels);
		pDstSpecInfo->waveFormatEx.nSamplesPerSec = utilSwap32(specInfo.waveFormatEx.nSamplesPerSec);
		pDstSpecInfo->waveFormatEx.nAvgBytesPerSec = utilSwap32(specInfo.waveFormatEx.nAvgBytesPerSec);
		pDstSpecInfo->waveFormatEx.nBlockAlign = utilSwap16(specInfo.waveFormatEx.nBlockAlign);
		pDstSpecInfo->waveFormatEx.wBitsPerSample = utilSwap16(specInfo.waveFormatEx.wBitsPerSample);
		if(specInfo.waveFormatEx.wFormatTag == AVIDMUX_WAVE_FORMAT_PCM){
			pDstSpecInfo->waveFormatEx.cbSize = 0;
		}else{
			bRet = aviDmuxReadData(pAviDmuxCtlInfo, &specInfo.waveFormatEx.cbSize, 2, &readSize);
			if(!bRet)	break;
			*pSkipSize -= readSize;
			pDstSpecInfo->waveFormatEx.cbSize = utilSwap16(specInfo.waveFormatEx.cbSize);
		}

		switch(pDstSpecInfo->waveFormatEx.wFormatTag){
		case AVIDMUX_WAVE_FORMAT_MPEG:
			bRet = aviDmuxReadData(pAviDmuxCtlInfo, &specInfo.mpeg1WaveFormat.fwHeadLayer, 22, &readSize);
			if(!bRet)	break;
			*pSkipSize -= readSize;
			pDstSpecInfo->mpeg1WaveFormat.fwHeadLayer = utilSwap16(specInfo.mpeg1WaveFormat.fwHeadLayer);
			pDstSpecInfo->mpeg1WaveFormat.dwHeadBitrate = utilSwap32(specInfo.mpeg1WaveFormat.dwHeadBitrate);
			pDstSpecInfo->mpeg1WaveFormat.fwHeadMode = utilSwap16(specInfo.mpeg1WaveFormat.fwHeadMode);
			pDstSpecInfo->mpeg1WaveFormat.fwHeadModeExt = utilSwap16(specInfo.mpeg1WaveFormat.fwHeadModeExt);
			pDstSpecInfo->mpeg1WaveFormat.wHeadEmphasis = utilSwap16(specInfo.mpeg1WaveFormat.wHeadEmphasis);
			pDstSpecInfo->mpeg1WaveFormat.fwHeadFlags = utilSwap16(specInfo.mpeg1WaveFormat.fwHeadFlags);
			pDstSpecInfo->mpeg1WaveFormat.dwPTSLow = utilSwap32(specInfo.mpeg1WaveFormat.dwPTSLow);
			pDstSpecInfo->mpeg1WaveFormat.dwPTSHigh = utilSwap32(specInfo.mpeg1WaveFormat.dwPTSHigh);
			break;

		case AVIDMUX_WAVE_FORMAT_MPEGLAYER3:
			bRet = aviDmuxReadData(pAviDmuxCtlInfo, &specInfo.mpegLayer3WaveFormat.wID, 12, &readSize);
			if(!bRet)	break;
			*pSkipSize -= readSize;
			pDstSpecInfo->mpegLayer3WaveFormat.wID = utilSwap16(specInfo.mpegLayer3WaveFormat.wID);
			pDstSpecInfo->mpegLayer3WaveFormat.fdwFlags = utilSwap32(specInfo.mpegLayer3WaveFormat.fdwFlags);
			pDstSpecInfo->mpegLayer3WaveFormat.nBlockSize = utilSwap16(specInfo.mpegLayer3WaveFormat.nBlockSize);
			pDstSpecInfo->mpegLayer3WaveFormat.nFramesPerBlock = utilSwap16(specInfo.mpegLayer3WaveFormat.nFramesPerBlock);
			pDstSpecInfo->mpegLayer3WaveFormat.nCodecDelay = utilSwap16(specInfo.mpegLayer3WaveFormat.nCodecDelay);
			break;
		}

		break;
	}

	return bRet;
}

static bool aviDmuxReadIdx1Chunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int streamOffset, unsigned int* pSkipSize){
	bool					bRet = true;
	unsigned int			readSize = 0;
	SAviDmuxOldIndexEntry	entry;
	const unsigned int		end = *pSkipSize / sizeof(entry);
	const unsigned int		validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
	char					szStreamIndex[] = "00\0";
	unsigned char*			pCkid = NULL;
	const long long			startPos = utilBfrGetPos(&pAviDmuxCtlInfo->bfr);
	unsigned int			indexNum[validStreamNum];

	/* clear index num. */
	for(unsigned int streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
		indexNum[streamIndex] = 0;
	}

	/* counts number of indexes. */
	for(unsigned int index = 0; bRet && index < end; ++index){
		if((bRet = aviDmuxReadData(pAviDmuxCtlInfo, &entry, sizeof(entry), &readSize)) == false){
			break;
		}
		pCkid = (unsigned char*)&entry.ckid;
		szStreamIndex[0] = pCkid[0];
		szStreamIndex[1] = pCkid[1];
		unsigned int streamIndex = (unsigned int)strtoul(szStreamIndex, NULL, 10);
		if(streamIndex < validStreamNum){
			++indexNum[streamIndex];
		}
		if(index == 0 && streamOffset <= utilSwap32(entry.dwChunkOffset) - 4){
			streamOffset = 0;
		}
	}
	if(bRet == false){
		return bRet;
	}

	/* allocate index store space. */
	for(unsigned int streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
		pAviDmuxCtlInfo->streamPosition[streamIndex] = (SAviDmuxPosition*)malloc(
			sizeof(SAviDmuxPosition) * indexNum[streamIndex]);
		pAviDmuxCtlInfo->streamInfo[streamIndex].indexNum = indexNum[streamIndex];
		indexNum[streamIndex] = 0;
	}

	/* create index. */
	if(utilBfrSeek(&pAviDmuxCtlInfo->bfr, startPos, SEEK_SET)){
		return false;
	}
	for(unsigned int index = 0; bRet && index < end; ++index){
		if((bRet = aviDmuxReadData(pAviDmuxCtlInfo, &entry, sizeof(entry), &readSize)) == false){
			break;
		}
		*pSkipSize -= readSize;
		pCkid = (unsigned char*)&entry.ckid;
		szStreamIndex[0] = pCkid[0];
		szStreamIndex[1] = pCkid[1];
		unsigned int streamIndex = (unsigned int)strtoul(szStreamIndex, NULL, 10);
		if(streamIndex < validStreamNum){
			unsigned long long	offset = utilSwap32(entry.dwChunkOffset) + streamOffset + 8;
			unsigned int		size = utilSwap32(entry.dwChunkLength);
			unsigned int		flags = utilSwap32(entry.dwFlags);
			pAviDmuxCtlInfo->streamPosition[streamIndex][indexNum[streamIndex]++]
				= (SAviDmuxPosition){offset, size, flags};
			if(pAviDmuxCtlInfo->streamInfo[streamIndex].maxDataSize < size){
				pAviDmuxCtlInfo->streamInfo[streamIndex].maxDataSize = size;
			}
		}
	}
	if(!bRet){
		for(unsigned int streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
			pAviDmuxCtlInfo->streamInfo[streamIndex].indexNum = 0;
		}
	}
	return bRet;
}

static bool aviDmuxReadSuperIndexChunk(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int csi, unsigned int* pSkipSize){
	bool					bRet = true;
	unsigned int			readSize = 0;
	SAviDmuxIndexWoCh		header;
	SAviDmuxSpuerIndexEntry	entry;

	bRet = aviDmuxReadData(pAviDmuxCtlInfo, &header, sizeof(header), &readSize);
	*pSkipSize -= readSize;
	if(!bRet || header.bIndexType != AVIDMUX_AVI_INDEX_OF_INDEXES){
		return false;
	}
	unsigned int		indexNum = utilSwap32(header.nEntriesInUse);
	unsigned long long*	pSuperIndex = (unsigned long long*)malloc(sizeof(entry) * indexNum + 8);
	pAviDmuxCtlInfo->superIndex[csi] = pSuperIndex;
	*pSuperIndex++ = indexNum;

	for(unsigned int index = 0; bRet && index < indexNum; ++index){
		bRet = aviDmuxReadData(pAviDmuxCtlInfo, &entry, sizeof(entry), &readSize);
		*pSkipSize -= readSize;

		*pSuperIndex++ = utilSwap64(entry.qwOffset);
		*pSuperIndex++ = ((unsigned long long)utilSwap32(entry.dwSize) << 32ULL) | utilSwap32(entry.dwDuration);
	}

	return bRet;
}

static bool aviDmuxReadStdFieldIndex(SAviDmuxCtlInfo* pAviDmuxCtlInfo, unsigned int streamIndex, unsigned int* pMaxDataSize){
	bool				bRet = true;
	const unsigned int	end = pAviDmuxCtlInfo->superIndex[streamIndex][0];
	SAviDmuxSpuerIndexEntry*	superIndexes =
		(SAviDmuxSpuerIndexEntry*)&pAviDmuxCtlInfo->superIndex[streamIndex][1];
	SAviDmuxIndexWoCh	header;
	unsigned int		indexNum = 0;

	/* counts number of indexes. */
	for(unsigned int index = 0; bRet && index < end; ++index){
		if(utilBfrSeek(&pAviDmuxCtlInfo->bfr, superIndexes[index].qwOffset + 8, SEEK_SET)){
			bRet = false;
			break;
		}
		if(utilBfrRead(&pAviDmuxCtlInfo->bfr, &header, sizeof(header))){
			bRet = false;
			break;
		}
		const unsigned int	nEntriesInUse = utilSwap32(header.nEntriesInUse);
		if(header.bIndexSubType == AVIDMUX_AVI_INDEX_OF_SUB_2FIELD){
			indexNum += nEntriesInUse * 2;
		}else{
			indexNum += nEntriesInUse;
		}
	}
	if(bRet){
		pAviDmuxCtlInfo->streamPosition[streamIndex] =
			(SAviDmuxPosition*)malloc(sizeof(SAviDmuxPosition) * indexNum);
		pAviDmuxCtlInfo->streamInfo[streamIndex].indexNum = indexNum;
		indexNum = 0;
	}

	for(unsigned int index = 0; bRet && index < end; ++index){
		if(utilBfrSeek(&pAviDmuxCtlInfo->bfr, superIndexes[index].qwOffset + 8, SEEK_SET)){
			bRet = false;
			break;
		}
		if(utilBfrRead(&pAviDmuxCtlInfo->bfr, &header, sizeof(header))){
			bRet = false;
			break;
		}
		const unsigned long long	qwBaseOffset =
			utilSwap64(*(unsigned long long*)header.dwReserved);
		const unsigned int	nEntriesInUse = utilSwap32(header.nEntriesInUse);
		if(header.bIndexSubType == AVIDMUX_AVI_INDEX_OF_SUB_2FIELD){
			SAviDmuxFieldIndexEntry	fieldEntry;
			for(unsigned int entryIndex = 0; entryIndex < nEntriesInUse; ++entryIndex){
				if(utilBfrRead(&pAviDmuxCtlInfo->bfr, &fieldEntry, sizeof(fieldEntry))){
					bRet = false;
					break;
				}
				const unsigned int	offset1 = utilSwap32(fieldEntry.dwOffset);		// first field
				const unsigned int	sizeFlag = utilSwap32(fieldEntry.dwSize);		// total size & flag
				const unsigned int	offset2 = utilSwap32(fieldEntry.dwOffsetField2);// second field
				unsigned int		size = sizeFlag & 0x7FFFFFFF;
				unsigned int		flags = (sizeFlag & 0x80000000)? 0: AVIDMUX_AVIIF_KEYFRAME;
				unsigned int		size1 = offset2 - offset1;
				unsigned int		size2 = size - size1;
				pAviDmuxCtlInfo->streamPosition[streamIndex][indexNum++] =
					(SAviDmuxPosition){qwBaseOffset+offset1, size1, flags};
				pAviDmuxCtlInfo->streamPosition[streamIndex][indexNum++] =
					(SAviDmuxPosition){qwBaseOffset+offset2, size2,     0};
				if(*pMaxDataSize < size1){
					*pMaxDataSize = size1;
				}
				if(*pMaxDataSize < size2){
					*pMaxDataSize = size2;
				}
			}
		}else{
			SAviDmuxStdIndexEntry	stdEntry;
			for(unsigned int entryIndex = 0; entryIndex < nEntriesInUse; ++entryIndex){
				if(utilBfrRead(&pAviDmuxCtlInfo->bfr, &stdEntry, sizeof(stdEntry))){
					bRet = false;
					break;
				}
				const unsigned int	offset = utilSwap32(stdEntry.dwOffset);
				const unsigned int	sizeFlag = utilSwap32(stdEntry.dwSize);
				unsigned int		size = sizeFlag & 0x7FFFFFFF;
				unsigned int		flags = (sizeFlag & 0x80000000)? 0: AVIDMUX_AVIIF_KEYFRAME;
				pAviDmuxCtlInfo->streamPosition[streamIndex][indexNum++] =
					(SAviDmuxPosition){qwBaseOffset+offset, size, flags};
				if(*pMaxDataSize < size){
					*pMaxDataSize = size;
				}
			}
		}
	}
	if(!bRet){
		pAviDmuxCtlInfo->streamInfo[streamIndex].indexNum = 0;
	}
	return bRet;
}

static bool aviDmuxReadOpenDmlIndex(SAviDmuxCtlInfo* pAviDmuxCtlInfo){
	bool		bRet = true;

	SAviDmuxFileInfo*	pFileInfo = &pAviDmuxCtlInfo->fileInfo;
	if(pFileInfo->odmlIndexValid){	/* for OpenDML */
		const unsigned int	validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
		for(unsigned int streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
			bRet = aviDmuxReadStdFieldIndex(pAviDmuxCtlInfo, streamIndex,
				&pAviDmuxCtlInfo->streamInfo[streamIndex].maxDataSize);
			if(!bRet)	break;
		}
	}

	return bRet;
}

static int aviDmuxClearIndexInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo){
	const uint32_t	validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
	for(unsigned int streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
		if(pAviDmuxCtlInfo->superIndex[streamIndex]){
			free(pAviDmuxCtlInfo->superIndex[streamIndex]);
			pAviDmuxCtlInfo->superIndex[streamIndex] = NULL;
		}
		if(pAviDmuxCtlInfo->streamPosition[streamIndex]){
			free(pAviDmuxCtlInfo->streamPosition[streamIndex]);
			pAviDmuxCtlInfo->streamPosition[streamIndex] = NULL;
		}
	}

	return CELL_OK;
}


/* AVI demuxer */

static void aviDmuxThread(uint64_t arg){
	SAviDmuxCtlInfo*	pAviDmuxCtlInfo = (SAviDmuxCtlInfo*)(uintptr_t)arg;
	int32_t				ret;
	SAviDmuxStatus		threadStatus = pAviDmuxCtlInfo->recvStatus;
	const long long		fileSize = utilBfrGetFileSize(&pAviDmuxCtlInfo->bfr);
	const uint32_t		validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;

	DP("start...\n");

	aviDmuxClearParseInfo(pAviDmuxCtlInfo);

	while(1){
		aviDmuxWaitEvent(pAviDmuxCtlInfo, &threadStatus);

		if(pAviDmuxCtlInfo->errorCode != RET_CODE_ERR_INVALID){
			break;
		}
		if( !( commonGetMode( pAviDmuxCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) ){
			break;
		}

		uint32_t		targetStreamIndex;

		if(aviDmuxSearchTargetStream(pAviDmuxCtlInfo, validStreamNum, fileSize, &targetStreamIndex)){
			uint32_t			streamIndex = targetStreamIndex;
			SAviDmuxStreamInfo*	pStreamInfo = &pAviDmuxCtlInfo->streamInfo[streamIndex];
			SAviDmuxEsCtlInfo*	pEsCtlInfo = &pAviDmuxCtlInfo->esCtlInfo[streamIndex];
			SAviDmuxPosition*	pPosition = &pAviDmuxCtlInfo->streamPosition[streamIndex][pEsCtlInfo->positionIndex];

			ret = (*pAviDmuxCtlInfo->esParseFunc[streamIndex])
				(&pAviDmuxCtlInfo->bfr, pStreamInfo, pEsCtlInfo, pPosition);

			if(ret == RET_CODE_ERR_FULL){
				threadStatus.bReleaseDone = false;
			}else if(ret < CELL_OK){
				pAviDmuxCtlInfo->errorCode = RET_CODE_ERR_FATAL;
			}else{
				threadStatus.bReleaseDone = true;
				++pEsCtlInfo->positionIndex;
			}
		}else{
			for(unsigned int streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
				if(pAviDmuxCtlInfo->esCtlInfo[streamIndex].pHandle){
					(*pAviDmuxCtlInfo->esCtlInfo[streamIndex].cbNotifyFunc)
						(AVIDMUX_MSG_TYPE_DMUX_DONE, pAviDmuxCtlInfo->esCtlInfo[streamIndex].cbNotifyArg);
				}
			}
			pAviDmuxCtlInfo->errorCode = CELL_OK;
		}
		if(mm_shutdown || !is_bg_video) {pAviDmuxCtlInfo->errorCode = CELL_OK; break;}
	}

	if(pAviDmuxCtlInfo->errorCode < CELL_OK
	&& pAviDmuxCtlInfo->errorCode != RET_CODE_ERR_INVALID){
		commonErrorExit(pAviDmuxCtlInfo->pCommonCtlInfo, pAviDmuxCtlInfo->errorCode);
		EINFO(pAviDmuxCtlInfo->errorCode);
	}else{
		pAviDmuxCtlInfo->errorCode = CELL_OK;
	}

	DP("end...\n");

	sys_ppu_thread_exit(0);
}

static void aviDmuxWaitEvent(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	SAviDmuxStatus* pAviDmuxThreadStatus)
{
	SAviDmuxStatus* pRecvStatus = &pAviDmuxCtlInfo->recvStatus;

	utilMonitorLock(&pAviDmuxCtlInfo->umInput, 0);

	/* update receiving status with thread status */
	if(!pRecvStatus->bReleaseDone){
		pRecvStatus->bReleaseDone = pAviDmuxThreadStatus->bReleaseDone;
	}

	/* wait condition */
	while(	( commonGetMode( pAviDmuxCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) &&
			pAviDmuxCtlInfo->errorCode == RET_CODE_ERR_INVALID &&
			!pRecvStatus->bReleaseDone ){
		utilMonitorWait(&pAviDmuxCtlInfo->umInput, 0);
	}

	/* copy from receiving status to thread status */
	*pAviDmuxThreadStatus = *pRecvStatus;

	/* clear receiving status */
	pRecvStatus->bReleaseDone = false;

	utilMonitorUnlock(&pAviDmuxCtlInfo->umInput);
}


static bool aviDmuxSearchTargetStream(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	uint32_t validStreamNum, long long fileSize, uint32_t* pStreamId)
{
	uint32_t	targetStreamIndex = AVIDMUX_MAX_STREAM_NUM;
	uint64_t	offset = fileSize;
	bool		bRet = false;
	for(uint32_t streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
		SAviDmuxStreamInfo*	pStreamInfo = &pAviDmuxCtlInfo->streamInfo[streamIndex];
		SAviDmuxEsCtlInfo*	pEsCtlInfo = &pAviDmuxCtlInfo->esCtlInfo[streamIndex];
		SAviDmuxPosition*	pPosition = pAviDmuxCtlInfo->streamPosition[streamIndex];

		if(pEsCtlInfo->pHandle == NULL){
			continue;
		}
		if(pEsCtlInfo->parseLastError == RET_CODE_ERR_FULL){
			targetStreamIndex = streamIndex;
			offset = pPosition[pEsCtlInfo->positionIndex].offset;
			break;
		}
		if(pEsCtlInfo->positionIndex < pStreamInfo->indexNum
		&& pPosition[pEsCtlInfo->positionIndex].offset < offset){
			targetStreamIndex = streamIndex;
			offset = pPosition[pEsCtlInfo->positionIndex].offset;
		}
	}
	if(targetStreamIndex != AVIDMUX_MAX_STREAM_NUM){
		bRet = true;
		*pStreamId = targetStreamIndex;
	}else{
		*pStreamId = 0;
	}
	return bRet;
}


static int aviDmuxParseDefaultEs(UtilBufferedFileReader* pBfr,
	SAviDmuxStreamInfo* pStreamInfo, SAviDmuxEsCtlInfo* pEsCtlInfo,
	SAviDmuxPosition* pPosition)
{
	(void)pStreamInfo;
	SAviDmuxAuInfo*	pAuInfo;

	if(pPosition->size == 0){
		pEsCtlInfo->parseLastError = CELL_OK;
		return pEsCtlInfo->parseLastError;
	}

	if(utilQueuePop(&pEsCtlInfo->auWriteQueue, &pAuInfo, false)){
		pEsCtlInfo->parseLastError = RET_CODE_ERR_FULL;
		return pEsCtlInfo->parseLastError;
	}

	if(utilBfrSeek(pBfr, pPosition->offset, SEEK_SET)
	|| utilBfrUnbufferedRead(pBfr, pAuInfo->auAddr, pPosition->size)){
		utilQueuePush(&pEsCtlInfo->auWriteQueue, &pAuInfo, true);
		pEsCtlInfo->parseLastError = RET_CODE_ERR_FATAL;
		return pEsCtlInfo->parseLastError;
	}

	pAuInfo->auSize = pPosition->size;
	if(pEsCtlInfo->parseAuCount){
		pAuInfo->ptsUpper = AVIDMUX_TIME_STAMP_INVALID;
		pAuInfo->ptsLower = AVIDMUX_TIME_STAMP_INVALID;
	}else{
		pAuInfo->ptsUpper = 0;
		pAuInfo->ptsLower = 0;
	}
	pAuInfo->dtsUpper = AVIDMUX_TIME_STAMP_INVALID;
	pAuInfo->dtsLower = AVIDMUX_TIME_STAMP_INVALID;

	utilQueuePush(&pEsCtlInfo->auReadQueue, &pAuInfo, true);
	(*pEsCtlInfo->cbNotifyFunc)(AVIDMUX_MSG_TYPE_AU_FOUND, pEsCtlInfo->cbNotifyArg);

	++pEsCtlInfo->parseAuCount;

	pEsCtlInfo->parseLastError = CELL_OK;
	return pEsCtlInfo->parseLastError;
}

static unsigned int aviDmuxGetMpegAudioAuLength(unsigned int nID,
	unsigned int nLayer, unsigned int nBitrate, unsigned int nFs)
{
	static const unsigned int	pcnBitrate[16][5] = {
		//M2L2/3,  M2L1,   M1L3,   M1L2,   M1L1
		{	2,		2,		2,		2,		2	},
		{	8,		32,		32,		32,		32	},
		{	16,		48,		40,		48,		64	},
		{	24,		56,		48,		56,		96	},
		{	32,		64,		56,		64,		128	},
		{	40,		80,		64,		80,		160	},
		{	48,		96,		80,		96,		192	},
		{	56,		112,	96,		112,	224	},
		{	64,		128,	112,	128,	256	},
		{	80,		144,	128,	160,	288	},
		{	96,		160,	160,	192,	320	},
		{	112,	176,	192,	224,	352	},
		{	128,	192,	224,	256,	384	},
		{	144,	224,	256,	320,	416	},
		{	160,	256,	320,	384,	448	},
		{	2,		2,		2,		2,		2	}
	};
	static const unsigned int	nX[6] = {
		65306, 60000, 90000, 32653, 30000, 45000
	};
	unsigned int	nIndex = 0;
	if(nID){
		nIndex = (nLayer & 3) + 1;
	}else{
		nIndex = (nLayer < 3)? 0: 1;
	}
	unsigned int	nY = pcnBitrate[nBitrate&15][nIndex];
	if(nFs > 2){
		nFs = 2;
	}
	return nY * nX[nFs+nID*3] / 10000;
}

static int aviDmuxParseMpegAudioEs(UtilBufferedFileReader* pBfr,
	SAviDmuxStreamInfo* pStreamInfo, SAviDmuxEsCtlInfo* pEsCtlInfo,
	SAviDmuxPosition* pPosition)
{
	(void)pStreamInfo;
	SAviDmuxAuInfo*	pAuInfo;

	//E read
	if(pEsCtlInfo->parseLastError == CELL_OK){
		//E move data to front
		if(pEsCtlInfo->parseStartIndex){
			if(pEsCtlInfo->parseSize){
				memmove(pEsCtlInfo->pParseBuffer,
					pEsCtlInfo->pParseBuffer+pEsCtlInfo->parseStartIndex,
					pEsCtlInfo->parseSize);
			}
			pEsCtlInfo->parseStartIndex = 0;
		}
		//E read data from file
		if(utilBfrSeek(pBfr, pPosition->offset, SEEK_SET)
		|| utilBfrUnbufferedRead(pBfr,
		   pEsCtlInfo->pParseBuffer+pEsCtlInfo->parseSize, pPosition->size)){
			pEsCtlInfo->parseLastError = RET_CODE_ERR_FATAL;
			return pEsCtlInfo->parseLastError;
		}
		pEsCtlInfo->parseSize += pPosition->size;
	}
	//E parse
	while(pEsCtlInfo->parseSize > 4){
		//E header search
		int				headerOffset = -1;
		const uint32_t	end = pEsCtlInfo->parseStartIndex + pEsCtlInfo->parseSize - 1;
		for(uint32_t index = pEsCtlInfo->parseStartIndex; index < end; ++index){
			const uint16_t	syncWord = *(const uint16_t*)(pEsCtlInfo->pParseBuffer + index);
			if((syncWord & 0xFFF0) == 0xFFF0){
				headerOffset = index;
				break;
			}
		}
		//E failure of header search
		if(headerOffset < 0){
			DP("header search failed. (offset:%llu,size:%u,parseStartIndex:%u,parseSize:%u)\n",
				pPosition->offset, pPosition->size,
				pEsCtlInfo->parseStartIndex, pEsCtlInfo->parseSize);
			*pEsCtlInfo->pParseBuffer = pEsCtlInfo->pParseBuffer
				[pEsCtlInfo->parseStartIndex+pEsCtlInfo->parseSize-1];
			pEsCtlInfo->parseStartIndex = 0;
			pEsCtlInfo->parseSize = 1;
			pEsCtlInfo->parseSyncSearch = true;
			break;
		}
		pEsCtlInfo->parseSize -= headerOffset - pEsCtlInfo->parseStartIndex;
		pEsCtlInfo->parseStartIndex = headerOffset;
		//E calculate au size
		uint32_t	id = (pEsCtlInfo->pParseBuffer[headerOffset+1] >> 3) & 1;
		uint32_t	layer = (pEsCtlInfo->pParseBuffer[headerOffset+1] >> 1) & 3;
		uint32_t	bitrate = pEsCtlInfo->pParseBuffer[headerOffset+2] >> 4;
		uint32_t	fs = (pEsCtlInfo->pParseBuffer[headerOffset+2] >> 2) & 3;
		uint32_t	padding_bit = (pEsCtlInfo->pParseBuffer[headerOffset+2] >> 1) & 1;
		uint32_t	auSize = aviDmuxGetMpegAudioAuLength(id, layer, bitrate, fs) + padding_bit;
		if(pEsCtlInfo->parseSize < auSize){
			break;
		}
		//E next header check
		if(pEsCtlInfo->parseSyncSearch){
			if(pEsCtlInfo->parseSize < auSize+2){
				break;
			}
			const uint16_t nextSyncWord = *(const uint16_t*)
				(pEsCtlInfo->pParseBuffer + pEsCtlInfo->parseStartIndex + auSize);
			if((nextSyncWord & 0xFFF0) != 0xFFF0){
				DP("header search failed. (offset:%llu,size:%u,parseStartIndex:%u,parseSize:%u)\n",
					pPosition->offset, pPosition->size,
					pEsCtlInfo->parseStartIndex, pEsCtlInfo->parseSize);
				++pEsCtlInfo->parseStartIndex;
				--pEsCtlInfo->parseSize;
				continue;
			}
		}

		if(utilQueuePop(&pEsCtlInfo->auWriteQueue, &pAuInfo, false)){
			pEsCtlInfo->parseLastError = RET_CODE_ERR_FULL;
			return pEsCtlInfo->parseLastError;
		}

		memcpy(pAuInfo->auAddr,
			pEsCtlInfo->pParseBuffer+pEsCtlInfo->parseStartIndex, auSize);
		pEsCtlInfo->parseStartIndex += auSize;
		pEsCtlInfo->parseSize -= auSize;

		pAuInfo->auSize = auSize;
		if(pEsCtlInfo->parseAuCount){
			pAuInfo->ptsUpper = AVIDMUX_TIME_STAMP_INVALID;
			pAuInfo->ptsLower = AVIDMUX_TIME_STAMP_INVALID;
		}else{
			pAuInfo->ptsUpper = 0;
			pAuInfo->ptsLower = 0;
		}
		pAuInfo->dtsUpper = AVIDMUX_TIME_STAMP_INVALID;
		pAuInfo->dtsLower = AVIDMUX_TIME_STAMP_INVALID;

		utilQueuePush(&pEsCtlInfo->auReadQueue, &pAuInfo, true);
		(*pEsCtlInfo->cbNotifyFunc)(AVIDMUX_MSG_TYPE_AU_FOUND, pEsCtlInfo->cbNotifyArg);

		pEsCtlInfo->parseSyncSearch = false;
		++pEsCtlInfo->parseAuCount;
	}

	pEsCtlInfo->parseLastError = CELL_OK;
	return pEsCtlInfo->parseLastError;
}


static unsigned int aviDmuxGetAc3AudioAuLength(
	unsigned int nFs, unsigned int nFrmsizecod)
{
	static const unsigned int	nSize[38][3] = {
		{  128,  138,  192 },
		{  128,  140,  192 },
		{  160,  174,  240 },
		{  160,  176,  240 },
		{  192,  208,  288 },
		{  192,  210,  288 },
		{  224,  242,  336 },
		{  224,  244,  336 },
		{  256,  278,  384 },
		{  256,  280,  384 },
		{  320,  348,  480 },
		{  320,  350,  480 },
		{  384,  416,  576 },
		{  384,  418,  576 },
		{  448,  486,  672 },
		{  448,  488,  672 },
		{  512,  556,  768 },
		{  512,  558,  768 },
		{  640,  696,  960 },
		{  640,  698,  960 },
		{  768,  834, 1152 },
		{  768,  836, 1152 },
		{  896,  974, 1344 },
		{  896,  976, 1344 },
		{ 1024, 1114, 1536 },
		{ 1024, 1116, 1536 },
		{ 1280, 1392, 1920 },
		{ 1280, 1394, 1920 },
		{ 1536, 1670, 2304 },
		{ 1536, 1672, 2304 },
		{ 1792, 1950, 2688 },
		{ 1792, 1952, 2688 },
		{ 2048, 2228, 3072 },
		{ 2048, 2230, 3072 },
		{ 2304, 2506, 3456 },
		{ 2304, 2508, 3456 },
		{ 2560, 2786, 3840 },
		{ 2560, 2788, 3840 }
	};
	if(nFrmsizecod > 37){
		nFrmsizecod = 37;
	}
	if(nFs > 2){
		nFs = 2;
	}
	return nSize[nFrmsizecod][nFs];
}

static int aviDmuxParseAc3AudioEs(UtilBufferedFileReader* pBfr,
	SAviDmuxStreamInfo* pStreamInfo, SAviDmuxEsCtlInfo* pEsCtlInfo,
	SAviDmuxPosition* pPosition)
{
	(void)pStreamInfo;
	SAviDmuxAuInfo*	pAuInfo;

	//E read
	if(pEsCtlInfo->parseLastError == CELL_OK){
		//E move data to front
		if(pEsCtlInfo->parseStartIndex){
			if(pEsCtlInfo->parseSize){
				memmove(pEsCtlInfo->pParseBuffer,
					pEsCtlInfo->pParseBuffer+pEsCtlInfo->parseStartIndex,
					pEsCtlInfo->parseSize);
			}
			pEsCtlInfo->parseStartIndex = 0;
		}
		//E read data from file
		if(utilBfrSeek(pBfr, pPosition->offset, SEEK_SET)
		|| utilBfrUnbufferedRead(pBfr,
		   pEsCtlInfo->pParseBuffer+pEsCtlInfo->parseSize, pPosition->size)){
			pEsCtlInfo->parseLastError = RET_CODE_ERR_FATAL;
			return pEsCtlInfo->parseLastError;
		}
		pEsCtlInfo->parseSize += pPosition->size;
	}
	//E parse
	while(pEsCtlInfo->parseSize > 4){
		//E header search
		int				headerOffset = -1;
		const uint32_t	end = pEsCtlInfo->parseStartIndex + pEsCtlInfo->parseSize - 1;
		for(uint32_t index = pEsCtlInfo->parseStartIndex; index < end; ++index){
			const uint16_t	syncWord = *(const uint16_t*)(pEsCtlInfo->pParseBuffer + index);
			if(syncWord == 0x0B77){
				headerOffset = index;
				break;
			}
		}
		//E failure of header search
		if(headerOffset < 0){
			DP("header search failed. (offset:%llu,size:%u,parseStartIndex:%u,parseSize:%u)\n",
				pPosition->offset, pPosition->size,
				pEsCtlInfo->parseStartIndex, pEsCtlInfo->parseSize);
			*pEsCtlInfo->pParseBuffer = pEsCtlInfo->pParseBuffer
				[pEsCtlInfo->parseStartIndex+pEsCtlInfo->parseSize-1];
			pEsCtlInfo->parseStartIndex = 0;
			pEsCtlInfo->parseSize = 1;
			pEsCtlInfo->parseSyncSearch = true;
			break;
		}
		pEsCtlInfo->parseSize -= headerOffset - pEsCtlInfo->parseStartIndex;
		pEsCtlInfo->parseStartIndex = headerOffset;
		//E calculate au size
		uint32_t	fscod = pEsCtlInfo->pParseBuffer[headerOffset+4] >> 6;
		uint32_t	frmsizecod = pEsCtlInfo->pParseBuffer[headerOffset+4] & 0x3F;
		uint32_t	auSize = aviDmuxGetAc3AudioAuLength(fscod, frmsizecod);
		if(pEsCtlInfo->parseSize < auSize){
			break;
		}
		//E next header check
		if(pEsCtlInfo->parseSyncSearch){
			if(pEsCtlInfo->parseSize < auSize+2){
				break;
			}
			const uint16_t nextSyncWord = *(const uint16_t*)
				(pEsCtlInfo->pParseBuffer + pEsCtlInfo->parseStartIndex + auSize);
			if(nextSyncWord != 0x0B77){
				DP("header search failed. (offset:%llu,size:%u,parseStartIndex:%u,parseSize:%u)\n",
					pPosition->offset, pPosition->size,
					pEsCtlInfo->parseStartIndex, pEsCtlInfo->parseSize);
				++pEsCtlInfo->parseStartIndex;
				--pEsCtlInfo->parseSize;
				continue;
			}
		}

		if(utilQueuePop(&pEsCtlInfo->auWriteQueue, &pAuInfo, false)){
			pEsCtlInfo->parseLastError = RET_CODE_ERR_FULL;
			return pEsCtlInfo->parseLastError;
		}

		memcpy(pAuInfo->auAddr,
			pEsCtlInfo->pParseBuffer+pEsCtlInfo->parseStartIndex, auSize);
		pEsCtlInfo->parseStartIndex += auSize;
		pEsCtlInfo->parseSize -= auSize;

		pAuInfo->auSize = auSize;
		if(pEsCtlInfo->parseAuCount){
			pAuInfo->ptsUpper = AVIDMUX_TIME_STAMP_INVALID;
			pAuInfo->ptsLower = AVIDMUX_TIME_STAMP_INVALID;
		}else{
			pAuInfo->ptsUpper = 0;
			pAuInfo->ptsLower = 0;
		}
		pAuInfo->dtsUpper = AVIDMUX_TIME_STAMP_INVALID;
		pAuInfo->dtsLower = AVIDMUX_TIME_STAMP_INVALID;

		utilQueuePush(&pEsCtlInfo->auReadQueue, &pAuInfo, true);
		(*pEsCtlInfo->cbNotifyFunc)(AVIDMUX_MSG_TYPE_AU_FOUND, pEsCtlInfo->cbNotifyArg);

		pEsCtlInfo->parseSyncSearch = false;
		++pEsCtlInfo->parseAuCount;
	}

	pEsCtlInfo->parseLastError = CELL_OK;
	return pEsCtlInfo->parseLastError;
}

static void aviDmuxClearParseInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo){
	const uint32_t	validStreamNum = pAviDmuxCtlInfo->fileInfo.validStreamNum;
	for(uint32_t streamIndex = 0; streamIndex < validStreamNum; ++streamIndex){
		pAviDmuxCtlInfo->esCtlInfo[streamIndex].positionIndex = 0;
		pAviDmuxCtlInfo->esCtlInfo[streamIndex].parseSyncSearch = true;
		pAviDmuxCtlInfo->esCtlInfo[streamIndex].parseStartIndex = 0;
		pAviDmuxCtlInfo->esCtlInfo[streamIndex].parseSize = 0;
		pAviDmuxCtlInfo->esCtlInfo[streamIndex].parseAuCount = 0;
		pAviDmuxCtlInfo->esCtlInfo[streamIndex].parseLastError = CELL_OK;
	}
}
