#include "vdec.h"

static void vdecWaitEvent(SVdecCtlInfo* pVdecCtlInfo, SVdecStatus* pVdecThreadStatus, bool bAuInfo);
static int vdecDmuxReleaseAu(SVdecCtlInfo* pVdecCtlInfo, uint32_t auNum);
static uint32_t vdecCb(CellVdecHandle handle, CellVdecMsgType msgType, int32_t msgData, void *cbArg);
static void vdecThread(uint64_t arg);

CellVdecHandle vdecGetVdecHandle(SVdecCtlInfo* pVdecCtlInfo){
	return pVdecCtlInfo->decHandle;
};

void vdecPictureOutCb(void* arg){
	(void)arg;
	/* do nothing. */
}

void vdecDmuxCb(uint32_t msg, void* arg){
	SVdecCtlInfo*	pVdecCtlInfo = (SVdecCtlInfo*)arg;

	utilMonitorLock(&pVdecCtlInfo->umInput, 0);

	switch(msg){
	case AVIDMUX_MSG_TYPE_AU_FOUND:
		pVdecCtlInfo->recvStatus.bAuFound = true;
		utilMonitorSignal(&pVdecCtlInfo->umInput);
		break;
	case AVIDMUX_MSG_TYPE_DMUX_DONE:
		pVdecCtlInfo->recvStatus.bStreamEnd	= true;
		pVdecCtlInfo->recvStatus.bAuFound	= true;
		utilMonitorSignal(&pVdecCtlInfo->umInput);
		break;
	default:
		EMSG("Callback Unknown %u\n", msg);
		break;
	}

	utilMonitorUnlock(&pVdecCtlInfo->umInput);
}

int vdecSetParam(SVdecCtlInfo* pVdecCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SVpostCtlInfo* pVpostCtlInfo)
{
	pVdecCtlInfo->pCommonCtlInfo	= pCommonCtlInfo;
	pVdecCtlInfo->pVpostCtlInfo		= pVpostCtlInfo;

	return CELL_OK;
}

