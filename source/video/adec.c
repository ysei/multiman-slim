#include "adec.h"

extern u8 is_bg_video;

static void adecWaitEvent(SAdecCtlInfo* pAdecCtlInfo, SAdecStatus* pAdecThreadStatus, bool bAuInfo);
static int adecMakeSeqParam(SAdecCtlInfo* pAdecCtlInfo);
static int adecDmuxReleaseAu(SAdecCtlInfo* pAdecCtlInfo, uint32_t auNum);
static int32_t adecCb(CellAdecHandle handle, CellAdecMsgType msgType, int32_t msgData, void *cbArg);
static void adecThread(uint64_t arg);

CellAdecHandle adecGetAdecHandle(SAdecCtlInfo* pAdecCtlInfo){
	return pAdecCtlInfo->decHandle;
};

void adecPcmOutCb(void* arg){
	SAdecCtlInfo* pAdecCtlInfo = (SAdecCtlInfo*)arg;
	utilMonitorLock(&pAdecCtlInfo->umInput, 0);
	pAdecCtlInfo->recvStatus.bReady = true;
	utilMonitorSignal(&pAdecCtlInfo->umInput);
	utilMonitorUnlock(&pAdecCtlInfo->umInput);
}

void adecDmuxCb(uint32_t msg, void* arg){
	SAdecCtlInfo*	pAdecCtlInfo = (SAdecCtlInfo*)arg;

	utilMonitorLock(&pAdecCtlInfo->umInput, 0);

	switch(msg){
	case AVIDMUX_MSG_TYPE_AU_FOUND:
		pAdecCtlInfo->recvStatus.bAuFound = true;
		utilMonitorSignal(&pAdecCtlInfo->umInput);
		break;
	case AVIDMUX_MSG_TYPE_DMUX_DONE:
		pAdecCtlInfo->recvStatus.bStreamEnd = true;
		pAdecCtlInfo->recvStatus.bAuFound = true;
		utilMonitorSignal(&pAdecCtlInfo->umInput);
		break;
	default:
		EMSG("Callback Unknown %u\n", msg);
		break;
	}

	utilMonitorUnlock(&pAdecCtlInfo->umInput);
}

int adecSetParam(SAdecCtlInfo* pAdecCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SApostCtlInfo* pApostCtlInfo, uint32_t chNum)
{
	pAdecCtlInfo->pCommonCtlInfo = pCommonCtlInfo;
	pAdecCtlInfo->pApostCtlInfo = pApostCtlInfo;

	pAdecCtlInfo->chNum = chNum;

	return CELL_OK;
}

int adecOpen(SAdecCtlInfo* pAdecCtlInfo, const SAviDmuxStreamInfo* pStreamInfo,
	AviDmuxEsHandle esHandle, int32_t ppuThreadPriority, size_t ppuThreadStackSize,
	int32_t spuThreadPriority)
{
	int32_t				ret;
	CellAdecAttr		decAttr;
	CellAdecCb			cb;
	CellAdecResource	res;
	const UAviDmuxStreamSpecInfo*	pSpecInfo = &pStreamInfo->specInfo;

	pAdecCtlInfo->dmuxEsHandle = esHandle;

	switch(pSpecInfo->waveFormatEx.wFormatTag){
	case AVIDMUX_WAVE_FORMAT_MPEGLAYER3:
		pAdecCtlInfo->prxModuleId = CELL_SYSMODULE_ADEC_MP3;
		pAdecCtlInfo->decType.audioCodecType = CELL_ADEC_TYPE_MP3;
		break;
	case AVIDMUX_WAVE_FORMAT_AC3:
		pAdecCtlInfo->prxModuleId = CELL_SYSMODULE_ADEC_AC3;
		pAdecCtlInfo->decType.audioCodecType = CELL_ADEC_TYPE_AC3;
		break;
	case AVIDMUX_WAVE_FORMAT_MPEG:
		switch(pSpecInfo->mpeg1WaveFormat.fwHeadLayer){
		case AVIDMUX_ACM_MPEG_LAYER3:
			pAdecCtlInfo->prxModuleId = CELL_SYSMODULE_ADEC_MP3;
			pAdecCtlInfo->decType.audioCodecType = CELL_ADEC_TYPE_MP3;
			break;
		case AVIDMUX_ACM_MPEG_LAYER2:
			pAdecCtlInfo->prxModuleId = CELL_SYSMODULE_ADEC_M2BC;
			pAdecCtlInfo->decType.audioCodecType = CELL_ADEC_TYPE_MPEG_L2;
			break;
		case AVIDMUX_ACM_MPEG_LAYER1:
		default:
			EMSG("unsupported format, fwHeadLayer: %u.\n",
				pSpecInfo->mpeg1WaveFormat.fwHeadLayer);
			return RET_CODE_ERR_FATAL;
		}
		break;
	default:
		return RET_CODE_ERR_ARG;
	}

	ret = cellSysmoduleLoadModule(pAdecCtlInfo->prxModuleId);
	if(ret < CELL_OK){
		return ret;
	}

	ret = cellAdecQueryAttr(&pAdecCtlInfo->decType, &decAttr);
	if(ret < CELL_OK){
		goto L_ADEC_OPEN_ERR;
	}

	pAdecCtlInfo->memSize = decAttr.workMemSize;

	if(utilMonitorInit(&pAdecCtlInfo->umInput)){
		ret = RET_CODE_ERR_FATAL;
		goto L_ADEC_OPEN_ERR;
	}

	pAdecCtlInfo->pWorkMemory = malloc(pAdecCtlInfo->memSize);
	if(pAdecCtlInfo->pWorkMemory == NULL){
		ret = RET_CODE_ERR_FATAL;
		goto L_ADEC_OPEN_ERR;
	}

	cb.callbackFunc	= adecCb;
	cb.callbackArg	= pAdecCtlInfo;

	res.totalMemSize		= pAdecCtlInfo->memSize;
	res.startAddr			= pAdecCtlInfo->pWorkMemory;
	res.ppuThreadPriority	= ppuThreadPriority;
	res.spuThreadPriority	= spuThreadPriority;
	res.ppuThreadStackSize	= ppuThreadStackSize;

	ret = cellAdecOpen(&pAdecCtlInfo->decType, &res, &cb, &pAdecCtlInfo->decHandle);
	if(ret < CELL_OK){
		free(pAdecCtlInfo->pWorkMemory);
		goto L_ADEC_OPEN_ERR;
	}

	return CELL_OK;

L_ADEC_OPEN_ERR:
	cellSysmoduleUnloadModule(pAdecCtlInfo->prxModuleId);
	return ret;
}

int adecClose(SAdecCtlInfo* pAdecCtlInfo){
	int32_t	ret;
	ret = cellAdecClose(pAdecCtlInfo->decHandle);
	if(ret < CELL_OK){
		return ret;
	}
	free(pAdecCtlInfo->pWorkMemory);
	if(utilMonitorFin(&pAdecCtlInfo->umInput)){
		return RET_CODE_ERR_FATAL;
	}
	ret = cellSysmoduleUnloadModule(pAdecCtlInfo->prxModuleId);

	return ret;
}

int adecStart(SAdecCtlInfo* pAdecCtlInfo, int prio, size_t stacksize){
	int32_t	ret;
	pAdecCtlInfo->errorCode				= RET_CODE_ERR_INVALID;
	pAdecCtlInfo->recvStatus.bStreamEnd	= false;
	pAdecCtlInfo->recvStatus.bAuFound	= false;
	pAdecCtlInfo->recvStatus.bReady		= true;
	pAdecCtlInfo->recvStatus.auDoneNum	= 0;

	ret = adecMakeSeqParam(pAdecCtlInfo);
	if(ret < CELL_OK){
		return ret;
	}

	ret = cellAdecStartSeq(pAdecCtlInfo->decHandle, &pAdecCtlInfo->decParam);
	if(ret < CELL_OK){
		return ret;
	}

	ret = sys_ppu_thread_create(&pAdecCtlInfo->threadId, adecThread, (uintptr_t)pAdecCtlInfo,
		prio, stacksize, SYS_PPU_THREAD_CREATE_JOINABLE, __func__);

	return ret;
}