int vdecOpen(SVdecCtlInfo* pVdecCtlInfo, const SAviDmuxStreamInfo* pStreamInfo,
	AviDmuxEsHandle esHandle, int32_t ppuThreadPriority, size_t ppuThreadStackSize,
	int32_t spuThreadPriority, uint32_t numOfSpus)
{
	int32_t				ret;
	CellVdecAttr		decAttr;
	CellVdecCb			cb;
	CellVdecResource	res;

	pVdecCtlInfo->dmuxEsHandle = esHandle;

	pVdecCtlInfo->prxModuleId		= CELL_SYSMODULE_VDEC_DIVX;
	pVdecCtlInfo->decType.codecType	= CELL_VDEC_CODEC_TYPE_DIVX;

	const uint32_t	frameRate = pStreamInfo->dwRate / pStreamInfo->dwScale;
	const uint32_t	width = pStreamInfo->specInfo.bmpInfoHeader.biWidth;
	const uint32_t	height = pStreamInfo->specInfo.bmpInfoHeader.biHeight;
	if(width <= 176 && height <= 144 && frameRate <= 15){
		pVdecCtlInfo->decType.profileLevel = CELL_VDEC_DIVX_QMOBILE;
	}else if(width <= 320 && height <= 240 && frameRate <= 30){
		pVdecCtlInfo->decType.profileLevel = CELL_VDEC_DIVX_MOBILE;
	}else if(width <= 720 && height <= 576 && frameRate <= 30){
		pVdecCtlInfo->decType.profileLevel = CELL_VDEC_DIVX_HOME_THEATER;
	}else if(width <= 1280 && height <= 720 && frameRate <= 60){
		pVdecCtlInfo->decType.profileLevel = CELL_VDEC_DIVX_HD_720;
	}else{
		pVdecCtlInfo->decType.profileLevel = CELL_VDEC_DIVX_HD_1080;
	}
	switch(frameRate){
	case 23: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_24000DIV1001;	break;
	case 24: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_24;			break;
	case 25: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_25;			break;
	case 29: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_30000DIV1001;	break;
	case 30: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_30;			break;
	case 50: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_50;			break;
	case 59: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_60000DIV1001;	break;
	case 60: pVdecCtlInfo->frameRate = CELL_VDEC_FRC_60;			break;
	default:
		EMSG("unsupported frame rate, dwRate: %u, dwScale: %u.\n",
			pStreamInfo->dwRate, pStreamInfo->dwScale);
		return RET_CODE_ERR_FATAL;
	}
	DP("profileLevel: %u, frameRate: %u\n",
		pVdecCtlInfo->decType.profileLevel, pVdecCtlInfo->frameRate);

	ret = cellSysmoduleLoadModule(pVdecCtlInfo->prxModuleId);
	if(ret < CELL_OK){
		return ret;
	}

	ret = cellVdecQueryAttr(&pVdecCtlInfo->decType, &decAttr);
	if(ret < CELL_OK){
		goto L_VDEC_OPEN_ERR;
	}

	pVdecCtlInfo->memSize = decAttr.memSize;

	if(utilMonitorInit(&pVdecCtlInfo->umInput)){
		ret = RET_CODE_ERR_FATAL;
		goto L_VDEC_OPEN_ERR;
	}

	pVdecCtlInfo->pWorkMemory = malloc(pVdecCtlInfo->memSize);
	if(pVdecCtlInfo->pWorkMemory == NULL){
		ret = RET_CODE_ERR_FATAL;
		goto L_VDEC_OPEN_ERR;
	}

	cb.cbFunc	= vdecCb;
	cb.cbArg	= pVdecCtlInfo;

	res.memAddr				= pVdecCtlInfo->pWorkMemory;
	res.memSize				= pVdecCtlInfo->memSize;
	res.ppuThreadPriority	= ppuThreadPriority;
	res.ppuThreadStackSize	= ppuThreadStackSize;
	res.spuThreadPriority	= spuThreadPriority;
	res.numOfSpus			= numOfSpus;

	ret = cellVdecOpen(&pVdecCtlInfo->decType, &res, &cb, &pVdecCtlInfo->decHandle);
	if(ret < CELL_OK){
		free(pVdecCtlInfo->pWorkMemory);
		goto L_VDEC_OPEN_ERR;
	}

	return CELL_OK;

L_VDEC_OPEN_ERR:
	cellSysmoduleUnloadModule(pVdecCtlInfo->prxModuleId);
	return ret;
}

int vdecClose(SVdecCtlInfo* pVdecCtlInfo){
	int32_t		ret;
	ret = cellVdecClose(pVdecCtlInfo->decHandle);
	if(ret < CELL_OK){
		return ret;
	}
	free(pVdecCtlInfo->pWorkMemory);
	if(utilMonitorFin(&pVdecCtlInfo->umInput)){
		return RET_CODE_ERR_FATAL;
	}
	ret = cellSysmoduleUnloadModule(pVdecCtlInfo->prxModuleId);

	return ret;
}

int vdecStart(SVdecCtlInfo* pVdecCtlInfo, int prio, size_t stacksize){
	int32_t	ret;
	pVdecCtlInfo->errorCode				= RET_CODE_ERR_INVALID;
	pVdecCtlInfo->recvStatus.bAuFound	= false;
	pVdecCtlInfo->recvStatus.bStreamEnd	= false;
	pVdecCtlInfo->recvStatus.bReady		= true;
	pVdecCtlInfo->recvStatus.auDoneNum	= 0;

	ret = cellVdecStartSeq(pVdecCtlInfo->decHandle);
	if(ret < CELL_OK){
		return ret;
	}

	if(pVdecCtlInfo->decType.codecType == CELL_VDEC_CODEC_TYPE_DIVX){
		ret = cellVdecSetFrameRate(pVdecCtlInfo->decHandle, pVdecCtlInfo->frameRate);
		if(ret < CELL_OK){
			return ret;
		}
	}

	ret = sys_ppu_thread_create(&pVdecCtlInfo->threadId, vdecThread, (uintptr_t)pVdecCtlInfo,
		prio, stacksize, SYS_PPU_THREAD_CREATE_JOINABLE, __func__);

	return ret;
}