int adecEnd(SAdecCtlInfo* pAdecCtlInfo){
	uint64_t thrStat;
	int	ret = sys_ppu_thread_join(pAdecCtlInfo->threadId, &thrStat);
	if(ret < CELL_OK){
		return ret;
	}

	return pAdecCtlInfo->errorCode;
}

static void adecThread(uint64_t arg){
	SAdecCtlInfo*			pAdecCtlInfo = (SAdecCtlInfo*)(uintptr_t)arg;
	int32_t					dmuxRet;
	const SAviDmuxAuInfo	*pAuInfo = NULL;
	void					*pAuSpecificInfo;
	int32_t					adecRet;
	SAdecStatus				threadStatus = pAdecCtlInfo->recvStatus;

	DP("start...\n");

	while(1){
		adecWaitEvent(pAdecCtlInfo, &threadStatus, (pAuInfo)? true: false);

		if(pAdecCtlInfo->errorCode == CELL_OK){
			pAdecCtlInfo->errorCode = adecDmuxReleaseAu(pAdecCtlInfo, threadStatus.auDoneNum);
			break;
		}else if(pAdecCtlInfo->errorCode != RET_CODE_ERR_INVALID){
			break;
		}
		if( !( commonGetMode( pAdecCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) ){
			break;
		}

		bool		bBreakLoop = false;

		if(threadStatus.auDoneNum){
			int32_t	ret = adecDmuxReleaseAu(pAdecCtlInfo, threadStatus.auDoneNum);
			if(ret < CELL_OK){
				pAdecCtlInfo->errorCode = ret;
				break;
			}
		}

		if(threadStatus.bAuFound && pAuInfo == NULL){
			dmuxRet = aviDmuxGetAu(pAdecCtlInfo->dmuxEsHandle, &pAuInfo, &pAuSpecificInfo);
			switch(dmuxRet){
			case CELL_OK:
				break;
			case RET_CODE_ERR_EMPTY:
				threadStatus.bAuFound = false;
				if(!threadStatus.bStreamEnd){
					break;
				}
				pAdecCtlInfo->recvStatus.bAuFound = false;
				pAdecCtlInfo->recvStatus.bStreamEnd = false;
				while(1){
					adecRet = cellAdecEndSeq(pAdecCtlInfo->decHandle);
					if(adecRet != CELL_ADEC_ERROR_BUSY
					&& adecRet != CELL_ADEC_ERROR_AC3_BUSY
					&& adecRet != (int)CELL_ADEC_ERROR_MP3_BUSY){
						break;
					}
					sys_timer_usleep(RETRY_INTERVAL);
				}
				if(adecRet < CELL_OK){
					pAdecCtlInfo->errorCode = adecRet;
					bBreakLoop = true;
				}
				break;
			default:
				pAdecCtlInfo->errorCode = dmuxRet;
				bBreakLoop = true;
				break;
			}
			if(bBreakLoop){
				break;
			}
		}

		if(threadStatus.bReady && pAuInfo != NULL){
			CellAdecAuInfo		au = { pAuInfo->auAddr, pAuInfo->auSize,
				{ pAuInfo->ptsUpper, pAuInfo->ptsLower }, 0 };
			adecRet = cellAdecDecodeAu(pAdecCtlInfo->decHandle, &au);
			switch(adecRet){
			case CELL_OK:
				pAuInfo = NULL;
				break;
			case CELL_ADEC_ERROR_BUSY:
			case CELL_ADEC_ERROR_AC3_BUSY:
			case CELL_ADEC_ERROR_MP3_BUSY:
				threadStatus.bReady = false;
				break;
			default:
				pAdecCtlInfo->errorCode = adecRet;
				bBreakLoop = true;
				break;
			}
			if(bBreakLoop){
				break;
			}
		}

		if(!is_bg_video) {pAdecCtlInfo->errorCode=RET_CODE_ERR_FORCE_EXIT; break;}
	}

	if(pAdecCtlInfo->errorCode < CELL_OK
	&& pAdecCtlInfo->errorCode != RET_CODE_ERR_INVALID){
		commonErrorExit(pAdecCtlInfo->pCommonCtlInfo, pAdecCtlInfo->errorCode);
		EINFO(pAdecCtlInfo->errorCode);
	}else{
		pAdecCtlInfo->errorCode = CELL_OK;
	}

	sys_ppu_thread_exit(0);
}

static void adecWaitEvent(SAdecCtlInfo* pAdecCtlInfo, SAdecStatus* pAdecThreadStatus, bool bAuInfo){
	SAdecStatus* pRecvStatus = &pAdecCtlInfo->recvStatus;

	utilMonitorLock(&pAdecCtlInfo->umInput, 0);

	//E update receiving status with thread status
	if(!pRecvStatus->bReady){
		pRecvStatus->bReady = pAdecThreadStatus->bReady;
	}
	if(!pRecvStatus->bAuFound){
		pRecvStatus->bAuFound = pAdecThreadStatus->bAuFound;
	}

	//E wait condition
	while( ( commonGetMode( pAdecCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) &&
		   pAdecCtlInfo->errorCode == RET_CODE_ERR_INVALID &&
		   ( !(pRecvStatus->auDoneNum | pRecvStatus->bReady) ||
			 !(pRecvStatus->auDoneNum | pRecvStatus->bAuFound | bAuInfo | !pRecvStatus->bReady) ) ){
		utilMonitorWait(&pAdecCtlInfo->umInput, 0);
	}

	//E copy from receiving status to thread status
	*pAdecThreadStatus = *pRecvStatus;

	//E clear receiving status
	pRecvStatus->auDoneNum = 0;
	pRecvStatus->bAuFound = false;
	pRecvStatus->bReady = false;

	utilMonitorUnlock(&pAdecCtlInfo->umInput);
}

static int adecMakeSeqParam(SAdecCtlInfo* pAdecCtlInfo){
	UAdecParam*	pDecParam = &pAdecCtlInfo->decParam;
	uint32_t	chNum = 6;//pAdecCtlInfo->chNum;

	switch(pAdecCtlInfo->decType.audioCodecType){
	case CELL_ADEC_TYPE_AC3:
		pDecParam->ac3.wordSize		= CELL_ADEC_AC3_WORD_SZ_FLOAT;
		switch(chNum){
		case 1:
			pDecParam->ac3.outputMode	= CELL_ADEC_AC3_OUTPUT_MODE_1_0;
			pDecParam->ac3.outLfeOn		= CELL_ADEC_AC3_LFE_NOT_PRESENT;
			pDecParam->ac3.channelPointer0 = CELL_ADEC_AC3_INPUT_CHANNEL_C;
			pDecParam->ac3.channelPointer1 = CELL_ADEC_AC3_INPUT_CHANNEL_L;
			pDecParam->ac3.channelPointer2 = CELL_ADEC_AC3_INPUT_CHANNEL_R;
			break;
		case 2:
			pDecParam->ac3.outputMode	= CELL_ADEC_AC3_OUTPUT_MODE_2_0;
			pDecParam->ac3.outLfeOn		= CELL_ADEC_AC3_LFE_NOT_PRESENT;
			pDecParam->ac3.channelPointer0 = CELL_ADEC_AC3_INPUT_CHANNEL_L;
			pDecParam->ac3.channelPointer1 = CELL_ADEC_AC3_INPUT_CHANNEL_R;
			pDecParam->ac3.channelPointer2 = CELL_ADEC_AC3_INPUT_CHANNEL_C;
			break;
		case 6:
			pDecParam->ac3.outputMode	= CELL_ADEC_AC3_OUTPUT_MODE_3_2;
			pDecParam->ac3.outLfeOn		= CELL_ADEC_AC3_LFE_PRESENT;
			pDecParam->ac3.channelPointer0 = CELL_ADEC_AC3_INPUT_CHANNEL_L;
			pDecParam->ac3.channelPointer1 = CELL_ADEC_AC3_INPUT_CHANNEL_C;
			pDecParam->ac3.channelPointer2 = CELL_ADEC_AC3_INPUT_CHANNEL_R;
			break;
		default:
			return RET_CODE_ERR_ARG;
		}
		pDecParam->ac3.channelPointer3 = CELL_ADEC_AC3_INPUT_CHANNEL_l;
		pDecParam->ac3.channelPointer4 = CELL_ADEC_AC3_INPUT_CHANNEL_r;
		pDecParam->ac3.channelPointer5 = CELL_ADEC_AC3_INPUT_CHANNEL_s;
		pDecParam->ac3.drcCutScaleFactor = 1.0f;
		pDecParam->ac3.drcBoostScaleFactor = 1.0f;
		pDecParam->ac3.compressionMode = CELL_ADEC_AC3_COMPRESSION_MODE_LINE_OUT;
		pDecParam->ac3.numberOfChannels = chNum;
		pDecParam->ac3.stereoMode	= CELL_ADEC_AC3_STEREO_MODE_AUTO_DETECT;
		pDecParam->ac3.dualmonoMode = CELL_ADEC_AC3_DUALMONO_MODE_STEREO;
		pDecParam->ac3.karaokeCapableMode
			= CELL_ADEC_AC3_KARAOKE_CAPABLE_MODE_BOTH_VOCAL;
		pDecParam->ac3.pcmScaleFactor  = 1.0f;
		break;
	case CELL_ADEC_TYPE_MP3:
		pDecParam->mp3.bw_pcm = CELL_ADEC_MP3_WORD_SZ_FLOAT;
		break;
	case CELL_ADEC_TYPE_MPEG_L2:
		pDecParam->mp2.channelNumber = chNum;
		pDecParam->mp2.downmix = 0;
		pDecParam->mp2.lfeUpSample = 0;
		break;
	default:
		return RET_CODE_ERR_ARG;
	}

	return CELL_OK;
}

static int adecDmuxReleaseAu(SAdecCtlInfo* pAdecCtlInfo, uint32_t auNum){
	int	ret = CELL_OK;
	while(auNum--){
		int32_t dmuxRet = aviDmuxReleaseAu(pAdecCtlInfo->dmuxEsHandle);
		if(dmuxRet < CELL_OK){
			ret = dmuxRet;
			break;
		}
	}
	return ret;
}

static int32_t adecCb(CellAdecHandle handle, CellAdecMsgType msgType, int32_t msgData, void *cbArg){
	(void)handle;
	SAdecCtlInfo*	pAdecCtlInfo = (SAdecCtlInfo*)cbArg;

	utilMonitorLock(&pAdecCtlInfo->umInput, 0);

	switch( msgType ){
	case CELL_ADEC_MSG_TYPE_AUDONE:
		++pAdecCtlInfo->recvStatus.auDoneNum;
		break;

	case CELL_ADEC_MSG_TYPE_PCMOUT:
		apostPcmOutCb(pAdecCtlInfo->pApostCtlInfo);
		break;

	case CELL_ADEC_MSG_TYPE_SEQDONE:
		apostSeqEndCb(pAdecCtlInfo->pApostCtlInfo);
		pAdecCtlInfo->errorCode = CELL_OK;
		break;

	case CELL_ADEC_MSG_TYPE_ERROR:
		EMSG("adec fatal error!! (0x%x) exit\n", msgData);
		pAdecCtlInfo->errorCode = RET_CODE_ERR_FATAL;
		break;
	}

	utilMonitorSignal(&pAdecCtlInfo->umInput);
	utilMonitorUnlock(&pAdecCtlInfo->umInput);

	return 0;
}