int vdecEnd(SVdecCtlInfo* pVdecCtlInfo){
	uint64_t thrStat;
	int	ret = sys_ppu_thread_join(pVdecCtlInfo->threadId, &thrStat);
	if(ret < CELL_OK){
		return ret;
	}

	return pVdecCtlInfo->errorCode;
}

static void vdecThread(uint64_t arg){
	SVdecCtlInfo*			pVdecCtlInfo = (SVdecCtlInfo*)(uintptr_t)arg;
	int32_t					dmuxRet;
	const SAviDmuxAuInfo	*pAuInfo = NULL;
	void					*pAuSpecificInfo;
	int32_t					vdecRet;
	SVdecStatus				threadStatus = pVdecCtlInfo->recvStatus;

	DP("start...\n");

	while(1){
		vdecWaitEvent(pVdecCtlInfo, &threadStatus, (pAuInfo)? true: false);

		if(pVdecCtlInfo->errorCode == CELL_OK){
			pVdecCtlInfo->errorCode = vdecDmuxReleaseAu(pVdecCtlInfo, threadStatus.auDoneNum);
			break;
		}else if(pVdecCtlInfo->errorCode != RET_CODE_ERR_INVALID){
			break;
		}
		if(!( commonGetMode( pVdecCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) ){
			break;
		}

		bool		bBreakLoop = false;

		if(threadStatus.auDoneNum){
			int32_t	ret = vdecDmuxReleaseAu(pVdecCtlInfo, threadStatus.auDoneNum);
			if(ret < CELL_OK){
				pVdecCtlInfo->errorCode = ret;
				break;
			}
		}

		if(threadStatus.bAuFound && pAuInfo == NULL){
			dmuxRet = aviDmuxGetAu(pVdecCtlInfo->dmuxEsHandle, &pAuInfo, &pAuSpecificInfo);
			switch(dmuxRet){
			case CELL_OK:
				break;
			case RET_CODE_ERR_EMPTY:
				threadStatus.bAuFound = false;
				if(!threadStatus.bStreamEnd){
					break;
				}
				pVdecCtlInfo->recvStatus.bAuFound = false;
				pVdecCtlInfo->recvStatus.bStreamEnd = false;
				while(1){
					vdecRet = cellVdecEndSeq(pVdecCtlInfo->decHandle);
					if(vdecRet != CELL_VDEC_ERROR_BUSY){
						break;
					}
					sys_timer_usleep(RETRY_INTERVAL);
				}
				if(vdecRet < CELL_OK){
					pVdecCtlInfo->errorCode = vdecRet;
					bBreakLoop = true;
				}
				break;
			default:
				pVdecCtlInfo->errorCode = dmuxRet;
				bBreakLoop = true;
				break;
			}
			if(bBreakLoop){
				break;
			}
		}

		if(threadStatus.bReady && pAuInfo != NULL){
			CellVdecDecodeMode	mode = CELL_VDEC_DEC_MODE_NORMAL;
			CellVdecAuInfo		au = {
				pAuInfo->auAddr, pAuInfo->auSize, { pAuInfo->ptsUpper, pAuInfo->ptsLower },
				{ pAuInfo->dtsUpper, pAuInfo->dtsLower }, 0, 0
			};
			vdecRet = cellVdecDecodeAu(pVdecCtlInfo->decHandle, mode, &au);
			switch(vdecRet){
			case CELL_OK:
				pAuInfo = NULL;
				break;
			case CELL_VDEC_ERROR_BUSY:
				threadStatus.bReady = false;
				break;
			default:
				pVdecCtlInfo->errorCode = vdecRet;
				bBreakLoop = true;
				break;
			}
			if(bBreakLoop){
				break;
			}
		}

	}

	if(pVdecCtlInfo->errorCode < CELL_OK
	&& pVdecCtlInfo->errorCode != RET_CODE_ERR_INVALID){
		commonErrorExit(pVdecCtlInfo->pCommonCtlInfo, pVdecCtlInfo->errorCode);
		EINFO(pVdecCtlInfo->errorCode);
	}else{
		pVdecCtlInfo->errorCode = CELL_OK;
	}

	DP("end...\n");

	sys_ppu_thread_exit(0);
}

static void vdecWaitEvent(SVdecCtlInfo* pVdecCtlInfo, SVdecStatus* pVdecThreadStatus, bool bAuInfo){
	SVdecStatus* pRecvStatus = &pVdecCtlInfo->recvStatus;

	utilMonitorLock(&pVdecCtlInfo->umInput, 0);

	// update receiving status with thread status
	if(!pRecvStatus->bReady){
		pRecvStatus->bReady = pVdecThreadStatus->bReady;
	}
	if(!pRecvStatus->bAuFound){
		pRecvStatus->bAuFound = pVdecThreadStatus->bAuFound;
	}

	// wait condition
	while( ( commonGetMode( pVdecCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) &&
		   pVdecCtlInfo->errorCode == RET_CODE_ERR_INVALID &&
		   ( !(pRecvStatus->auDoneNum | pRecvStatus->bReady) ||
			 !(pRecvStatus->auDoneNum | pRecvStatus->bAuFound | bAuInfo | !pRecvStatus->bReady) ) ){
		utilMonitorWait(&pVdecCtlInfo->umInput, 0);
	}

	// copy from receiving status to thread status
	*pVdecThreadStatus = *pRecvStatus;

	// clear receiving status
	pRecvStatus->auDoneNum = 0;
	pRecvStatus->bAuFound = false;
	pRecvStatus->bReady = false;

	utilMonitorUnlock(&pVdecCtlInfo->umInput);
}

static int vdecDmuxReleaseAu(SVdecCtlInfo* pVdecCtlInfo, uint32_t auNum){
	int	ret = CELL_OK;
	while(auNum--){
		int32_t dmuxRet = aviDmuxReleaseAu(pVdecCtlInfo->dmuxEsHandle);
		if(dmuxRet < CELL_OK){
			ret = dmuxRet;
			break;
		}
	}
	return ret;
}

static uint32_t vdecCb(CellVdecHandle handle, CellVdecMsgType msgType, int32_t msgData, void *cbArg){
	(void)handle;
	SVdecCtlInfo*	pVdecCtlInfo = (SVdecCtlInfo*)cbArg;

	utilMonitorLock(&pVdecCtlInfo->umInput, 0);

	switch( msgType ){
	case CELL_VDEC_MSG_TYPE_AUDONE:
		if(CELL_OK != msgData){
			EMSG("Invalid AU was found in Video Decoder.\n");
		}
		++pVdecCtlInfo->recvStatus.auDoneNum;
		pVdecCtlInfo->recvStatus.bReady = true;
		break;

	case CELL_VDEC_MSG_TYPE_PICOUT:
		vpostPictureOutCb(pVdecCtlInfo->pVpostCtlInfo);
		break;

	case CELL_VDEC_MSG_TYPE_SEQDONE:
		vpostSeqEndCb(pVdecCtlInfo->pVpostCtlInfo);
		pVdecCtlInfo->errorCode = CELL_OK;
		break;

	case CELL_VDEC_MSG_TYPE_ERROR:
		EMSG("vdec fatal error!! (0x%x) exit\n", msgData);
		pVdecCtlInfo->errorCode = RET_CODE_ERR_FATAL;
		break;
	}

	utilMonitorSignal(&pVdecCtlInfo->umInput);
	utilMonitorUnlock(&pVdecCtlInfo->umInput);

	return 0;
}

